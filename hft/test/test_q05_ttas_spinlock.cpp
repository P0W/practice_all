#include "../src/q05_ttas_spinlock.hpp"
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>

#define HFT_VERIFY(expr) \
    do { \
        if (!(expr)) { \
            std::cerr << "[FAIL] " << __FILE__ << ":" << __LINE__ << " -> " #expr "\n"; \
            std::abort(); \
        } \
    } while (false)

int main() {
    hft::TtasSpinLock spin;
    std::uint64_t shared_counter = 0;
    constexpr int kThreads = 8;
    constexpr int kIncrements = 50'000;

    // Verify try_lock semantics on uncontended lock
    HFT_VERIFY(spin.try_lock());
    HFT_VERIFY(!spin.try_lock());
    spin.unlock();

    std::vector<std::thread> threads;
    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([&] {
            for (int i = 0; i < kIncrements; ++i) {
                std::lock_guard<hft::TtasSpinLock> guard(spin);
                ++shared_counter;
            }
        });
    }

    for (auto& t : threads) t.join();
    HFT_VERIFY(shared_counter == static_cast<std::uint64_t>(kThreads * kIncrements));
    std::cout << "[PASS] Q05 TTAS SpinLock with Hardware Pause\n";
    return 0;
}
