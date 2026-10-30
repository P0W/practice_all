#ifndef HFT_Q03_FIXED_POINT_PRICE_HPP
#define HFT_Q03_FIXED_POINT_PRICE_HPP

// ============================================================================
// QUESTION 03 [TIER 1 - EASY]: CHECKED FIXED-POINT TICK PRICE ARITHMETIC
// ============================================================================
// EXCLUSIVE PROBLEM SCOPE (NON-OVERLAPPING):
//   Exact scaled-integer decimal representation (`FixedPrice<Scale>`) with
//   checked overflow arithmetic (`checked_add`, `checked_sub`, `checked_mul_qty`,
//   and wide intermediate `multiply_prices`) so order-book prices and PnL
//   never suffer from IEEE-754 floating-point drift OR silent signed-overflow UB.
//
// KEY NUMERIC SAFETY:
//   1. Why raw `a.m_raw * b.m_raw` overflows 64-bit signed ints:
//      If `Scale = 10,000` and `a = 400,000.0000` (`m_raw = 4,000,000,000`),
//      `a.m_raw * b.m_raw = 1.6e19 > INT64_MAX`, even though the scaled product
//      (`1.6e15`) easily fits in `int64_t`!
//      By decomposing `a = q_a * Scale + r_a` and `b = q_b * Scale + r_b`,
//      we compute `(a * b)` exactly without needing non-standard `__int128`!
//   2. Checked Overflow API:
//      Provides `checked_add`, `checked_sub`, and `checked_multiply_prices`
//      returning `std::optional<FixedPrice>` without ever triggering C++
//      signed integer overflow Undefined Behavior.
// ============================================================================

#include <cassert>
#include <compare>
#include <cstdint>
#include <limits>
#include <optional>

namespace hft {

template <std::int64_t Scale = 10'000>
    requires(Scale > 0)
class FixedPrice {
public:
    static constexpr std::int64_t kScale = Scale;
    static constexpr std::int64_t kMaxRaw = std::numeric_limits<std::int64_t>::max();
    static constexpr std::int64_t kMinRaw = std::numeric_limits<std::int64_t>::min();

    constexpr FixedPrice() noexcept = default;
    constexpr explicit FixedPrice(std::int64_t raw_scaled) noexcept : m_raw(raw_scaled) {}

    [[nodiscard]] static constexpr FixedPrice from_raw(std::int64_t raw_scaled) noexcept {
        return FixedPrice(raw_scaled);
    }

    // `frac_scaled` must be non-negative in [0, Scale). Sign is determined by `whole`.
    [[nodiscard]] static constexpr std::optional<FixedPrice>
    checked_from_whole_and_frac(std::int64_t whole, std::int64_t frac_scaled) noexcept {
        if (frac_scaled < 0 || frac_scaled >= Scale) {
            return std::nullopt;
        }
        if (whole > kMaxRaw / Scale || whole < kMinRaw / Scale) {
            return std::nullopt;
        }
        const std::int64_t base = whole * Scale;
        if (whole >= 0) {
            if (base > kMaxRaw - frac_scaled) return std::nullopt;
            return FixedPrice(base + frac_scaled);
        } else {
            if (base < kMinRaw + frac_scaled) return std::nullopt;
            return FixedPrice(base - frac_scaled);
        }
    }

    [[nodiscard]] static constexpr FixedPrice from_whole_and_frac(std::int64_t whole, std::int64_t frac_scaled) noexcept {
        const auto res = checked_from_whole_and_frac(whole, frac_scaled);
        assert(res.has_value() && "FixedPrice::from_whole_and_frac overflow or invalid frac_scaled!");
        return res.value_or(FixedPrice{0});
    }

    [[nodiscard]] constexpr std::int64_t raw() const noexcept { return m_raw; }
    [[nodiscard]] constexpr std::int64_t whole_part() const noexcept { return m_raw / Scale; }
    [[nodiscard]] constexpr std::int64_t frac_part() const noexcept {
        const std::int64_t rem = m_raw % Scale;
        return rem >= 0 ? rem : -rem;
    }

    // UB-free checked addition
    [[nodiscard]] static constexpr std::optional<FixedPrice> checked_add(FixedPrice a, FixedPrice b) noexcept {
        if ((b.m_raw > 0 && a.m_raw > kMaxRaw - b.m_raw) ||
            (b.m_raw < 0 && a.m_raw < kMinRaw - b.m_raw)) {
            return std::nullopt;
        }
        return FixedPrice(a.m_raw + b.m_raw);
    }

