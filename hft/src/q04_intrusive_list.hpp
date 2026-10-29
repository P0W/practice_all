#ifndef HFT_Q04_INTRUSIVE_LIST_HPP
#define HFT_Q04_INTRUSIVE_LIST_HPP

// ============================================================================
// QUESTION 04 [TIER 2 - EASY-MEDIUM]: O(1) INTRUSIVE DOUBLY-LINKED LIST
// ============================================================================
// EXCLUSIVE PROBLEM SCOPE (NON-OVERLAPPING):
//   Non-owning, zero-allocation $O(1)$ intrusive doubly-linked list where
//   `prev` and `next` pointers live directly inside `Node`.
//   (Contrast: Q11 manages object storage/lifetime; Q19 combines Q11 + Q04.)
//
// KEY INVARIANTS:
//   1. Zero Heap Allocations:
//      `push_back(Node*)`, `remove(Node*)`, and `pop_front()` perform pure
//      pointer manipulation in 4 instructions without touching `new`/`delete`.
//   2. Membership Preconditions & Validation:
//      - `push_back(node)` requires `node` to be currently detached
//        (`node->prev == nullptr && node->next == nullptr && node != head_`).
//      - `remove(node)` requires `node` to be currently linked in THIS list.
// ============================================================================

#include <cassert>
#include <concepts>
#include <cstddef>

namespace hft {

template <typename Node>
concept IntrusiveListNodeConcept = requires(Node* n) {
    { n->prev } -> std::convertible_to<Node*>;
    { n->next } -> std::convertible_to<Node*>;
};

template <IntrusiveListNodeConcept Node>
class IntrusiveList {
public:
    constexpr IntrusiveList() noexcept = default;
    IntrusiveList(const IntrusiveList&) = delete;
    IntrusiveList& operator=(const IntrusiveList&) = delete;

    void push_back(Node* node) noexcept {
        assert(node != nullptr);
        assert(node->prev == nullptr && node->next == nullptr && node != head_ &&
               "Node is already linked in an IntrusiveList!");

        node->prev = tail_;
        node->next = nullptr;
        if (tail_ != nullptr) {
            tail_->next = node;
        } else {
            head_ = node;
        }
        tail_ = node;
        ++size_;
    }

    void remove(Node* node) noexcept {
        assert(node != nullptr && size_ > 0);
        assert((node->prev != nullptr || node == head_) && "Node is not linked in this IntrusiveList!");

        if (node->prev != nullptr) {
            node->prev->next = node->next;
        } else {
            head_ = node->next;
        }

        if (node->next != nullptr) {
            node->next->prev = node->prev;
        } else {
            tail_ = node->prev;
        }

        node->prev = nullptr;
        node->next = nullptr;
        --size_;
    }

    [[nodiscard]] Node* pop_front() noexcept {
        if (head_ == nullptr) {
            return nullptr;
        }
        Node* first = head_;
        remove(first);
        return first;
    }

    [[nodiscard]] Node* front() const noexcept { return head_; }
    [[nodiscard]] Node* back() const noexcept { return tail_; }
    [[nodiscard]] bool empty() const noexcept { return size_ == 0; }
    [[nodiscard]] std::size_t size() const noexcept { return size_; }

private:
    Node* head_{nullptr};
    Node* tail_{nullptr};
    std::size_t size_{0};
};

} // namespace hft

#endif // HFT_Q04_INTRUSIVE_LIST_HPP
