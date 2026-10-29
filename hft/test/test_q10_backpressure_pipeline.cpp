#include "../src/q10_backpressure_pipeline.hpp"
#include <atomic>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <thread>

#define HFT_VERIFY(expr) \
    do { \
        if (!(expr)) { \
            std::cerr << "[FAIL] " << __FILE__ << ":" << __LINE__ << " -> " #expr "\n"; \
            std::abort(); \
        } \
    } while (false)

int main() {
    // 1. OverflowPolicy::DropOldest eviction semantics
    {
        hft::BoundedChannel<int> ch(2, hft::OverflowPolicy::DropOldest);
        HFT_VERIFY(ch.push(10));
        HFT_VERIFY(ch.push(20));
        HFT_VERIFY(ch.push(30)); // Evicts 10, retains {20, 30}
        HFT_VERIFY(*ch.pop() == 20);
        HFT_VERIFY(*ch.pop() == 30);
    }

    // 2. Pattern 9 PipelineStageGuard: verify exception unwind closes channels and joins workers cleanly
    std::atomic<int> processed{0};
    bool caught_stage_error = false;
    try {
        hft::BoundedChannel<int> stage1_to_2(8, hft::OverflowPolicy::Block);
        hft::PipelineStageGuard guard;
        guard.register_channel(stage1_to_2);

        guard.add_worker(std::jthread([&] {
            while (auto v = stage1_to_2.pop()) {
                processed.fetch_add(*v, std::memory_order_relaxed);
            }
        }));

        stage1_to_2.push(10);
        stage1_to_2.push(20);
        // Simulate mid-pipeline exception: ~PipelineStageGuard() MUST close stage1_to_2
        // so the consumer jthread unblocks from pop() and joins without deadlocking!
        throw std::runtime_error("simulated pipeline failure");
    } catch (const std::runtime_error&) {
        caught_stage_error = true;
    }
    HFT_VERIFY(caught_stage_error);
    HFT_VERIFY(processed.load() == 30);

    std::cout << "[PASS] Q10 Bounded Backpressure Channel & PipelineStageGuard\n";
    return 0;
}
