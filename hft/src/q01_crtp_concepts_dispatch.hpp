#ifndef HFT_Q01_CRTP_CONCEPTS_DISPATCH_HPP
#define HFT_Q01_CRTP_CONCEPTS_DISPATCH_HPP

// ============================================================================
// QUESTION 01 [TIER 1 - WARM-UP]: STATIC POLYMORPHISM (CRTP & C++20 CONCEPTS)
// ============================================================================
// EXCLUSIVE PROBLEM SCOPE (NON-OVERLAPPING):
//   Zero-overhead compile-time strategy dispatch via **CRTP** and **C++20 Concepts**,
//   eliminating `vptr`/`vtable` indirection, enabling cross-call inlining, and
//   preventing Branch Target Buffer (BTB) / L1i misses.
//   (Contrast: Q13 covers runtime type-erased callbacks; Q03 covers fixed-point math.)
//
// KEY OVERFLOW SAFETY IN MICROPRICE CALCULATION:
//   - Always widen `uint32_t` quantities to `uint64_t` BEFORE adding
//     (`static_cast<uint64_t>(bid_qty) + static_cast<uint64_t>(ask_qty)`) so
//     `UINT32_MAX + 1` never wraps to `0`!
//   - Rewrites microprice as `bid_price + (spread * bid_qty) / total_qty`
//     so `bid_price * ask_qty` never overflows signed 64-bit integers!
// ============================================================================

#include <concepts>
#include <cstdint>

namespace hft {

struct MarketTick {
    std::uint64_t instrument_id;
    std::int64_t bid_price;
    std::int64_t ask_price;
    std::uint32_t bid_qty;
    std::uint32_t ask_qty;
};

// Approach A: C++20 Concept constraining any zero-overhead strategy handler
template <typename S>
concept TickStrategyConcept = requires(const S& strat, const MarketTick& tick) {
    { strat.on_tick(tick) } noexcept -> std::same_as<std::int64_t>;
};

// Approach B: Curiously Recurring Template Pattern (CRTP) Base Class
template <typename Derived>
class CrtpStrategyBase {
public:
    [[nodiscard]] constexpr std::int64_t process_tick(const MarketTick& tick) const noexcept {
        return static_cast<const Derived*>(this)->on_tick_impl(tick);
    }
};

// Concrete Strategy: Overflow-safe size-weighted microprice
class SpreadSignalStrategy : public CrtpStrategyBase<SpreadSignalStrategy> {
public:
    constexpr explicit SpreadSignalStrategy(std::int64_t max_spread) noexcept
        : m_max_spread(max_spread) {}

    [[nodiscard]] constexpr std::int64_t on_tick_impl(const MarketTick& tick) const noexcept {
        if (tick.ask_price <= tick.bid_price || m_max_spread <= 0) {
            return 0;
        }
        // Compute unsigned spread safely to prevent signed subtraction overflow
        const std::uint64_t u_spread =
            static_cast<std::uint64_t>(tick.ask_price) - static_cast<std::uint64_t>(tick.bid_price);
        if (u_spread > static_cast<std::uint64_t>(m_max_spread)) {
            return 0;
        }

        // Widen uint32_t quantities BEFORE addition so UINT32_MAX + 1 never wraps!
        const std::uint64_t total_qty =
            static_cast<std::uint64_t>(tick.bid_qty) + static_cast<std::uint64_t>(tick.ask_qty);
        if (total_qty == 0ULL) {
            return 0;
        }

        // Algebraically equivalent to (bid * ask_qty + ask * bid_qty) / total_qty,
        // but bounded by `u_spread * bid_qty` so large prices cannot overflow int64_t!
        const std::uint64_t offset = (u_spread * static_cast<std::uint64_t>(tick.bid_qty)) / total_qty;
        return tick.bid_price + static_cast<std::int64_t>(offset);
    }

    [[nodiscard]] constexpr std::int64_t on_tick(const MarketTick& tick) const noexcept {
        return process_tick(tick);
    }

private:
    std::int64_t m_max_spread;
};

template <TickStrategyConcept Strategy>
[[nodiscard]] constexpr std::int64_t dispatch_hot_path_tick(const Strategy& strategy, const MarketTick& tick) noexcept {
    return strategy.on_tick(tick);
}

} // namespace hft

#endif // HFT_Q01_CRTP_CONCEPTS_DISPATCH_HPP
