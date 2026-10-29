#include "../src/q12_shared_ptr_atomic.hpp"
#include <atomic>
#include <cstdlib>
#include <iostream>
#include <thread>
#include <vector>

#define HFT_VERIFY(expr) \
    do { \
        if (!(expr)) { \
            std::cerr << "[FAIL] " << __FILE__ << ":" << __LINE__ << " -> " #expr "\n"; \
            std::abort(); \
        } \
    } while (false)

namespace {
struct TrackedState {
    static inline std::atomic<int> destructions{0};
    int value;
    explicit TrackedState(int v) noexcept : value(v) {}
    ~TrackedState() noexcept { destructions.fetch_add(1, std::memory_order_relaxed); }
};
} // namespace

int main() {
    TrackedState::destructions.store(0);
    {
        hft::SharedPtr<TrackedState> root(new TrackedState(42));
        HFT_VERIFY(root.use_count() == 1);

        // Self copy-assignment & self move-assignment safety
        root = root;
        HFT_VERIFY(root.use_count() == 1 && root->value == 42);

        std::vector<std::thread> workers;
        for (int t = 0; t < 6; ++t) {
            workers.emplace_back([root]() {
                for (int i = 0; i < 20'000; ++i) {
                    hft::SharedPtr<TrackedState> local_copy = root;
                    HFT_VERIFY(local_copy->value == 42);
                }
            });
        }
        for (auto& w : workers) w.join();
        HFT_VERIFY(root.use_count() == 1);
    }
    HFT_VERIFY(TrackedState::destructions.load() == 1);
    std::cout << "[PASS] Q12 Reference-Counted SharedPtr Internals\n";
    return 0;
}
