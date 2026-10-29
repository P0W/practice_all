#include "../src/q01_crtp_concepts_dispatch.hpp"
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>

#define HFT_VERIFY(expr) \
    do { \
        if (!(expr)) { \
            std::cerr << "[FAIL] " << __FILE__ << ":" << __LINE__ << " -> " #expr "\n"; \
            std::abort(); \
        } \
    } while (false)

int main() {
    constexpr hft::SpreadSignalStrategy strat(/*max_spread=*/20);

    // 1. Compile-time evaluation via C++20 Concept + CRTP
    constexpr hft::MarketTick tick{1, 1000, 1010, 300, 100};
    constexpr std::int64_t micro_px = hft::dispatch_hot_path_tick(strat, tick);
    static_assert(micro_px == 1007);
    HFT_VERIFY(micro_px == 1007);

    // 2. Overflow Test: bid_qty = UINT32_MAX, ask_qty = 1 (previously wrapped to 0 in uint32_t!)
    constexpr hft::MarketTick huge_qty_tick{
        1, 1000, 1010, std::numeric_limits<std::uint32_t>::max(), 1U};
    const std::int64_t huge_micro = hft::dispatch_hot_path_tick(strat, huge_qty_tick);
    HFT_VERIFY(huge_micro == 1009);

    // 3. Wide spread rejected cleanly
    constexpr hft::MarketTick wide_tick{1, 1000, 1050, 100, 100};
    HFT_VERIFY(hft::dispatch_hot_path_tick(strat, wide_tick) == 0);

    std::cout << "[PASS] Q01 Static Polymorphism CRTP & Concepts\n";
    return 0;
}
