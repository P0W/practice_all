#ifndef HFT_Q19_LIMIT_ORDER_BOOK_HPP
#define HFT_Q19_LIMIT_ORDER_BOOK_HPP

// ============================================================================
// QUESTION 19 [TIER 5 - PRINCIPAL]: O(1) L3 LIMIT ORDER BOOK & MATCHING ENGINE
// ============================================================================
// EXCLUSIVE PROBLEM SCOPE (NON-OVERLAPPING):
//   L3 Order-by-Order (`AddOrder`, `CancelOrder`, `ReduceOrder`, `ExecuteMarket`)
//   Limit Order Book composing `FixedObjectPool` (Q11) + `IntrusiveList` (Q04).
//
// KEY ARCHITECTURAL IMPROVEMENTS:
//   1. True $O(1)$ `reduce_order_qty` and Non-Empty `cancel_order`:
//      Each `OrderNode` caches a direct pointer `PriceLevel* level` to its
//      owning `PriceLevel`! Thus:
//      - `reduce_order_qty(id, delta)` is **strictly $O(1)$** (1 hash lookup +
//        direct `node->level->total_qty -= delta`, zero `std::map::find` tree
//        traversals!).
//      - `cancel_order(id)` removes the node from `node->level->orders` in
//        **$O(1)$**, and only touches `std::map::erase(price)` ($O(\log L)$ where
//        $L$ is active price levels) when `node->level->orders.empty()`!
//   2. Transactional Exception Safety on `add_order`:
//      Looks up/creates the `PriceLevel` and inserts into `m_order_index` with
//      full rollback if any container allocation fails, never leaking a pool
//      slot or leaving a dangling index entry.
// ============================================================================

#include "q11_fixed_object_pool.hpp"
#include "q04_intrusive_list.hpp"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <unordered_map>

namespace hft {

enum class Side : std::uint8_t { Buy = 0, Sell = 1 };

struct PriceLevel;

struct OrderNode {
    std::uint64_t order_id{0};
    std::int64_t price{0};
    std::uint32_t qty{0};
    Side side{Side::Buy};
    PriceLevel* level{nullptr}; // Direct pointer to owning PriceLevel -> avoids O(log L) map::find!
    OrderNode* prev{nullptr};
    OrderNode* next{nullptr};
};

struct PriceLevel {
    std::int64_t price{0};
    std::uint64_t total_qty{0};
    IntrusiveList<OrderNode> orders{};
};

template <std::size_t MaxOrders = 65536>
class LimitOrderBook {
public:
    LimitOrderBook() { m_order_index.reserve(MaxOrders); }

    LimitOrderBook(const LimitOrderBook&) = delete;
    LimitOrderBook& operator=(const LimitOrderBook&) = delete;

    // Add resting order: O(log L) to locate/insert price level + O(1) pool & FIFO append
    [[nodiscard]] bool add_order(std::uint64_t order_id, Side side, std::int64_t price, std::uint32_t qty) {
        if (qty == 0 || m_order_index.contains(order_id)) [[unlikely]] {
            return false;
        }

        OrderNode* node = m_pool.allocate(order_id, price, qty, side, nullptr, nullptr, nullptr);
        if (node == nullptr) [[unlikely]] {
            return false;
        }

        PriceLevel* lvl_ptr = nullptr;
        bool created_new_level = false;

        try {
            if (side == Side::Buy) {
                auto [it, inserted] = m_bids.try_emplace(price);
                if (inserted) it->second.price = price;
                lvl_ptr = &it->second;
                created_new_level = inserted;
            } else {
                auto [it, inserted] = m_asks.try_emplace(price);
                if (inserted) it->second.price = price;
                lvl_ptr = &it->second;
                created_new_level = inserted;
            }

            m_order_index.emplace(order_id, node);
        } catch (...) {
            if (created_new_level) {
                if (side == Side::Buy) m_bids.erase(price);
                else m_asks.erase(price);
            }
            m_pool.deallocate(node);
            throw;
        }

        node->level = lvl_ptr;
        lvl_ptr->total_qty += qty;
        lvl_ptr->orders.push_back(node);
        return true;
    }

