#ifndef HFT_Q21_UDP_FEED_CAPTURE_SPOOLER_HPP
#define HFT_Q21_UDP_FEED_CAPTURE_SPOOLER_HPP

// ============================================================================
// QUESTION 21 [TIER 5 - VERY HARD]: ZERO-DROP MARKET DATA CAPTURE & SPOOLER
// ============================================================================
// EXCLUSIVE PROBLEM SCOPE (NON-OVERLAPPING):
//   Multi-stage, core-pinned market-data packet capture pipeline decoupling:
//     1. Fast Capture Core (Kernel-Bypass / UDP Multicast / User-Space TCP):
//        Writes variable-length wire frames prefixed with a 24-byte hardware
//        timestamp & channel header (`CaptureFrameHeader`) into a lock-free,
//        page-prefaulted SPSC byte ring (`VariableByteRingBuffer`).
//        - Contiguous-frame guarantee: if a variable-length frame would straddle
//          the end of the physical ring, writes a 24-byte `kWrapPaddingSentinel`
//          header at the tail and places the entire frame contiguously at offset 0
//          so zero-copy DMA/socket receives and parser spans never split across wrap.
//        - Supports two-phase zero-copy reservation (`claim_frame(max_mtu)` ->
//          NIC/socket receive -> `commit_frame(claim, actual_len)`).
//        - Shadow-index caching (`alignas(64)`) avoids cross-core MESI traffic.
//     2. Raw Chunk Spooler Core (Batched Sequential NVMe Residency):
//        Scans contiguous multi-frame slabs (`peek_contiguous_slab`) and coalesces
//        consecutive frames up to `rotate_threshold_bytes` into a single sequential
//        batch write call to the sink, rotating `.raw` chunks on clean frame
//        boundaries and invoking a flush/close hook before publishing descriptors.
//     3. Decoupled Downstream Fan-Out & Reference-Counted `.raw` Unlink:
//        Publishes `ClosedChunkDescriptor` notifications to two Vyukov MPMC queues:
//        (a) a multi-threaded ZSTD compression + channel-splitter worker pool, and
//        (b) an asynchronous streaming relay (e.g., `librdkafka` WAN publisher).
//        Each closed chunk tracks an atomic 2-consumer completion bitmask so the
//        `.raw` file is unlinked only after BOTH compression and relay finish.
// ============================================================================

#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <span>
#include <type_traits>

namespace hft {

inline constexpr std::size_t kCaptureCacheLine = 64;
inline constexpr std::size_t kOsPageSize       = 4096;

static_assert(std::atomic<std::uint64_t>::is_always_lock_free,
              "64-bit atomic offsets must be lock-free");

// Bitmask flags stored in CaptureFrameHeader::flags
enum CaptureFlags : std::uint32_t {
    kFlagNone          = 0u,
    kFlagHwTimestamp   = 1u << 0, // NIC PHC / hardware MAC timestamp
    kFlagKernelBypass  = 1u << 1, // Captured via user-space NIC ring
    kFlagTcpStream     = 1u << 2  // User-space TCP stream segment (vs UDP datagram)
};

// 24-byte, 8-byte-aligned binary frame header preceding each captured packet
struct alignas(8) CaptureFrameHeader {
    std::uint64_t rx_timestamp_ns{0}; // Hardware/software ingress timestamp (UTC ns)
    std::uint64_t channel_id{0};      // 64-bit hash/ID of multicast group or feed URI
    std::uint32_t payload_len{0};     // Wire payload bytes (or kWrapPaddingSentinel)
    std::uint32_t flags{0};           // Combination of CaptureFlags
};
static_assert(sizeof(CaptureFrameHeader) == 24, "CaptureFrameHeader must be 24 bytes");
static_assert(alignof(CaptureFrameHeader) == 8, "CaptureFrameHeader must be 8-byte aligned");

inline constexpr std::uint32_t kWrapPaddingSentinel = 0xFFFF'FFFFu;
inline constexpr std::uint32_t kMaxWirePayloadLimit = 0x7FFF'FFF0u;

[[nodiscard]] constexpr std::size_t align_up_8(std::size_t n) noexcept {
    return (n + 7u) & ~std::size_t{7u};
}

// ============================================================================
// 1. Lock-Free SPSC Variable-Length Byte Ring Buffer (Fast Capture -> Spooler)
// ============================================================================
template <std::size_t CapacityBytes>
    requires(CapacityBytes >= 256 && ((CapacityBytes & (CapacityBytes - 1)) == 0))
class VariableByteRingBuffer {
public:
    static constexpr std::size_t kMask = CapacityBytes - 1;
    static constexpr std::size_t kHeaderSize = sizeof(CaptureFrameHeader);

