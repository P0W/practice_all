#include "../src/q18_ab_feed_arbitrator.hpp"
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <vector>

#define HFT_VERIFY(expr) \
    do { \
        if (!(expr)) { \
            std::cerr << "[FAIL] " << __FILE__ << ":" << __LINE__ << " -> " #expr "\n"; \
            std::abort(); \
        } \
    } while (false)

namespace {
// Non-default-constructible packet to verify std::optional<PacketT> slot storage!
struct NonDefaultMdPacket {
    std::uint64_t seq;
    std::int64_t price;
    constexpr NonDefaultMdPacket(std::uint64_t s, std::int64_t p) noexcept : seq(s), price(p) {}
};
} // namespace

int main() {
    hft::AbFeedArbitrator<NonDefaultMdPacket, 8> arb(1);
    std::vector<std::uint64_t> delivered;

    auto on_deliver = [&](const NonDefaultMdPacket& pkt, hft::FeedLine) noexcept {
        delivered.push_back(pkt.seq);
    };

    // 1. Seq 1 on Line A (in-order), Seq 1 on Line B (duplicate ignored)
    HFT_VERIFY(arb.on_packet(1, hft::FeedLine::LineA, NonDefaultMdPacket{1, 100}, on_deliver) ==
               hft::FeedStatus::ProcessedInOrder);
    HFT_VERIFY(arb.on_packet(1, hft::FeedLine::LineB, NonDefaultMdPacket{1, 100}, on_deliver) ==
               hft::FeedStatus::DuplicateIgnored);

    // 2. Out-of-order Seq 3 & 4 arrive on Line B before Seq 2
    HFT_VERIFY(arb.on_packet(3, hft::FeedLine::LineB, NonDefaultMdPacket{3, 103}, on_deliver) ==
               hft::FeedStatus::BufferedOutOfOrder);
    HFT_VERIFY(arb.on_packet(4, hft::FeedLine::LineB, NonDefaultMdPacket{4, 104}, on_deliver) ==
               hft::FeedStatus::BufferedOutOfOrder);
    HFT_VERIFY(arb.buffered_count() == 2);

    // 3. Missing Seq 2 arrives on Line A -> drains 2, 3, 4 in order!
    HFT_VERIFY(arb.on_packet(2, hft::FeedLine::LineA, NonDefaultMdPacket{2, 102}, on_deliver) ==
               hft::FeedStatus::ProcessedInOrder);
    HFT_VERIFY(arb.buffered_count() == 0);
    HFT_VERIFY((delivered == std::vector<std::uint64_t>{1, 2, 3, 4}));

    // 4. Modular Wraparound Test across UINT64_MAX -> 0
    const std::uint64_t near_max = std::numeric_limits<std::uint64_t>::max();
    arb.reset_expected_seq(near_max);
    delivered.clear();

    // Seq 0 (which is near_max + 1 mod 2^64) arrives early before near_max
    HFT_VERIFY(arb.on_packet(0ULL, hft::FeedLine::LineB, NonDefaultMdPacket{0ULL, 200}, on_deliver) ==
               hft::FeedStatus::BufferedOutOfOrder);
    // Old packet (near_max - 1) must be treated as duplicate even near UINT64_MAX
    HFT_VERIFY(arb.on_packet(near_max - 1, hft::FeedLine::LineA, NonDefaultMdPacket{near_max - 1, 199}, on_deliver) ==
               hft::FeedStatus::DuplicateIgnored);
    // Arrival of near_max drains both near_max and wrapped 0ULL!
    HFT_VERIFY(arb.on_packet(near_max, hft::FeedLine::LineA, NonDefaultMdPacket{near_max, 199}, on_deliver) ==
               hft::FeedStatus::ProcessedInOrder);
    HFT_VERIFY((delivered == std::vector<std::uint64_t>{near_max, 0ULL}));

    std::cout << "[PASS] Q18 A/B Redundant Feed Arbitrator\n";
    return 0;
}
