#include "../src/q16_spsc_ring_buffer.hpp"
#include <cstdlib>
#include <iostream>
#include <memory>
#include <thread>

#define HFT_VERIFY(expr) \
    do { \
        if (!(expr)) { \
            std::cerr << "[FAIL] " << __FILE__ << ":" << __LINE__ << " -> " #expr "\n"; \
            std::abort(); \
        } \
    } while (false)

namespace {
struct MoveOnlyPayload {
    static inline int live_count = 0;
    int value;
    explicit MoveOnlyPayload(int v) noexcept : value(v) { ++live_count; }
    MoveOnlyPayload(const MoveOnlyPayload&) = delete;
    MoveOnlyPayload& operator=(const MoveOnlyPayload&) = delete;
    MoveOnlyPayload(MoveOnlyPayload&& o) noexcept : value(o.value) {
        o.value = -1;
        ++live_count;
    }
    MoveOnlyPayload& operator=(MoveOnlyPayload&& o) noexcept {
        value = o.value;
        o.value = -1;
        return *this;
    }
    ~MoveOnlyPayload() noexcept { --live_count; }
};
} // namespace

int main() {
    // 1. Move-only + non-default-constructible type & non-destructive failed try_push(T&&)
    {
        hft::SpscRingBuffer<MoveOnlyPayload, 2> q;
        HFT_VERIFY(q.try_push(MoveOnlyPayload{10}));
        HFT_VERIFY(q.try_push(MoveOnlyPayload{20}));

        MoveOnlyPayload preserved{99};
        HFT_VERIFY(!q.try_push(std::move(preserved)));
        HFT_VERIFY(preserved.value == 99); // Must NOT be moved-from when queue is full!

        MoveOnlyPayload out{0};
        HFT_VERIFY(q.try_pop(out) && out.value == 10);
        // Leave one element (20) inside `q` to verify ~SpscRingBuffer() destroys it!
    }
    HFT_VERIFY(MoveOnlyPayload::live_count == 0);

    // 2. Concurrent SPSC stress test (500,000 messages)
    hft::SpscRingBuffer<std::uint64_t, 1024> ring;
    constexpr std::uint64_t kCount = 500'000;

    std::thread producer([&] {
        for (std::uint64_t i = 1; i <= kCount; ++i) {
            while (!ring.try_push(i)) {}
        }
    });

    std::thread consumer([&] {
        std::uint64_t expected = 1;
        std::uint64_t val = 0;
        while (expected <= kCount) {
            if (ring.try_pop(val)) {
                HFT_VERIFY(val == expected);
                ++expected;
            }
        }
    });

    producer.join();
    consumer.join();
    HFT_VERIFY(ring.size_approx() == 0);
    std::cout << "[PASS] Q16 SPSC Lock-Free Ring Buffer\n";
    return 0;
}