    // Maximum single payload that can fit along with its header and a worst-case wrap sentinel
    static constexpr std::size_t kMaxPayloadBytes = std::min<std::size_t>(
        (CapacityBytes / 2) - (2 * kHeaderSize),
        static_cast<std::size_t>(kMaxWirePayloadLimit));

    struct WriteClaim {
        std::span<std::byte> payload_span{};
        std::uint64_t base_write_offset{0};
        std::size_t   header_phys_pos{0};
        std::size_t   wrap_pad_bytes{0};
        std::uint32_t claimed_payload_len{0};
        bool          active{false};
    };

    constexpr VariableByteRingBuffer() noexcept = default;

    VariableByteRingBuffer(const VariableByteRingBuffer&) = delete;
    VariableByteRingBuffer& operator=(const VariableByteRingBuffer&) = delete;

    // Touch every 4 KB page at startup so the fast capture thread never takes
    // a first-touch minor page fault on the critical path.
    void prefault_pages() noexcept {
        for (std::size_t off = 0; off < CapacityBytes; off += kOsPageSize) {
            std::atomic_ref<std::byte> page_byte(m_storage[off]);
            page_byte.store(std::byte{0}, std::memory_order_relaxed);
        }
        std::atomic_ref<std::byte> last_byte(m_storage[CapacityBytes - 1]);
        last_byte.store(std::byte{0}, std::memory_order_relaxed);
    }

    // Zero-copy two-phase producer API:
    // Step 1: Reserve contiguous space for up to `max_payload_len` bytes (e.g. MTU 1500).
    // If `record_drop_on_full` is true, increments `dropped_packets()` when full.
    [[nodiscard]] std::optional<WriteClaim> claim_frame(
        std::uint64_t rx_timestamp_ns,
        std::uint64_t channel_id,
        std::uint32_t max_payload_len,
        std::uint32_t flags = kFlagHwTimestamp | kFlagKernelBypass,
        bool record_drop_on_full = true) noexcept
    {
        return claim_frame_impl(
            rx_timestamp_ns,
            channel_id,
            max_payload_len,
            flags,
            record_drop_on_full,
            true);
    }

    // Step 2a: Commit the full claimed payload length
    bool commit_frame(WriteClaim& claim) noexcept {
        return commit_frame(claim, claim.claimed_payload_len);
    }

    // Step 2b: Commit an actual received payload length (`actual_payload_len <= claimed_payload_len`),
    // supporting zero-copy receives where MTU is claimed before the packet size is known.
    bool commit_frame(WriteClaim& claim, std::uint32_t actual_payload_len) noexcept {
        if (!claim.active ||
            actual_payload_len > claim.claimed_payload_len ||
            claim.base_write_offset != m_write_offset.load(std::memory_order_relaxed)) [[unlikely]]
        {
            return false;
        }

        const std::size_t actual_aligned = align_up_8(actual_payload_len);
        const std::size_t actual_frame_bytes = kHeaderSize + actual_aligned;
        const std::size_t phys_end = claim.header_phys_pos + actual_frame_bytes;
        const std::size_t tail_after = CapacityBytes - phys_end;

        if (tail_after > 0 && tail_after < kHeaderSize) [[unlikely]] {
            return false;
        }

        if (actual_payload_len != claim.claimed_payload_len) {
            std::memcpy(
                m_storage.data() + claim.header_phys_pos + offsetof(CaptureFrameHeader, payload_len),
                &actual_payload_len,
                sizeof(actual_payload_len));
        }

        if (actual_aligned > actual_payload_len) {
            std::memset(
                m_storage.data() + claim.header_phys_pos + kHeaderSize + actual_payload_len,
                0,
                actual_aligned - actual_payload_len);
        }

        claim.active = false;
        const std::size_t committed_bytes = claim.wrap_pad_bytes + actual_frame_bytes;
        m_write_offset.store(claim.base_write_offset + committed_bytes, std::memory_order_release);
        return true;
    }

    // Convenience one-shot producer write
    [[nodiscard]] bool try_write_packet(
        std::uint64_t rx_timestamp_ns,
        std::uint64_t channel_id,
        std::span<const std::byte> payload,
        std::uint32_t flags = kFlagHwTimestamp | kFlagKernelBypass,
        bool record_drop_on_full = true) noexcept
    {
        if (payload.size() > kMaxPayloadBytes) [[unlikely]] {
            m_oversized_drops.store(
                m_oversized_drops.load(std::memory_order_relaxed) + 1,
                std::memory_order_relaxed);
            return false;
        }
        auto claim = claim_frame_impl(
            rx_timestamp_ns,
            channel_id,
            static_cast<std::uint32_t>(payload.size()),
            flags,
            record_drop_on_full,
            false);
        if (!claim.has_value()) [[unlikely]] {
            return false;
        }
        if (!payload.empty()) {
            std::memcpy(claim->payload_span.data(), payload.data(), payload.size());
        }
        return commit_frame(*claim);
    }

