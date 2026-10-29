#ifndef HFT_Q14_ATOMIC_RCU_PUBLISH_HPP
#define HFT_Q14_ATOMIC_RCU_PUBLISH_HPP

// ============================================================================
// QUESTION 14 [TIER 4 - HARD]: RCU IMMUTABLE SNAPSHOT PUBLISHER (ATOMIC<SHARED_PTR>)
// ============================================================================
// EXCLUSIVE PROBLEM SCOPE (NON-OVERLAPPING):
//   Concurrent reader/writer state publication (`AtomicRcuStore<T>`) for
//   large, variable-sized, or heap-referenced structures (e.g., symbol routing
//   tables, multi-leg option surfaces, risk limits) using C++20
//   `std::atomic<std::shared_ptr<const T>>` (Concurrency-Ninja Ch11 Pattern 3).
//
//   Contrast with other patterns in this suite:
//   - Q15 (`SeqLock<T>`): Lock-free optimistic reader for small `trivially_copyable`
//     structs with zero ref-count traffic.
//   - Q12 (`SharedPtr<T>`): Internals of `fetch_add`/`fetch_sub` on distinct
//     `SharedPtr` instances; NOT safe when multiple threads mutate the *same*
//     `SharedPtr` variable without Q14 (`std::atomic<std::shared_ptr<const T>>`).
//
// KEY C++20 IMPLEMENTATION DETAILS:
//   1. Non-Null Invariant:
//      `AtomicRcuStore` enforces a non-null snapshot invariant in both its
//      constructor and `publish()` so readers calling `read()` never dereference
//      `nullptr`.
//   2. Hardware vs. STL Progress Guarantee (`is_lock_free()`):
//      C++20 `std::atomic<std::shared_ptr<T>>` eliminates application-level
//      reader mutexes, though standard libraries (e.g., MSVC STL / libstdc++)
//      commonly implement it via an internal per-instance spinlock or split
//      ref-count table. Expose `is_lock_free()` so callers can inspect the
//      platform's exact progress guarantee.
// ============================================================================

#include <atomic>
#include <concepts>
#include <memory>
#include <stdexcept>
#include <utility>

namespace hft {

template <typename T>
class AtomicRcuStore {
public:
    explicit AtomicRcuStore(std::shared_ptr<const T> initial_state)
        : state_( validate_non_null(std::move(initial_state)) ) {}

    template <typename... Args>
        requires std::constructible_from<T, Args...>
    explicit AtomicRcuStore(std::in_place_t, Args&&... args)
        : state_(std::make_shared<const T>(std::forward<Args>(args)...)) {}

    AtomicRcuStore(const AtomicRcuStore&) = delete;
    AtomicRcuStore& operator=(const AtomicRcuStore&) = delete;

    // Reader path: returns an immutable shared_ptr<const T> snapshot
    [[nodiscard]] std::shared_ptr<const T> read() const noexcept {
        return state_.load(std::memory_order_acquire);
    }

    // Writer path: atomically publishes a pre-built immutable snapshot
    void publish(std::shared_ptr<const T> next_state) {
        state_.store(validate_non_null(std::move(next_state)), std::memory_order_release);
    }

    // CAS Read-Copy-Update loop: clones current state, applies `mutator(T&)`,
    // and retries if a concurrent writer published a newer snapshot first.
    template <typename Mutator>
        requires std::invocable<Mutator&, T&> && std::copy_constructible<T>
    std::shared_ptr<const T> update_cas(Mutator&& mutator) {
        std::shared_ptr<const T> expected = state_.load(std::memory_order_acquire);
        for (;;) {
            auto next = std::make_shared<T>(*expected);
            mutator(*next);
            std::shared_ptr<const T> next_const = std::move(next);
            if (state_.compare_exchange_weak(
                    expected,
                    next_const,
                    std::memory_order_acq_rel,
                    std::memory_order_acquire)) {
                return next_const;
            }
        }
    }

    [[nodiscard]] bool is_lock_free() const noexcept {
        return state_.is_lock_free();
    }

private:
    static std::shared_ptr<const T> validate_non_null(std::shared_ptr<const T> ptr) {
        if (!ptr) {
            throw std::invalid_argument("AtomicRcuStore: null snapshot pointer is forbidden");
        }
        return ptr;
    }

    std::atomic<std::shared_ptr<const T>> state_;
};

} // namespace hft

#endif // HFT_Q14_ATOMIC_RCU_PUBLISH_HPP
