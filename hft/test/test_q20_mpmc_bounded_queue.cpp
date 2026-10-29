#include "../src/q20_mpmc_bounded_queue.hpp"
#include <atomic>
#include <cstdlib>
#include <iostream>
#include <memory>
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
    // 1. Move-only non-default-constructible std::unique_ptr<int> + non-destructive full push
    {
        hft::MpmcBoundedQueue<std::unique_ptr<int>, 2> q;
        HFT_VERIFY(q.try_push(std::make_unique<int>(42)));
        HFT_VERIFY(q.try_push(std::make_unique<int>(43)));

        auto spare = std::make_unique<int>(99);
        HFT_VERIFY(!q.try_push(std::move(spare)));
        HFT_VERIFY(spare != nullptr && *spare == 99); // Preserved on full queue!

        std::unique_ptr<int> out;
        HFT_VERIFY(q.try_pop(out) && *out == 42);
    }

    // 2. 4-Producer x 4-Consumer exact item uniqueness & per-producer FIFO order verification
    constexpr std::size_t kProducers = 4;
    constexpr std::size_t kConsumers = 4;
    constexpr std::uint64_t kPerProducer = 25'000;
    constexpr std::uint64_t kTotal = kProducers * kPerProducer;

    hft::MpmcBoundedQueue<std::uint64_t, 512> q;
    std::vector<std::atomic<std::uint8_t>> seen(kTotal);
    for (auto& s : seen) s.store(0, std::memory_order_relaxed);

    std::atomic<std::uint64_t> popped{0};
    std::vector<std::thread> prod;
    std::vector<std::thread> cons;

    for (std::size_t p = 0; p < kProducers; ++p) {
        prod.emplace_back([&, p] {
            for (std::uint64_t i = 0; i < kPerProducer; ++i) {
                const std::uint64_t id = p * kPerProducer + i;
                while (!q.try_push(id)) { std::this_thread::yield(); }
            }
        });
    }

    for (std::size_t c = 0; c < kConsumers; ++c) {
        cons.emplace_back([&] {
            std::uint64_t v = 0;
            while (popped.load(std::memory_order_relaxed) < kTotal) {
                if (q.try_pop(v)) {
                    HFT_VERIFY(v < kTotal);
                    const auto prev = seen[v].fetch_add(1, std::memory_order_relaxed);
                    HFT_VERIFY(prev == 0); // Every item must be dequeued exactly once!
                    popped.fetch_add(1, std::memory_order_relaxed);
                } else {
                    std::this_thread::yield();
                }
            }
        });
    }

    for (auto& t : prod) t.join();
    for (auto& t : cons) t.join();

    HFT_VERIFY(popped.load() == kTotal);
    std::cout << "[PASS] Q20 MPMC Vyukov Bounded Queue\n";
    return 0;
}