    // Consumer API (Raw Spooler):
    // Returns the largest contiguous readable byte slab up to `max_bytes`
    // without crossing the physical end of the ring buffer.
    [[nodiscard]] std::span<const std::byte> peek_contiguous_slab(
        std::size_t max_bytes = CapacityBytes) noexcept
    {
        const std::uint64_t r = m_read_offset.load(std::memory_order_relaxed);
        if (r == m_write_offset_cache) [[unlikely]] {
            m_write_offset_cache = m_write_offset.load(std::memory_order_acquire);
            if (r == m_write_offset_cache) [[unlikely]] {
                return {};
            }
        }

        const std::size_t avail = static_cast<std::size_t>(m_write_offset_cache - r);
        const std::size_t phys_r = static_cast<std::size_t>(r) & kMask;
        const std::size_t contiguous = std::min({avail, CapacityBytes - phys_r, max_bytes});
        return std::span<const std::byte>(m_storage.data() + phys_r, contiguous);
    }

    // Advance consumer read offset after sequential disk write completes.
    // Overflow-safe check against published distance prevents consumer overrun.
    bool advance_read(std::size_t bytes_consumed) noexcept {
        const std::uint64_t r = m_read_offset.load(std::memory_order_relaxed);
        if (bytes_consumed > (m_write_offset_cache - r)) [[unlikely]] {
            m_write_offset_cache = m_write_offset.load(std::memory_order_acquire);
            if (bytes_consumed > (m_write_offset_cache - r)) [[unlikely]] {
                return false;
            }
        }
        m_read_offset.store(r + bytes_consumed, std::memory_order_release);
        return true;
    }

    // Thread-safe approximate readable bytes (handles concurrent producer/consumer progress)
    [[nodiscard]] std::size_t readable_bytes_approx() const noexcept {
        const std::uint64_t r = m_read_offset.load(std::memory_order_acquire);
        const std::uint64_t w = m_write_offset.load(std::memory_order_acquire);
        if (w < r) {
            return 0;
        }
        const std::uint64_t diff = w - r;
        if (diff > CapacityBytes) {
            return CapacityBytes;
        }
        return static_cast<std::size_t>(diff);
    }

    [[nodiscard]] std::uint64_t dropped_packets() const noexcept {
        return m_dropped_packets.load(std::memory_order_relaxed);
    }

    [[nodiscard]] std::uint64_t oversized_drops() const noexcept {
        return m_oversized_drops.load(std::memory_order_relaxed);
    }

    [[nodiscard]] static constexpr std::size_t capacity_bytes() noexcept {
        return CapacityBytes;
    }

private:
    [[nodiscard]] std::optional<WriteClaim> claim_frame_impl(
        std::uint64_t rx_timestamp_ns,
        std::uint64_t channel_id,
        std::uint32_t max_payload_len,
        std::uint32_t flags,
        bool record_drop_on_full,
        bool allow_shrink) noexcept
    {
        if (max_payload_len > kMaxPayloadBytes) [[unlikely]] {
            m_oversized_drops.store(
                m_oversized_drops.load(std::memory_order_relaxed) + 1,
                std::memory_order_relaxed);
            return std::nullopt;
        }

        const std::size_t frame_bytes = kHeaderSize + align_up_8(max_payload_len);
        const std::uint64_t w = m_write_offset.load(std::memory_order_relaxed);
        const std::size_t phys_w = static_cast<std::size_t>(w) & kMask;
        const std::size_t tail_room = CapacityBytes - phys_w;

        // If the frame would straddle the physical ring wrap boundary, or leave
        // fewer than kHeaderSize bytes at the tail (unable to hold a wrap sentinel
        // on the next write, including when shrunk during commit_frame), emit a
        // 24-byte wrap sentinel at `phys_w` and place the frame at physical offset 0.
        const bool needs_wrap_pad = allow_shrink
            ? ((frame_bytes + kHeaderSize) > tail_room)
            : ((frame_bytes > tail_room) || (tail_room > frame_bytes && (tail_room - frame_bytes) < kHeaderSize));

        const std::size_t wrap_pad_bytes = needs_wrap_pad ? tail_room : 0;
        const std::size_t total_required = wrap_pad_bytes + frame_bytes;
        if (!has_free_bytes(w, total_required)) [[unlikely]] {
            if (record_drop_on_full) {
                m_dropped_packets.store(
                    m_dropped_packets.load(std::memory_order_relaxed) + 1,
                    std::memory_order_relaxed);
            }
            return std::nullopt;
        }

        std::size_t write_pos = phys_w;
        if (needs_wrap_pad) {
            const CaptureFrameHeader pad_hdr{
                .rx_timestamp_ns = rx_timestamp_ns,
                .channel_id      = tail_room,
                .payload_len     = kWrapPaddingSentinel,
                .flags           = kFlagNone
            };
            std::memcpy(m_storage.data() + phys_w, &pad_hdr, kHeaderSize);
            write_pos = 0;
        }

        const CaptureFrameHeader hdr{
            .rx_timestamp_ns = rx_timestamp_ns,
            .channel_id      = channel_id,
            .payload_len     = max_payload_len,
            .flags           = flags
        };
        std::memcpy(m_storage.data() + write_pos, &hdr, kHeaderSize);

        return WriteClaim{
            .payload_span        = std::span<std::byte>(m_storage.data() + write_pos + kHeaderSize, max_payload_len),
            .base_write_offset   = w,
            .header_phys_pos     = write_pos,
            .wrap_pad_bytes      = wrap_pad_bytes,
            .claimed_payload_len = max_payload_len,
            .active              = true
        };
    }