    // Cancel order by ID: O(1) via cached PriceLevel* when level stays non-empty;
    // O(log L) only when erasing an exhausted price level.
    [[nodiscard]] bool cancel_order(std::uint64_t order_id) {
        auto it = m_order_index.find(order_id);
        if (it == m_order_index.end()) [[unlikely]] {
            return false;
        }

        OrderNode* node = it->second;
        m_order_index.erase(it);
        unlink_and_cleanup_level(node);
        m_pool.deallocate(node);
        return true;
    }

    // Reduce quantity in strict O(1) while preserving FIFO queue position!
    [[nodiscard]] bool reduce_order_qty(std::uint64_t order_id, std::uint32_t reduce_by) {
        auto it = m_order_index.find(order_id);
        if (it == m_order_index.end() || reduce_by == 0) [[unlikely]] {
            return false;
        }

        OrderNode* node = it->second;
        if (reduce_by >= node->qty) {
            m_order_index.erase(it);
            unlink_and_cleanup_level(node);
            m_pool.deallocate(node);
            return true;
        }

        node->qty -= reduce_by;
        node->level->total_qty -= reduce_by; // O(1) direct pointer update — zero std::map lookup!
        return true;
    }

    // Execute aggressive market order against opposite book in strict Price-Time FIFO priority
    [[nodiscard]] std::uint32_t execute_market_order(Side aggressor_side, std::uint32_t requested_qty) {
        std::uint32_t filled_qty = 0;

        if (aggressor_side == Side::Buy) {
            while (requested_qty > 0 && !m_asks.empty()) {
                auto lvl_it = m_asks.begin();
                PriceLevel& lvl = lvl_it->second;
                sweep_level(lvl, requested_qty, filled_qty);
                if (lvl.orders.empty()) {
                    m_asks.erase(lvl_it);
                }
            }
        } else {
            while (requested_qty > 0 && !m_bids.empty()) {
                auto lvl_it = m_bids.begin();
                PriceLevel& lvl = lvl_it->second;
                sweep_level(lvl, requested_qty, filled_qty);
                if (lvl.orders.empty()) {
                    m_bids.erase(lvl_it);
                }
            }
        }

        return filled_qty;
    }

    [[nodiscard]] std::optional<std::int64_t> best_bid() const noexcept {
        if (m_bids.empty()) return std::nullopt;
        return m_bids.begin()->first;
    }

    [[nodiscard]] std::optional<std::int64_t> best_ask() const noexcept {
        if (m_asks.empty()) return std::nullopt;
        return m_asks.begin()->first;
    }

    [[nodiscard]] std::uint64_t level_qty(Side side, std::int64_t price) const noexcept {
        if (side == Side::Buy) {
            auto it = m_bids.find(price);
            return it != m_bids.end() ? it->second.total_qty : 0ULL;
        } else {
            auto it = m_asks.find(price);
            return it != m_asks.end() ? it->second.total_qty : 0ULL;
        }
    }

    [[nodiscard]] std::size_t active_order_count() const noexcept { return m_order_index.size(); }

private:
    void sweep_level(PriceLevel& lvl, std::uint32_t& requested_qty, std::uint32_t& filled_qty) {
        while (requested_qty > 0 && !lvl.orders.empty()) {
            OrderNode* head = lvl.orders.front();
            const std::uint32_t trade_qty = std::min(requested_qty, head->qty);
            filled_qty += trade_qty;
            requested_qty -= trade_qty;
            head->qty -= trade_qty;
            lvl.total_qty -= trade_qty;

            if (head->qty == 0) {
                lvl.orders.remove(head);
                m_order_index.erase(head->order_id);
                m_pool.deallocate(head);
            }
        }
    }

    void unlink_and_cleanup_level(OrderNode* node) {
        PriceLevel* lvl = node->level;
        lvl->total_qty -= node->qty;
        lvl->orders.remove(node);
        if (lvl->orders.empty()) {
            if (node->side == Side::Buy) {
                m_bids.erase(node->price);
            } else {
                m_asks.erase(node->price);
            }
        }
    }

    FixedObjectPool<OrderNode, MaxOrders> m_pool{};
    std::unordered_map<std::uint64_t, OrderNode*> m_order_index{};
    std::map<std::int64_t, PriceLevel, std::greater<std::int64_t>> m_bids{};
    std::map<std::int64_t, PriceLevel, std::less<std::int64_t>> m_asks{};
};

} // namespace hft

#endif // HFT_Q19_LIMIT_ORDER_BOOK_HPP
