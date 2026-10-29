#include "../src/q04_intrusive_list.hpp"
#include <cstdlib>
#include <iostream>

#define HFT_VERIFY(expr) \
    do { \
        if (!(expr)) { \
            std::cerr << "[FAIL] " << __FILE__ << ":" << __LINE__ << " -> " #expr "\n"; \
            std::abort(); \
        } \
    } while (false)

struct QuoteNode {
    QuoteNode* prev{nullptr};
    QuoteNode* next{nullptr};
    int id;
    explicit QuoteNode(int i) noexcept : id(i) {}
};

int main() {
    hft::IntrusiveList<QuoteNode> list;
    QuoteNode n1{1}, n2{2}, n3{3};

    list.push_back(&n1);
    list.push_back(&n2);
    list.push_back(&n3);
    HFT_VERIFY(list.size() == 3);
    HFT_VERIFY(n2.prev == &n1 && n2.next == &n3);

    // O(1) middle removal via pointer
    list.remove(&n2);
    HFT_VERIFY(n2.prev == nullptr && n2.next == nullptr);
    HFT_VERIFY(list.size() == 2);
    HFT_VERIFY(list.front()->id == 1);
    HFT_VERIFY(list.back()->id == 3);

    QuoteNode* popped = list.pop_front();
    HFT_VERIFY(popped == &n1 && n1.prev == nullptr && n1.next == nullptr);
    HFT_VERIFY(list.front() == &n3 && list.back() == &n3);

    list.remove(&n3);
    HFT_VERIFY(list.empty());
    std::cout << "[PASS] Q04 O(1) Intrusive Doubly-Linked List\n";
    return 0;
}