    [[nodiscard]] bool has_free_bytes(std::uint64_t w, std::size_t required) noexcept {
        if ((w - m_read_offset_cache) + required > CapacityBytes) [[unlikely]] {
            m_read_offset_cache = m_read_offset.load(std::memory_order_acquire);
            if ((w - m_read_offset_cache) + required > CapacityBytes) [[unlikely]] {
                return false;
            }
        }
        return true;
    }

    alignas(kCaptureCacheLine) std::atomic<std::uint64_t> m_write_offset{0};
    std::uint64_t m_read_offset_cache{0};
    std::atomic<std::uint64_t> m_dropped_packets{0};
    std::atomic<std::uint64_t> m_oversized_drops{0};

    alignas(kCaptureCacheLine) std::atomic<std::uint64_t> m_read_offset{0};
    std::uint64_t m_write_offset_cache{0};

    alignas(kCaptureCacheLine) std::array<std::byte, CapacityBytes> m_storage{};
};

// ============================================================================
// 2. Zero-Copy Raw Chunk Frame Walker (Used by Spooler, Splitter & Compressor)
// ============================================================================
enum class WalkStatus : std::uint8_t {
    Complete       = 0, // Reached end of buffer or wrap-padding sentinel cleanly
    TruncatedFrame = 1, // Trailing bytes hold an incomplete header or payload
    CorruptHeader  = 2  // Invalid payload_len (> kMaxWirePayloadLimit and != sentinel)
};

struct WalkResult {
    std::size_t frames_visited{0};
    std::size_t bytes_consumed{0};
    WalkStatus  status{WalkStatus::Complete};
};

// Walks a raw byte buffer containing captured frames (automatically skipping any
// tail wrap-padding sentinels) and invokes `callback(hdr, payload_span)`.
template <typename Callback>
    requires std::invocable<Callback&, const CaptureFrameHeader&, std::span<const std::byte>>
[[nodiscard]] WalkResult walk_chunk_frames(
    std::span<const std::byte> raw_bytes,
    Callback&& callback) noexcept(
        std::is_nothrow_invocable_v<Callback&, const CaptureFrameHeader&, std::span<const std::byte>>)
{
    std::size_t offset = 0;
    std::size_t packets_visited = 0;

    while (offset < raw_bytes.size()) {
        const std::size_t remaining = raw_bytes.size() - offset;
        if (remaining < sizeof(CaptureFrameHeader)) {
            return WalkResult{packets_visited, offset, WalkStatus::TruncatedFrame};
        }

        CaptureFrameHeader hdr{};
        std::memcpy(&hdr, raw_bytes.data() + offset, sizeof(CaptureFrameHeader));

        if (hdr.payload_len == kWrapPaddingSentinel) {
            // A wrap sentinel marks the unused tail (`hdr.channel_id` bytes) of a physical ring slab.
            const std::size_t pad_bytes =
                (hdr.channel_id >= sizeof(CaptureFrameHeader))
                    ? static_cast<std::size_t>(hdr.channel_id)
                    : remaining;
            if (remaining < pad_bytes) {
                return WalkResult{packets_visited, offset, WalkStatus::TruncatedFrame};
            }
            offset += pad_bytes;
            return WalkResult{packets_visited, offset, WalkStatus::Complete};
        }

        if (hdr.payload_len > kMaxWirePayloadLimit) [[unlikely]] {
            return WalkResult{packets_visited, offset, WalkStatus::CorruptHeader};
        }

        const std::size_t aligned_payload = align_up_8(static_cast<std::size_t>(hdr.payload_len));
        if (aligned_payload > remaining - sizeof(CaptureFrameHeader)) {
            return WalkResult{packets_visited, offset, WalkStatus::TruncatedFrame};
        }

        const std::size_t frame_total = sizeof(CaptureFrameHeader) + aligned_payload;
        const std::span<const std::byte> payload(
            raw_bytes.data() + offset + sizeof(CaptureFrameHeader),
            hdr.payload_len);
        callback(hdr, payload);
        ++packets_visited;
        offset += frame_total;
    }

    return WalkResult{packets_visited, offset, WalkStatus::Complete};
}

// ============================================================================
// 3. Raw Chunk Spooler + Vyukov MPMC Fan-Out + Ref-Counted `.raw` Unlink
// ============================================================================
struct ClosedChunkDescriptor {
    std::uint64_t chunk_id{0};        // Monotonic chunk sequence (e.g., chunk_0001.raw)
    std::size_t   raw_byte_size{0};   // Total bytes written to the closed raw chunk
    std::size_t   packet_count{0};    // Valid market-data frames inside the chunk
    std::uint64_t first_rx_ts_ns{0};  // Earliest packet HW timestamp in chunk
    std::uint64_t last_rx_ts_ns{0};   // Latest packet HW timestamp in chunk
};

enum class ChunkConsumer : std::uint8_t {
    CompressionPool = 1u << 0, // ZSTD compression + channel splitter worker pool
    AsyncRelay      = 1u << 1  // librdkafka / WAN relay dispatcher
};

template <std::size_t MaxQueuedChunks = 64>
    requires(MaxQueuedChunks >= 2 && ((MaxQueuedChunks & (MaxQueuedChunks - 1)) == 0))
class RawChunkRotationCoordinator {
public:
    static constexpr std::size_t kQueueMask = MaxQueuedChunks - 1;
    static constexpr std::uint8_t kBothConsumersMask =
        static_cast<std::uint8_t>(ChunkConsumer::CompressionPool) |
        static_cast<std::uint8_t>(ChunkConsumer::AsyncRelay);

