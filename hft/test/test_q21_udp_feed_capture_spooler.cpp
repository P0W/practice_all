#include "../src/q21_udp_feed_capture_spooler.hpp"
#include <atomic>
#include <cstdlib>
#include <iostream>
#include <map>
#include <mutex>
#include <thread>
#include <vector>

#define HFT_VERIFY(expr) \
    do { \
        if (!(expr)) { \
            std::cerr << "[FAIL] " << __FILE__ << ":" << __LINE__ << " -> " #expr "\n"; \
            std::abort(); \
        } \
    } while (false)

int main() {
    // 1. Variable-length frame wraparound sentinel, boundary checks, and two-phase claim/commit
    {
        hft::VariableByteRingBuffer<256> ring;
        ring.prefault_pages();

        // Oversized payload (> kMaxPayloadBytes = 80 for 256B ring) is rejected & counted
        const std::array<std::byte, 81> oversized{};
        HFT_VERIFY(!ring.try_write_packet(500ULL, 1ULL, oversized));
        HFT_VERIFY(ring.oversized_drops() == 1);

        // pkt1: 60B payload -> 24 + 64 = 88B frame [0..88)
        // pkt2: 75B payload -> 24 + 80 = 104B frame [88..192)
        const std::array<std::byte, 60> pkt1{std::byte{0x11}, std::byte{0x22}};
        const std::array<std::byte, 75> pkt2{std::byte{0x33}, std::byte{0x44}};
        const std::array<std::byte, 64> pkt3{std::byte{0x55}, std::byte{0x66}};
        const std::array<std::byte, 65> pkt_too_big_for_wrap{};

        HFT_VERIFY(ring.try_write_packet(1000ULL, 0xA1ULL, pkt1));
        HFT_VERIFY(ring.try_write_packet(2000ULL, 0xB2ULL, pkt2));

        // Drain pkt1 (88B): read offset is now 88, write offset is 192 (104B unread, 152B free).
        auto first_slab = ring.peek_contiguous_slab(88);
        HFT_VERIFY(first_slab.size() == 88);
        const auto res1 = hft::walk_chunk_frames(
            first_slab,
            [&](const hft::CaptureFrameHeader& hdr, std::span<const std::byte> payload) noexcept {
                HFT_VERIFY(hdr.rx_timestamp_ns == 1000ULL);
                HFT_VERIFY(hdr.channel_id == 0xA1ULL);
                HFT_VERIFY(payload.size() == 60);
                HFT_VERIFY(payload[0] == std::byte{0x11});
            });
        HFT_VERIFY(res1.frames_visited == 1 && res1.bytes_consumed == 88 &&
                   res1.status == hft::WalkStatus::Complete);
        HFT_VERIFY(ring.advance_read(res1.bytes_consumed));

        // 65B payload needs 24 + 72 = 96B frame + 64B tail wrap pad = 160B > 152B free -> must reject!
        HFT_VERIFY(!ring.try_write_packet(2500ULL, 0xFFULL, pkt_too_big_for_wrap));
        HFT_VERIFY(ring.dropped_packets() == 1);

        // 64B payload needs 24 + 64 = 88B frame + 64B tail wrap pad = 152B == 152B free -> fits exactly!
        HFT_VERIFY(ring.try_write_packet(3000ULL, 0xC3ULL, pkt3));
        HFT_VERIFY(ring.readable_bytes_approx() == 256);

        std::vector<std::uint64_t> timestamps;
        while (ring.readable_bytes_approx() > 0) {
            auto slab = ring.peek_contiguous_slab();
            HFT_VERIFY(!slab.empty());
            const auto walk = hft::walk_chunk_frames(
                slab,
                [&](const hft::CaptureFrameHeader& hdr, std::span<const std::byte> payload) {
                    timestamps.push_back(hdr.rx_timestamp_ns);
                    if (hdr.rx_timestamp_ns == 2000ULL) {
                        HFT_VERIFY(payload.size() == 75 && payload[0] == std::byte{0x33});
                    } else if (hdr.rx_timestamp_ns == 3000ULL) {
                        HFT_VERIFY(payload.size() == 64 && payload[0] == std::byte{0x55});
                    }
                });
            HFT_VERIFY(walk.status == hft::WalkStatus::Complete);
            HFT_VERIFY(ring.advance_read(walk.bytes_consumed));
        }
        HFT_VERIFY((timestamps == std::vector<std::uint64_t>{2000ULL, 3000ULL}));

        // Test two-phase claim_frame(max_mtu) + commit_frame(claim, actual_smaller_len)
        // and zero-length payload frame
        auto claim = ring.claim_frame(4000ULL, 0xD4ULL, /*max_payload_len=*/64);
        HFT_VERIFY(claim.has_value());
        claim->payload_span[0] = std::byte{0x7F};
        HFT_VERIFY(ring.commit_frame(*claim, /*actual_payload_len=*/1));
        HFT_VERIFY(!ring.commit_frame(*claim, 1)); // Double commit rejected

        HFT_VERIFY(ring.try_write_packet(5000ULL, 0xE5ULL, std::span<const std::byte>{})); // 0-byte payload
        auto slab2 = ring.peek_contiguous_slab();
        std::vector<std::size_t> sizes;
        const auto walk2 = hft::walk_chunk_frames(
            slab2,
            [&](const hft::CaptureFrameHeader&, std::span<const std::byte> payload) {
                sizes.push_back(payload.size());
            });
        HFT_VERIFY(walk2.frames_visited == 2 && walk2.status == hft::WalkStatus::Complete);
        HFT_VERIFY((sizes == std::vector<std::size_t>{1, 0}));
        HFT_VERIFY(!ring.advance_read(std::numeric_limits<std::size_t>::max())); // Overflow-safe rejection
        HFT_VERIFY(ring.advance_read(walk2.bytes_consumed));
    }

    // 2. Batched Slab Coalescing, Corrupt/Truncated Frame Detection & Deterministic Backpressure
    {
        hft::VariableByteRingBuffer<1024> ring;
        const std::array<std::byte, 16> p{};
        for (std::uint64_t i = 1; i <= 5; ++i) {
            HFT_VERIFY(ring.try_write_packet(i * 100, i, p)); // 5 * 40B = 200B
        }

        // With rotate_threshold_bytes = 512, all 5 contiguous frames (200B) must be
        // coalesced into a SINGLE sink write invocation!
        hft::RawChunkRotationCoordinator<2> spooler(512);
        std::size_t sink_calls = 0;
        std::size_t spooled = spooler.drain_and_spool(
            ring,
            [&](std::uint64_t, std::span<const std::byte> batch) noexcept {
                ++sink_calls;
                HFT_VERIFY(batch.size() == 200);
            });
        HFT_VERIFY(spooled == 5 && sink_calls == 1);
        HFT_VERIFY(spooler.has_pending_active_chunk());
        HFT_VERIFY(spooler.flush_active_chunk()); // Chunk 1 published

        // Deterministic backpressure & packed (chunk_id, mask) generation safety:
        // Queue capacity is 2. Publish Chunk 2, then attempt to flush Chunk 3 while
        // Chunks 1 and 2 are still unacknowledged -> must block on backpressure!
        HFT_VERIFY(ring.try_write_packet(600, 1, p));
        HFT_VERIFY(spooler.drain_and_spool(ring, [](std::uint64_t, std::span<const std::byte>) noexcept {}) == 1);
        HFT_VERIFY(spooler.flush_active_chunk()); // Chunk 2 published (in_flight == 2)
        HFT_VERIFY(spooler.in_flight_chunks() == 2);

        HFT_VERIFY(ring.try_write_packet(700, 1, p));
        HFT_VERIFY(spooler.drain_and_spool(ring, [](std::uint64_t, std::span<const std::byte>) noexcept {}) == 1);
        HFT_VERIFY(!spooler.flush_active_chunk()); // Blocked: slot (3 & 1) held by Chunk 1!
        HFT_VERIFY(spooler.queue_backpressure_events() == 1);

        // Complete Chunk 1 across both consumers; verify duplicate and stale-generation calls fail
        std::size_t unlinks = 0;
        auto d1_c = spooler.pop_for_compression();
        auto d1_r = spooler.pop_for_async_relay();
        HFT_VERIFY(d1_c.has_value() && d1_r.has_value() && d1_c->chunk_id == 1);
        HFT_VERIFY(!spooler.complete_chunk(1, hft::ChunkConsumer::CompressionPool, [&](std::uint64_t) { ++unlinks; }));
        HFT_VERIFY(!spooler.complete_chunk(1, hft::ChunkConsumer::CompressionPool, [&](std::uint64_t) { ++unlinks; })); // Duplicate role rejected
        HFT_VERIFY(unlinks == 0);
        HFT_VERIFY(spooler.complete_chunk(1, hft::ChunkConsumer::AsyncRelay, [&](std::uint64_t) { ++unlinks; }));
        HFT_VERIFY(unlinks == 1);

        // Now Chunk 3 can rotate into slot (3 & 1), and a late duplicate completion for Chunk 1
        // must NOT corrupt Chunk 3's completion mask!
        HFT_VERIFY(spooler.flush_active_chunk());
        HFT_VERIFY(!spooler.complete_chunk(1, hft::ChunkConsumer::AsyncRelay, [&](std::uint64_t) { ++unlinks; }));
        HFT_VERIFY(unlinks == 1);

        // Verify WalkStatus::TruncatedFrame and WalkStatus::CorruptHeader detection
        std::array<std::byte, 12> short_hdr{};
        const auto w_trunc_hdr = hft::walk_chunk_frames(short_hdr, [](const auto&, auto) noexcept {});
        HFT_VERIFY(w_trunc_hdr.status == hft::WalkStatus::TruncatedFrame);

        std::array<std::byte, 32> trunc_payload{};
        hft::CaptureFrameHeader bad_hdr{
            .rx_timestamp_ns = 1,
            .channel_id      = 1,
            .payload_len     = 64, // claims 64B payload in a 32B buffer
            .flags           = 0
        };
        std::memcpy(trunc_payload.data(), &bad_hdr, sizeof(bad_hdr));
        const auto w_trunc_pay = hft::walk_chunk_frames(trunc_payload, [](const auto&, auto) noexcept {});
        HFT_VERIFY(w_trunc_pay.status == hft::WalkStatus::TruncatedFrame);

        bad_hdr.payload_len = hft::kMaxWirePayloadLimit + 1;
        std::memcpy(trunc_payload.data(), &bad_hdr, sizeof(bad_hdr));
        const auto w_corrupt = hft::walk_chunk_frames(trunc_payload, [](const auto&, auto) noexcept {});
        HFT_VERIFY(w_corrupt.status == hft::WalkStatus::CorruptHeader);
    }

    // 3. End-to-End Multi-Threaded Pipeline with Small Ring Wrap, Multi-Worker ZSTD Pool,
    //    Async Relay, Queue Backpressure, and Reference-Counted `.raw` Unlink
    {
        // 512-byte ring forces frequent physical wraparounds and backpressure under load
        hft::VariableByteRingBuffer<512> capture_ring;
        capture_ring.prefault_pages();

        // Small descriptor queue (8 slots) + 192-byte chunk threshold -> >70 chunks rotated,
        // exercising descriptor queue backpressure and reference-counted completion!
        hft::RawChunkRotationCoordinator<8> spooler(/*rotate_threshold_bytes=*/192);

        struct RawChunkFile {
            std::vector<std::byte> bytes{};
            bool closed{false};
        };
        std::mutex store_mtx;
        std::map<std::uint64_t, RawChunkFile> raw_chunk_store;

        constexpr std::uint64_t kTotalPackets = 320;
        std::atomic<bool> producer_done{false};
        std::atomic<bool> spooler_done{false};

        // Stage 1: Fast Capture Thread (variable payload sizes 8..72 bytes)
        std::thread fast_capture_thread([&] {
            for (std::uint64_t seq = 1; seq <= kTotalPackets; ++seq) {
                const std::size_t len = 8 + static_cast<std::size_t>((seq * 13) % 65);
                std::array<std::byte, 72> wire_pkt{};
                std::memcpy(wire_pkt.data(), &seq, sizeof(seq));
                while (!capture_ring.try_write_packet(
                    /*rx_timestamp_ns=*/1'700'000'000'000ULL + seq * 100,
                    /*channel_id=*/(seq % 4) + 1,
                    std::span<const std::byte>(wire_pkt.data(), len),
                    hft::kFlagHwTimestamp | hft::kFlagKernelBypass,
                    /*record_drop_on_full=*/false))
                {
                    std::this_thread::yield();
                }
            }
            producer_done.store(true, std::memory_order_release);
        });

        // Stage 2: Raw Spooler Thread (with on_chunk_closed hook before publishing descriptors)
        std::thread spooler_thread([&] {
            auto sink = [&](std::uint64_t chunk_id, std::span<const std::byte> batch) {
                std::lock_guard<std::mutex> lk(store_mtx);
                auto& file = raw_chunk_store[chunk_id];
                HFT_VERIFY(!file.closed);
                file.bytes.insert(file.bytes.end(), batch.begin(), batch.end());
            };
            auto on_close = [&](const hft::ClosedChunkDescriptor& desc) noexcept {
                std::lock_guard<std::mutex> lk(store_mtx);
                auto& file = raw_chunk_store[desc.chunk_id];
                HFT_VERIFY(file.bytes.size() == desc.raw_byte_size);
                file.closed = true;
            };

            while (!producer_done.load(std::memory_order_acquire) ||
                   capture_ring.readable_bytes_approx() > 0)
            {
                (void)spooler.drain_and_spool(capture_ring, sink, /*max_frames_budget=*/16, on_close);
                std::this_thread::yield();
            }
            while (spooler.has_pending_active_chunk()) {
                if (!spooler.flush_active_chunk(on_close)) {
                    std::this_thread::yield();
                }
            }
            spooler_done.store(true, std::memory_order_release);
        });

        std::atomic<std::size_t> comp_packets{0};
        std::atomic<std::size_t> relay_packets{0};
        std::atomic<std::size_t> unlinked_files{0};

        auto unlink_raw_chunk = [&](std::uint64_t chunk_id) {
            std::lock_guard<std::mutex> lk(store_mtx);
            const auto erased = raw_chunk_store.erase(chunk_id);
            HFT_VERIFY(erased == 1);
            unlinked_files.fetch_add(1, std::memory_order_relaxed);
        };

        auto process_chunk = [&](hft::ChunkConsumer role, std::atomic<std::size_t>& pkt_counter) {
            for (;;) {
                auto desc = (role == hft::ChunkConsumer::CompressionPool)
                                ? spooler.pop_for_compression()
                                : spooler.pop_for_async_relay();
                if (!desc.has_value()) {
                    if (spooler_done.load(std::memory_order_acquire)) {
                        // Re-check queue once more after observing spooler_done
                        desc = (role == hft::ChunkConsumer::CompressionPool)
                                   ? spooler.pop_for_compression()
                                   : spooler.pop_for_async_relay();
                        if (!desc.has_value()) {
                            break;
                        }
                    } else {
                        std::this_thread::yield();
                        continue;
                    }
                }

                std::vector<std::byte> snapshot;
                {
                    std::lock_guard<std::mutex> lk(store_mtx);
                    const auto& file = raw_chunk_store.at(desc->chunk_id);
                    HFT_VERIFY(file.closed);
                    snapshot = file.bytes;
                }

                const auto walk = hft::walk_chunk_frames(
                    snapshot,
                    [&](const hft::CaptureFrameHeader& hdr, std::span<const std::byte> payload) {
                        HFT_VERIFY(payload.size() >= 8);
                        std::uint64_t seq = 0;
                        std::memcpy(&seq, payload.data(), sizeof(seq));
                        HFT_VERIFY(hdr.channel_id == (seq % 4) + 1);
                        pkt_counter.fetch_add(1, std::memory_order_relaxed);
                    });
                HFT_VERIFY(walk.status == hft::WalkStatus::Complete);
                HFT_VERIFY(walk.frames_visited == desc->packet_count);

                // Reference-counted completion: raw chunk is unlinked only when BOTH
                // CompressionPool and AsyncRelay have completed!
                (void)spooler.complete_chunk(desc->chunk_id, role, unlink_raw_chunk);
            }
        };

        // Stage 3a: Two concurrent ZSTD Compression Worker Threads (MPMC queue)
        std::thread zstd_worker_1([&] { process_chunk(hft::ChunkConsumer::CompressionPool, comp_packets); });
        std::thread zstd_worker_2([&] { process_chunk(hft::ChunkConsumer::CompressionPool, comp_packets); });

        // Stage 3b: Async Relay Thread (simulating librdkafka publisher)
        std::thread relay_worker([&] { process_chunk(hft::ChunkConsumer::AsyncRelay, relay_packets); });

        fast_capture_thread.join();
        spooler_thread.join();
        zstd_worker_1.join();
        zstd_worker_2.join();
        relay_worker.join();

        HFT_VERIFY(comp_packets.load() == kTotalPackets);
        HFT_VERIFY(relay_packets.load() == kTotalPackets);
        HFT_VERIFY(unlinked_files.load() == spooler.total_chunks_rotated());
        HFT_VERIFY(spooler.in_flight_chunks() == 0);
        HFT_VERIFY(raw_chunk_store.empty());
    }

    std::cout << "[PASS] Q21 Zero-Drop Market Data Capture & Spooler\n";
    return 0;
}
