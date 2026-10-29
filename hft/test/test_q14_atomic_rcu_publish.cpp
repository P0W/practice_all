#include "../src/q14_atomic_rcu_publish.hpp"
#include <atomic>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

#define HFT_VERIFY(expr) \
    do { \
        if (!(expr)) { \
            std::cerr << "[FAIL] " << __FILE__ << ":" << __LINE__ << " -> " #expr "\n"; \
            std::abort(); \
        } \
    } while (false)

struct RiskRoutingTable {
    std::uint64_t epoch{0};
    std::int64_t  max_notional{0};
};

int main() {
    hft::AtomicRcuStore<RiskRoutingTable> rcu(std::in_place, RiskRoutingTable{1, 1'000'000});

    // 1. Verify nullptr publish is rejected cleanly
    bool rejected_null = false;
    try {
        rcu.publish(nullptr);
    } catch (const std::invalid_argument&) {
        rejected_null = true;
    }
    HFT_VERIFY(rejected_null);

    // 2. Concurrent readers + CAS writers
    std::atomic<bool> stop{false};
    std::vector<std::thread> readers;
    for (int r = 0; r < 4; ++r) {
        readers.emplace_back([&] {
            while (!stop.load(std::memory_order_acquire)) {
                auto snap = rcu.read();
                HFT_VERIFY(snap != nullptr);
                HFT_VERIFY(snap->max_notional == static_cast<std::int64_t>(snap->epoch * 1'000'000));
            }
        });
    }

    std::vector<std::thread> writers;
    for (int w = 0; w < 2; ++w) {
        writers.emplace_back([&] {
            for (int i = 0; i < 5'000; ++i) {
                rcu.update_cas([](RiskRoutingTable& next) {
                    ++next.epoch;
                    next.max_notional = static_cast<std::int64_t>(next.epoch * 1'000'000);
                });
            }
        });
    }

    for (auto& w : writers) w.join();
    stop.store(true, std::memory_order_release);
    for (auto& r : readers) r.join();

    HFT_VERIFY(rcu.read()->epoch == 10'001);
    std::cout << "[PASS] Q14 RCU Immutable Snapshot Publisher\n";
    return 0;
}