    explicit RawChunkRotationCoordinator(std::size_t rotate_threshold_bytes = 64 * 1024) noexcept
        : m_rotate_threshold_bytes(rotate_threshold_bytes > 0 ? rotate_threshold_bytes : 1) {}

    // Called on the Raw Spooler Core (Core 4):
    // Coalesces consecutive valid frames from `ring` into single sequential batch
    // spans (`chunk_sink(chunk_id, contiguous_batch_span)`) up to `m_rotate_threshold_bytes`,
    // advances `ring` immediately after each committed batch, and rotates the active
    // chunk when the byte threshold is reached.
    //
    // Backpressure safety: if either downstream descriptor queue is full (`MaxQueuedChunks`
    // unacknowledged chunks in flight), pauses draining before rotating so zero chunk
    // descriptors are ever silently dropped and the capture ring absorbs the burst.
    template <std::size_t RingCap, typename ChunkWriteSink, typename ChunkCloseHook = void(*)(const ClosedChunkDescriptor&) noexcept>
        requires std::invocable<ChunkWriteSink&, std::uint64_t, std::span<const std::byte>> &&
                 std::invocable<ChunkCloseHook&, const ClosedChunkDescriptor&>
    std::size_t drain_and_spool(
        VariableByteRingBuffer<RingCap>& ring,
        ChunkWriteSink&& chunk_sink,
        std::size_t max_frames_budget = std::numeric_limits<std::size_t>::max(),
        ChunkCloseHook&& on_chunk_closed = +[](const ClosedChunkDescriptor&) noexcept {}) noexcept(
            std::is_nothrow_invocable_v<ChunkWriteSink&, std::uint64_t, std::span<const std::byte>> &&
            std::is_nothrow_invocable_v<ChunkCloseHook&, const ClosedChunkDescriptor&>)
    {
        std::size_t frames_spooled = 0;

        while (frames_spooled < max_frames_budget) {
            if (m_rotation_blocked) {
                if (!try_rotate_active_chunk(on_chunk_closed)) {
                    break;
                }
            }

            const auto slab = ring.peek_contiguous_slab();
            if (slab.empty()) {
                break;
            }

            std::size_t slab_cursor = 0;
            bool stop_draining = false;

            while (slab_cursor < slab.size() && frames_spooled < max_frames_budget) {
                const std::size_t remaining = slab.size() - slab_cursor;
                if (remaining < sizeof(CaptureFrameHeader)) {
                    stop_draining = true;
                    break;
                }

                CaptureFrameHeader hdr{};
                std::memcpy(&hdr, slab.data() + slab_cursor, sizeof(CaptureFrameHeader));

                if (hdr.payload_len == kWrapPaddingSentinel) {
                    ring.advance_read(remaining);
                    slab_cursor = slab.size();
                    break;
                }

                if (hdr.payload_len > kMaxWirePayloadLimit) [[unlikely]] {
                    stop_draining = true;
                    break;
                }

                const std::size_t aligned_payload = align_up_8(static_cast<std::size_t>(hdr.payload_len));
                if (aligned_payload > remaining - sizeof(CaptureFrameHeader)) {
                    stop_draining = true;
                    break;
                }
                const std::size_t first_frame_bytes = sizeof(CaptureFrameHeader) + aligned_payload;

                if (m_active_bytes > 0 && (m_active_bytes + first_frame_bytes > m_rotate_threshold_bytes)) {
                    if (!try_rotate_active_chunk(on_chunk_closed)) {
                        stop_draining = true;
                        break;
                    }
                }

                const std::size_t batch_start = slab_cursor;
                std::size_t batch_bytes       = first_frame_bytes;
                std::size_t batch_packets     = 1;
                const std::uint64_t batch_first_ts = hdr.rx_timestamp_ns;
                std::uint64_t batch_last_ts        = hdr.rx_timestamp_ns;

                while ((frames_spooled + batch_packets) < max_frames_budget &&
                       (m_active_bytes + batch_bytes) < m_rotate_threshold_bytes)
                {
                    const std::size_t next_pos = batch_start + batch_bytes;
                    const std::size_t rem_after = slab.size() - next_pos;
                    if (rem_after < sizeof(CaptureFrameHeader)) {
                        break;
                    }

                    CaptureFrameHeader next_hdr{};
                    std::memcpy(&next_hdr, slab.data() + next_pos, sizeof(CaptureFrameHeader));
                    if (next_hdr.payload_len == kWrapPaddingSentinel ||
                        next_hdr.payload_len > kMaxWirePayloadLimit)
                    {
                        break;
                    }

                    const std::size_t next_aligned = align_up_8(static_cast<std::size_t>(next_hdr.payload_len));
                    if (next_aligned > rem_after - sizeof(CaptureFrameHeader)) {
                        break;
                    }

                    const std::size_t next_frame_bytes = sizeof(CaptureFrameHeader) + next_aligned;
                    if (m_active_bytes + batch_bytes + next_frame_bytes > m_rotate_threshold_bytes) {
                        break;
                    }

                    batch_bytes += next_frame_bytes;
                    ++batch_packets;
                    batch_last_ts = next_hdr.rx_timestamp_ns;
                }

                const std::span<const std::byte> batch_span(slab.data() + batch_start, batch_bytes);
                if (!invoke_sink(chunk_sink, m_active_chunk_id, batch_span)) [[unlikely]] {
                    stop_draining = true;
                    break;
                }

                if (m_active_packets == 0) {
                    m_first_ts_ns = batch_first_ts;
                }
                m_last_ts_ns      = batch_last_ts;
                m_active_bytes   += batch_bytes;
                m_active_packets += batch_packets;
                frames_spooled   += batch_packets;
                slab_cursor      += batch_bytes;

                ring.advance_read(batch_bytes);

                if (m_active_bytes >= m_rotate_threshold_bytes) {
                    if (!try_rotate_active_chunk(on_chunk_closed)) {
                        stop_draining = true;
                        break;
                    }
                }
            }

            if (stop_draining) {
                break;
            }
        }

        return frames_spooled;
    }

