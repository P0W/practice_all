#include "../src/q02_binary_wire_codec.hpp"
#include <array>
#include <cstdlib>
#include <iostream>

#define HFT_VERIFY(expr) \
    do { \
        if (!(expr)) { \
            std::cerr << "[FAIL] " << __FILE__ << ":" << __LINE__ << " -> " #expr "\n"; \
            std::abort(); \
        } \
    } while (false)

int main() {
    // Deliberately misalign buffer by 1 byte to verify unaligned-safe memcpy codec
    alignas(16) std::array<std::byte, hft::wire::kAddOrderWireSize + 3> raw_storage{};
    std::span<std::byte, hft::wire::kAddOrderWireSize> unaligned_frame(
        raw_storage.data() + 1, hft::wire::kAddOrderWireSize);

    const hft::wire::AddOrderMsg original{
        .msg_type  = 'A',
        .order_id  = 0x0102030405060708ULL,
        .side      = hft::wire::Side::Sell,
        .qty       = 1500U,
        .price_raw = -250'500LL // Also verify negative signed price round-trip
    };

    hft::wire::encode_add_order(unaligned_frame, original);

    const auto decoded = hft::wire::decode_add_order(unaligned_frame);
    HFT_VERIFY(decoded.has_value());
    HFT_VERIFY(decoded->order_id == original.order_id);
    HFT_VERIFY(decoded->side == hft::wire::Side::Sell);
    HFT_VERIFY(decoded->qty == 1500U);
    HFT_VERIFY(decoded->price_raw == -250'500LL);

    // Truncated frame must return std::nullopt
    HFT_VERIFY(!hft::wire::decode_add_order(unaligned_frame.first(10)).has_value());

    std::cout << "[PASS] Q02 Unaligned & Endian-Safe Binary Wire Codec\n";
    return 0;
}
