#ifndef HFT_Q18_AB_FEED_ARBITRATOR_HPP
#define HFT_Q18_AB_FEED_ARBITRATOR_HPP

// ============================================================================
// QUESTION 18 [TIER 5 - VERY HARD]: HFT A/B REDUNDANT MULTICAST FEED ARBITRATOR
// ============================================================================
// EXCLUSIVE PROBLEM SCOPE (NON-OVERLAPPING):
//   Single-threaded, deterministic UDP multicast feed arbitrator across two
//   redundant exchange lines (**Line A** and **Line B**), implementing:
//   1. **First-Arrival Dispatch**: Whichever line delivers `expected_seq_`
//      first is immediately forwarded to the strategy callback.
//   2. **Wrap-Safe Modular Sequence Deduplication**: Uses signed 2's-complement
//      distance `static_cast<std::int64_t>(seq - expected_seq_) < 0` so even
//      `uint64_t` rollover is handled cleanly without wraparound bugs!
//   3. **Bounded Out-of-Order Reassembly Ring**: Buffers early future packets
//      (`0 < diff < WindowSize`) in `std::optional<PacketT>` slots (requiring
//      no default constructor for `PacketT` and accurately maintaining
//      `buffered_count()`).
//   4. **Strict Nothrow / Conditional Exception Contract**: Enforces nothrow
//      move/copy and callback invocation when marked `noexcept`.
// ============================================================================

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <type_traits>

namespace hft {

enum class FeedLine : std::uint8_t { LineA = 0, LineB = 1 };

enum class FeedStatus : std::uint8_t {
    ProcessedInOrder,     // Dispatched immediately (plus any contiguous staged packets)
    DuplicateIgnored,     // Already processed or already staged from the faster line
    BufferedOutOfOrder,   // Future sequence staged in reorder window waiting for gap fill
    UnrecoverableGap      // Gap >= WindowSize -> trigger snapshot/TCP replay recovery
};

template <typename PacketT, std::size_t WindowSize = 64>
    requires(WindowSize >= 2 && ((WindowSize & (WindowSize - 1)) == 0) && std::is_copy_constructible_v<PacketT>)
class AbFeedArbitrator {
public:
    static constexpr std::size_t kMask = WindowSize - 1;

    constexpr explicit AbFeedArbitrator(std::uint64_t initial_expected_seq = 1) noexcept
        : expected_seq_(initial_expected_seq) {}

    template <typename Callback>
        requires std::invocable<Callback&, const PacketT&, FeedLine>
    [[nodiscard]] FeedStatus on_packet(
        std::uint64_t seq,
        FeedLine line,
        const PacketT& pkt,
        Callback&& callback) noexcept(std::is_nothrow_copy_constructible_v<PacketT> &&
                                      std::is_nothrow_invocable_v<Callback&, const PacketT&, FeedLine>)
    {
        // Modular signed distance handles uint64_t sequence rollover cleanly
        const auto signed_diff = static_cast<std::int64_t>(seq - expected_seq_);

        // 1. Old/duplicate sequence number (< expected_seq_ in modular space)
        if (signed_diff < 0) {
            ++duplicates_dropped_;
            return FeedStatus::DuplicateIgnored;
        }

        // 2. Exact expected sequence arrived!
        if (signed_diff == 0) [[likely]] {
            record_line_win(line);
            callback(pkt, line);
            ++expected_seq_;

            drain_staged(callback);
            return FeedStatus::ProcessedInOrder;
        }

        // 3. Future sequence: check if within bounded reorder window
        const auto delta = static_cast<std::uint64_t>(signed_diff);
        if (delta >= WindowSize) [[unlikely]] {
            ++gap_alerts_;
            return FeedStatus::UnrecoverableGap;
        }

        Slot& slot = ring_[seq & kMask];
        if (slot.packet.has_value() && slot.seq == seq) {
            ++duplicates_dropped_;
            return FeedStatus::DuplicateIgnored;
        }

        if (!slot.packet.has_value()) {
            ++buffered_count_;
        }
        slot.seq  = seq;
        slot.line = line;
        slot.packet.emplace(pkt);
        return FeedStatus::BufferedOutOfOrder;
    }

    void reset_expected_seq(std::uint64_t new_seq) noexcept {
        expected_seq_ = new_seq;
        buffered_count_ = 0;
        for (auto& s : ring_) {
            s.packet.reset();
        }
    }

    [[nodiscard]] std::uint64_t expected_seq() const noexcept { return expected_seq_; }
    [[nodiscard]] std::uint64_t duplicates_dropped() const noexcept { return duplicates_dropped_; }
    [[nodiscard]] std::size_t buffered_count() const noexcept { return buffered_count_; }
    [[nodiscard]] std::uint64_t line_a_wins() const noexcept { return line_a_wins_; }
    [[nodiscard]] std::uint64_t line_b_wins() const noexcept { return line_b_wins_; }
    [[nodiscard]] std::uint64_t gap_alerts() const noexcept { return gap_alerts_; }

private:
    struct Slot {
        std::uint64_t seq{0};
        FeedLine line{FeedLine::LineA};
        std::optional<PacketT> packet{std::nullopt};
    };

    void record_line_win(FeedLine line) noexcept {
        if (line == FeedLine::LineA) ++line_a_wins_;
        else ++line_b_wins_;
    }

    template <typename Callback>
    void drain_staged(Callback&& callback) noexcept(
        std::is_nothrow_invocable_v<Callback&, const PacketT&, FeedLine>)
    {
        for (;;) {
            Slot& next_slot = ring_[expected_seq_ & kMask];
            if (!next_slot.packet.has_value() || next_slot.seq != expected_seq_) {
                break;
            }
            record_line_win(next_slot.line);
            PacketT staged_pkt = std::move(*next_slot.packet);
            next_slot.packet.reset();
            --buffered_count_;
            ++expected_seq_;
            callback(staged_pkt, next_slot.line);
        }
    }

    std::uint64_t expected_seq_{1};
    std::uint64_t duplicates_dropped_{0};
    std::size_t buffered_count_{0};
    std::uint64_t line_a_wins_{0};
    std::uint64_t line_b_wins_{0};
    std::uint64_t gap_alerts_{0};
    std::array<Slot, WindowSize> ring_{};
};

} // namespace hft

#endif // HFT_Q18_AB_FEED_ARBITRATOR_HPP