    // Force-close the current active chunk (e.g., on end-of-day or timer flush)
    template <typename ChunkCloseHook = void(*)(const ClosedChunkDescriptor&) noexcept>
        requires std::invocable<ChunkCloseHook&, const ClosedChunkDescriptor&>
    bool flush_active_chunk(
        ChunkCloseHook&& on_chunk_closed = +[](const ClosedChunkDescriptor&) noexcept {}) noexcept(
            std::is_nothrow_invocable_v<ChunkCloseHook&, const ClosedChunkDescriptor&>)
    {
        if (m_active_packets == 0 && !m_rotation_blocked) {
            return false;
        }
        return try_rotate_active_chunk(on_chunk_closed);
    }

    [[nodiscard]] bool has_pending_active_chunk() const noexcept {
        return m_active_packets > 0 || m_rotation_blocked;
    }

    // Multi-Consumer safe (Vyukov MPMC): multiple ZSTD worker threads can pop concurrently!
    [[nodiscard]] std::optional<ClosedChunkDescriptor> pop_for_compression() noexcept {
        return m_compression_queue.try_pop();
    }

    // Multi-Consumer safe (Vyukov MPMC): async relay thread(s) pop independently!
    [[nodiscard]] std::optional<ClosedChunkDescriptor> pop_for_async_relay() noexcept {
        return m_relay_queue.try_pop();
    }

