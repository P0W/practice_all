#ifndef HFT_Q02_BINARY_WIRE_CODEC_HPP
#define HFT_Q02_BINARY_WIRE_CODEC_HPP

// ============================================================================
// QUESTION 02 [TIER 1 - EASY]: UNALIGNED-SAFE & ENDIAN-SAFE BINARY WIRE CODEC
// ============================================================================
// EXCLUSIVE PROBLEM SCOPE (NON-OVERLAPPING):
//   Allocation-free exchange binary protocol encoder/decoder (`AddOrderMsg`)
//   using `std::memcpy`, `std::bit_cast`, and `std::endian` to avoid:
//   1. Strict-aliasing Undefined Behavior (`reinterpret_cast<const Msg*>(buf)`).
//   2. Unaligned memory faults on packed network frames at arbitrary TCP/UDP offsets.
//   3. Host vs. Big-Endian network byte order mismatches (`BSWAP` / `MOVBE`).
// ============================================================================

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>

namespace hft::wire {

static_assert(std::endian::native == std::endian::little || std::endian::native == std::endian::big,
              "Mixed/PDP endian architectures are not supported");

enum class Side : std::uint8_t { Buy = 'B', Sell = 'S' };

[[nodiscard]] constexpr std::uint16_t bswap16(std::uint16_t v) noexcept {
    return static_cast<std::uint16_t>((v << 8U) | (v >> 8U));
}

[[nodiscard]] constexpr std::uint32_t bswap32(std::uint32_t v) noexcept {
    return ((v & 0x000000FFU) << 24U) |
           ((v & 0x0000FF00U) << 8U)  |
           ((v & 0x00FF0000U) >> 8U)  |
           ((v & 0xFF000000U) >> 24U);
}

[[nodiscard]] constexpr std::uint64_t bswap64(std::uint64_t v) noexcept {
    return ((v & 0x00000000000000FFULL) << 56U) |
           ((v & 0x000000000000FF00ULL) << 40U) |
           ((v & 0x0000000000FF0000ULL) << 24U) |
           ((v & 0x00000000FF000000ULL) << 8U)  |
           ((v & 0x000000FF00000000ULL) >> 8U)  |
           ((v & 0x0000FF0000000000ULL) >> 24U) |
           ((v & 0x00FF000000000000ULL) >> 40U) |
           ((v & 0xFF00000000000000ULL) >> 56U);
}

[[nodiscard]] constexpr std::uint32_t host_to_be32(std::uint32_t v) noexcept {
    if constexpr (std::endian::native == std::endian::little) return bswap32(v);
    else return v;
}

[[nodiscard]] constexpr std::uint64_t host_to_be64(std::uint64_t v) noexcept {
    if constexpr (std::endian::native == std::endian::little) return bswap64(v);
    else return v;
}

[[nodiscard]] constexpr std::uint32_t be32_to_host(std::uint32_t v) noexcept { return host_to_be32(v); }
[[nodiscard]] constexpr std::uint64_t be64_to_host(std::uint64_t v) noexcept { return host_to_be64(v); }

inline constexpr std::size_t kAddOrderWireSize = 22;

struct AddOrderMsg {
    char msg_type{'A'};
    std::uint64_t order_id{0};
    Side side{Side::Buy};
    std::uint32_t qty{0};
    std::int64_t price_raw{0};
};

inline void encode_add_order(std::span<std::byte, kAddOrderWireSize> out_buf, const AddOrderMsg& msg) noexcept {
    out_buf[0] = static_cast<std::byte>(msg.msg_type);

    const std::uint64_t be_oid = host_to_be64(msg.order_id);
    std::memcpy(out_buf.data() + 1, &be_oid, sizeof(be_oid));

    out_buf[9] = static_cast<std::byte>(msg.side);

    const std::uint32_t be_qty = host_to_be32(msg.qty);
    std::memcpy(out_buf.data() + 10, &be_qty, sizeof(be_qty));

    const std::uint64_t be_px = host_to_be64(std::bit_cast<std::uint64_t>(msg.price_raw));
    std::memcpy(out_buf.data() + 14, &be_px, sizeof(be_px));
}

[[nodiscard]] inline std::optional<AddOrderMsg> decode_add_order(std::span<const std::byte> wire_bytes) noexcept {
    if (wire_bytes.size() < kAddOrderWireSize) [[unlikely]] {
        return std::nullopt;
    }

    const char mtype = static_cast<char>(wire_bytes[0]);
    if (mtype != 'A') [[unlikely]] {
        return std::nullopt;
    }

    const auto raw_side = static_cast<std::uint8_t>(wire_bytes[9]);
    if (raw_side != 'B' && raw_side != 'S') [[unlikely]] {
        return std::nullopt;
    }

    std::uint64_t be_oid = 0;
    std::uint32_t be_qty = 0;
    std::uint64_t be_px  = 0;

    std::memcpy(&be_oid, wire_bytes.data() + 1, sizeof(be_oid));
    std::memcpy(&be_qty, wire_bytes.data() + 10, sizeof(be_qty));
    std::memcpy(&be_px,  wire_bytes.data() + 14, sizeof(be_px));

    return AddOrderMsg{
        .msg_type  = mtype,
        .order_id  = be64_to_host(be_oid),
        .side      = (raw_side == 'B') ? Side::Buy : Side::Sell,
        .qty       = be32_to_host(be_qty),
        .price_raw = std::bit_cast<std::int64_t>(be64_to_host(be_px))
    };
}

} // namespace hft::wire

#endif // HFT_Q02_BINARY_WIRE_CODEC_HPP
