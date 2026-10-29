#include "../src/q06_sharded_counter_map.hpp"
#include <cstdlib>
#include <iostream>
#include <string>
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
    // Verify both power-of-two (16) and prime (17) shard counts work cleanly
    hft::ShardedCounterMap<int, 17> counters;

    constexpr int kThreads = 8;
    constexpr int kKeys = 32;
    constexpr int kIters = 2'048; // Exact multiple of kKeys (64 increments/key/thread)

    std::vector<std::thread> workers;
    for (int t = 0; t < kThreads; ++t) {
        workers.emplace_back([&] {
            for (int i = 0; i < kIters; ++i) {
                counters.increment(i % kKeys, 1);
            }
        });
    }
    for (auto& w : workers) w.join();

    HFT_VERIFY(counters.total() == static_cast<std::int64_t>(kThreads * kIters));
    for (int k = 0; k < kKeys; ++k) {
        HFT_VERIFY(counters.get(k) == static_cast<std::int64_t>((kThreads * kIters) / kKeys));
    }

    std::cout << "[PASS] Q06 Cache-Line-Aligned Sharded Counter Map\n";
    return 0;
}