    // Reference-counted raw file retention:
    // Both `ChunkConsumer::CompressionPool` and `ChunkConsumer::AsyncRelay` call
    // `complete_chunk(chunk_id, role, unlink_fn)` when finished reading `chunk_<id>.raw`.
    // Uses a single packed 64-bit `(chunk_id << 2) | mask` atomic CAS so generation
    // validation and bit updates are indivisible, and `unlink_fn(chunk_id)` finishes
    // BEFORE the slot is released for reuse or `in_flight_chunks()` is decremented.
    template <typename UnlinkCallback>
        requires std::invocable<UnlinkCallback&, std::uint64_t>
    bool complete_chunk(
        std::uint64_t chunk_id,
        ChunkConsumer role,
        UnlinkCallback&& unlink_fn) noexcept(std::is_nothrow_invocable_v<UnlinkCallback&, std::uint64_t>)
    {
        if (chunk_id == 0) [[unlikely]] {
            return false;
        }

        TrackerSlot& slot = m_completion_tracker[static_cast<std::size_t>(chunk_id) & kQueueMask];
        const auto bit = static_cast<std::uint64_t>(role);
        std::uint64_t cur = slot.state.load(std::memory_order_acquire);

        for (;;) {
            if ((cur >> 2) != chunk_id) [[unlikely]] {
                return false; // Slot unoccupied or holds a different generation
            }
            if ((cur & bit) != 0) [[unlikely]] {
                return false; // Duplicate completion for same role ignored
            }

            const std::uint64_t next = cur | bit;
            if (slot.state.compare_exchange_weak(
                    cur, next, std::memory_order_acq_rel, std::memory_order_acquire))
            {
                if ((next & kBothConsumersMask) == kBothConsumersMask) {
                    unlink_fn(chunk_id);
                    slot.state.store(0, std::memory_order_release);
                    m_in_flight_chunks.fetch_sub(1, std::memory_order_release);
                    return true;
                }
                return false;
            }
        }
    }

    [[nodiscard]] std::uint64_t total_chunks_rotated() const noexcept {
        return m_chunks_rotated.load(std::memory_order_acquire);
    }

    [[nodiscard]] std::size_t in_flight_chunks() const noexcept {
        return m_in_flight_chunks.load(std::memory_order_acquire);
    }

    [[nodiscard]] std::uint64_t queue_backpressure_events() const noexcept {
        return m_backpressure_events.load(std::memory_order_relaxed);
    }

private:
    struct VyukovDescriptorRing {
        VyukovDescriptorRing() noexcept {
            for (std::size_t i = 0; i < MaxQueuedChunks; ++i) {
                slots[i].seq.store(i, std::memory_order_relaxed);
            }
        }

        [[nodiscard]] bool can_push() const noexcept {
            const std::size_t pos = write_idx.load(std::memory_order_relaxed);
            const std::size_t seq = slots[pos & kQueueMask].seq.load(std::memory_order_acquire);
            return seq == pos;
        }

        [[nodiscard]] bool try_push(const ClosedChunkDescriptor& desc) noexcept {
            std::size_t pos = write_idx.load(std::memory_order_relaxed);
            for (;;) {
                Slot& slot = slots[pos & kQueueMask];
                const std::size_t seq = slot.seq.load(std::memory_order_acquire);
                const auto diff = static_cast<std::intptr_t>(seq - pos);
                if (diff == 0) {
                    if (write_idx.compare_exchange_weak(
                            pos, pos + 1, std::memory_order_relaxed, std::memory_order_relaxed))
                    {
                        slot.desc = desc;
                        slot.seq.store(pos + 1, std::memory_order_release);
                        return true;
                    }
                } else if (diff < 0) {
                    return false;
                } else {
                    pos = write_idx.load(std::memory_order_relaxed);
                }
            }
        }

        [[nodiscard]] std::optional<ClosedChunkDescriptor> try_pop() noexcept {
            std::size_t pos = read_idx.load(std::memory_order_relaxed);
            for (;;) {
                Slot& slot = slots[pos & kQueueMask];
                const std::size_t seq = slot.seq.load(std::memory_order_acquire);
                const auto diff = static_cast<std::intptr_t>(seq - (pos + 1));
                if (diff == 0) {
                    if (read_idx.compare_exchange_weak(
                            pos, pos + 1, std::memory_order_relaxed, std::memory_order_relaxed))
                    {
                        const ClosedChunkDescriptor out = slot.desc;
                        slot.seq.store(pos + MaxQueuedChunks, std::memory_order_release);
                        return out;
                    }
                } else if (diff < 0) {
                    return std::nullopt;
                } else {
                    pos = read_idx.load(std::memory_order_relaxed);
                }
            }
        }

        struct alignas(kCaptureCacheLine) Slot {
            std::atomic<std::size_t> seq{0};
            ClosedChunkDescriptor desc{};
        };

        alignas(kCaptureCacheLine) std::atomic<std::size_t> write_idx{0};
        alignas(kCaptureCacheLine) std::atomic<std::size_t> read_idx{0};
        alignas(kCaptureCacheLine) std::array<Slot, MaxQueuedChunks> slots{};
    };