    // UB-free checked subtraction
    [[nodiscard]] static constexpr std::optional<FixedPrice> checked_sub(FixedPrice a, FixedPrice b) noexcept {
        if ((b.m_raw < 0 && a.m_raw > kMaxRaw + b.m_raw) ||
            (b.m_raw > 0 && a.m_raw < kMinRaw + b.m_raw)) {
            return std::nullopt;
        }
        return FixedPrice(a.m_raw - b.m_raw);
    }

    // Portable wide multiplication: decomposes `a = qa*Scale + ra`, `b = qb*Scale + rb`
    // so `(a * b) / Scale = qa*qb*Scale + qa*rb + qb*ra + round((ra*rb)/Scale)`
    // without overflowing 64-bit signed intermediate products!
    [[nodiscard]] static constexpr std::optional<FixedPrice>
    checked_multiply_prices(FixedPrice a, FixedPrice b) noexcept {
        const std::int64_t qa = a.m_raw / Scale;
        const std::int64_t ra = a.m_raw % Scale;
        const std::int64_t qb = b.m_raw / Scale;
        const std::int64_t rb = b.m_raw % Scale;

        std::int64_t term1 = 0;
        if (!checked_mul_i64(qa, qb, term1) || !checked_mul_i64(term1, Scale, term1)) {
            return std::nullopt;
        }

        std::int64_t term2 = 0;
        std::int64_t term3 = 0;
        std::int64_t rem_prod = 0;
        if (!checked_mul_i64(qa, rb, term2) ||
            !checked_mul_i64(qb, ra, term3) ||
            !checked_mul_i64(ra, rb, rem_prod)) {
            return std::nullopt;
        }

        const std::int64_t half = (rem_prod >= 0) ? (Scale / 2) : -(Scale / 2);
        const std::int64_t term4 = (rem_prod + half) / Scale;

        auto acc = checked_add(FixedPrice(term1), FixedPrice(term2));
        if (!acc) return std::nullopt;
        acc = checked_add(*acc, FixedPrice(term3));
        if (!acc) return std::nullopt;
        return checked_add(*acc, FixedPrice(term4));
    }

    [[nodiscard]] friend constexpr FixedPrice operator+(FixedPrice a, FixedPrice b) noexcept {
        const auto r = checked_add(a, b);
        assert(r.has_value() && "FixedPrice addition overflow!");
        return r.value_or(FixedPrice{0});
    }

    [[nodiscard]] friend constexpr FixedPrice operator-(FixedPrice a, FixedPrice b) noexcept {
        const auto r = checked_sub(a, b);
        assert(r.has_value() && "FixedPrice subtraction overflow!");
        return r.value_or(FixedPrice{0});
    }

    [[nodiscard]] friend constexpr FixedPrice operator*(FixedPrice px, std::int64_t qty) noexcept {
        std::int64_t out = 0;
        const bool ok = checked_mul_i64(px.m_raw, qty, out);
        assert(ok && "FixedPrice quantity multiplication overflow!");
        return ok ? FixedPrice(out) : FixedPrice{0};
    }

    [[nodiscard]] friend constexpr FixedPrice multiply_prices(FixedPrice a, FixedPrice b) noexcept {
        const auto r = checked_multiply_prices(a, b);
        assert(r.has_value() && "FixedPrice::multiply_prices overflow!");
        return r.value_or(FixedPrice{0});
    }

    friend constexpr auto operator<=>(FixedPrice, FixedPrice) noexcept = default;

private:
    [[nodiscard]] static constexpr bool checked_mul_i64(std::int64_t x, std::int64_t y, std::int64_t& out) noexcept {
        if (x == 0 || y == 0) {
            out = 0;
            return true;
        }
        if ((x == kMinRaw && y == -1) || (y == kMinRaw && x == -1)) {
            return false;
        }
        if (x > 0) {
            if (y > 0 && x > kMaxRaw / y) return false;
            if (y < 0 && y < kMinRaw / x) return false;
        } else {
            if (y > 0 && x < kMinRaw / y) return false;
            if (y < 0 && x < kMaxRaw / y) return false;
        }
        out = x * y;
        return true;
    }

    std::int64_t m_raw{0};
};

} // namespace hft

#endif // HFT_Q03_FIXED_POINT_PRICE_HPP
