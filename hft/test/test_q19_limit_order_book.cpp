#include "../src/q19_limit_order_book.hpp"
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
    hft::LimitOrderBook<64> lob;

    // 1. Add Bid and Ask orders with price-time priority
    HFT_VERIFY(lob.add_order(1, hft::Side::Buy, 10000, 50));
    HFT_VERIFY(lob.add_order(2, hft::Side::Buy, 10000, 30));
    HFT_VERIFY(lob.add_order(3, hft::Side::Buy, 10005, 40)); // Best Bid
    HFT_VERIFY(!lob.add_order(3, hft::Side::Buy, 10005, 10)); // Duplicate ID rejected cleanly

    HFT_VERIFY(lob.add_order(4, hft::Side::Sell, 10010, 25)); // Best Ask
    HFT_VERIFY(lob.add_order(5, hft::Side::Sell, 10015, 60));

    HFT_VERIFY(lob.best_bid() == 10005 && lob.level_qty(hft::Side::Buy, 10005) == 40);
    HFT_VERIFY(lob.best_ask() == 10010 && lob.level_qty(hft::Side::Sell, 10010) == 25);

    // 2. O(1) partial quantity reduction via OrderNode::level pointer
    HFT_VERIFY(lob.reduce_order_qty(1, 20));
    HFT_VERIFY(lob.level_qty(hft::Side::Buy, 10000) == 60);
    // 3. O(1) middle cancel at level 10000
    HFT_VERIFY(lob.cancel_order(1));
    HFT_VERIFY(lob.level_qty(hft::Side::Buy, 10000) == 30);

    // 4. Sweep asks with aggressive Buy market order for 50 lots
    const std::uint32_t filled = lob.execute_market_order(hft::Side::Buy, 50);
    HFT_VERIFY(filled == 50); // 25 @ 10010 + 25 @ 10015
    HFT_VERIFY(lob.best_ask() == 10015 && lob.level_qty(hft::Side::Sell, 10015) == 35);

    std::cout << "[PASS] Q19 O(1) L3 Limit Order Book\n";
    return 0;
}