    // Packed 64-bit state: `(chunk_id << 2) | consumer_mask` (0 == unoccupied)
    struct alignas(kCaptureCacheLine) TrackerSlot {
        std::atomic<std::uint64_t> state{0};
    };

    template <typename Sink>
    static bool invoke_sink(
        Sink& sink,
        std::uint64_t chunk_id,
        std::span<const std::byte> batch) noexcept(
            std::is_nothrow_invocable_v<Sink&, std::uint64_t, std::span<const std::byte>>)
    {
        using RetT = std::invoke_result_t<Sink&, std::uint64_t, std::span<const std::byte>>;
        static_assert(
            std::is_same_v<RetT, void> || std::is_same_v<RetT, bool>,
            "ChunkWriteSink must return void or bool");
        if constexpr (std::is_same_v<RetT, bool>) {
            return sink(chunk_id, batch);
        } else {
            sink(chunk_id, batch);
            return true;
        }
    }

    template <typename ChunkCloseHook>
    static bool invoke_close_hook(
        ChunkCloseHook& on_chunk_closed,
        const ClosedChunkDescriptor& desc) noexcept(
            std::is_nothrow_invocable_v<ChunkCloseHook&, const ClosedChunkDescriptor&>)
    {
        using RetT = std::invoke_result_t<ChunkCloseHook&, const ClosedChunkDescriptor&>;
        static_assert(
            std::is_same_v<RetT, void> || std::is_same_v<RetT, bool>,
            "ChunkCloseHook must return void or bool");
        if constexpr (std::is_same_v<RetT, bool>) {
            return on_chunk_closed(desc);
        } else {
            on_chunk_closed(desc);
            return true;
        }
    }

    template <typename ChunkCloseHook>
    bool try_rotate_active_chunk(ChunkCloseHook& on_chunk_closed) noexcept(
        std::is_nothrow_invocable_v<ChunkCloseHook&, const ClosedChunkDescriptor&>)
    {
        TrackerSlot& tracker = m_completion_tracker[static_cast<std::size_t>(m_active_chunk_id) & kQueueMask];
        if (tracker.state.load(std::memory_order_acquire) != 0 ||
            !m_compression_queue.can_push() ||
            !m_relay_queue.can_push()) [[unlikely]]
        {
            if (!m_rotation_blocked) {
                m_rotation_blocked = true;
                m_backpressure_events.store(
                    m_backpressure_events.load(std::memory_order_relaxed) + 1,
                    std::memory_order_relaxed);
            }
            return false;
        }

        const ClosedChunkDescriptor desc{
            .chunk_id       = m_active_chunk_id,
            .raw_byte_size  = m_active_bytes,
            .packet_count   = m_active_packets,
            .first_rx_ts_ns = m_first_ts_ns,
            .last_rx_ts_ns  = m_last_ts_ns
        };

        if (!m_close_hook_called) {
            if (!invoke_close_hook(on_chunk_closed, desc)) [[unlikely]] {
                m_rotation_blocked = true;
                return false;
            }
            m_close_hook_called = true;
        }

        tracker.state.store(desc.chunk_id << 2, std::memory_order_release);
        m_in_flight_chunks.fetch_add(1, std::memory_order_release);

        const bool pushed_comp  = m_compression_queue.try_push(desc);
        const bool pushed_relay = m_relay_queue.try_push(desc);
        assert(pushed_comp && pushed_relay);
        (void)pushed_comp;
        (void)pushed_relay;

        m_chunks_rotated.store(m_active_chunk_id, std::memory_order_release);
        ++m_active_chunk_id;
        m_active_bytes      = 0;
        m_active_packets    = 0;
        m_first_ts_ns       = 0;
        m_last_ts_ns        = 0;
        m_rotation_blocked  = false;
        m_close_hook_called = false;
        return true;
    }

    std::size_t   m_rotate_threshold_bytes;
    std::uint64_t m_active_chunk_id{1};
    std::size_t   m_active_bytes{0};
    std::size_t   m_active_packets{0};
    std::uint64_t m_first_ts_ns{0};
    std::uint64_t m_last_ts_ns{0};
    bool          m_rotation_blocked{false};
    bool          m_close_hook_called{false};

    alignas(kCaptureCacheLine) std::atomic<std::uint64_t> m_chunks_rotated{0};
    std::atomic<std::size_t>   m_in_flight_chunks{0};
    std::atomic<std::uint64_t> m_backpressure_events{0};

    VyukovDescriptorRing m_compression_queue{};
    VyukovDescriptorRing m_relay_queue{};
    alignas(kCaptureCacheLine) std::array<TrackerSlot, MaxQueuedChunks> m_completion_tracker{};
};

} // namespace hft

#endif // HFT_Q21_UDP_FEED_CAPTURE_SPOOLER_HPP
