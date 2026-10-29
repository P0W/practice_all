#include "../src/q03_fixed_point_price.hpp"
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
    using Price = hft::FixedPrice<10'000>;

    // 1. Exact decimal representation without IEEE-754 drift
    constexpr Price p1 = Price::from_whole_and_frac(100, 2500); // 100.2500
    constexpr Price p2 = Price::from_whole_and_frac(0, 7500);   //   0.7500
    constexpr Price sum = p1 + p2;
    static_assert(sum.whole_part() == 101 && sum.frac_part() == 0);
    HFT_VERIFY(sum.whole_part() == 101 && sum.frac_part() == 0);

    // 2. Wide Multiplication Test:
    //    400,000.0000 * 400,000.0000 -> raw = 4e9 * 4e9 = 1.6e19 (overflows raw int64_t!),
    //    but scaled product is 160,000,000,000.0000 (raw = 1.6e15, fits in int64_t!).
    constexpr Price large_px = Price::from_whole_and_frac(400'000, 0);
    constexpr auto prod_opt = Price::checked_multiply_prices(large_px, large_px);
    static_assert(prod_opt.has_value());
    static_assert(prod_opt->whole_part() == 160'000'000'000LL);
    HFT_VERIFY(prod_opt.has_value() && prod_opt->whole_part() == 160'000'000'000LL);

    // 3. Checked overflow returns std::nullopt without UB
    const Price max_px = Price::from_raw(std::numeric_limits<std::int64_t>::max());
    HFT_VERIFY(!Price::checked_add(max_px, Price::from_raw(1)).has_value());
    HFT_VERIFY(!Price::checked_from_whole_and_frac(10, -1).has_value());
    HFT_VERIFY(!Price::checked_from_whole_and_frac(10, 10'000).has_value());

    std::cout << "[PASS] Q03 Checked Fixed-Point Price Arithmetic\n";
    return 0;
}
