#include "../src/q09_thread_pool.hpp"
#include <atomic>
#include <cstdlib>
#include <future>
#include <iostream>
#include <stdexcept>
#include <vector>

#define HFT_VERIFY(expr) \
    do { \
        if (!(expr)) { \
            std::cerr << "[FAIL] " << __FILE__ << ":" << __LINE__ << " -> " #expr "\n"; \
            std::abort(); \
        } \
    } while (false)

int main() {
    // 1. DrainQueued policy: all 200 submitted tasks complete before destructor returns
    std::atomic<int> completed{0};
    {
        hft::ThreadPool pool(4, hft::PoolShutdownPolicy::DrainQueued);
        std::vector<std::future<int>> futures;
        for (int i = 1; i <= 200; ++i) {
            futures.push_back(pool.submit([i, &completed] {
                completed.fetch_add(1, std::memory_order_relaxed);
                return i * 2;
            }));
        }
        for (int i = 0; i < 200; ++i) {
            HFT_VERIFY(futures[i].get() == (i + 1) * 2);
        }
    }
    HFT_VERIFY(completed.load() == 200);

    // 2. Exception propagation through packaged_task future
    {
        hft::ThreadPool pool(2);
        auto bad_fut = pool.submit([]() -> int {
            throw std::runtime_error("worker task failure");
        });
        bool caught = false;
        try {
            (void)bad_fut.get();
        } catch (const std::runtime_error&) {
            caught = true;
        }
        HFT_VERIFY(caught);
    }

    std::cout << "[PASS] Q09 Fixed-Worker ThreadPool\n";
    return 0;
}
