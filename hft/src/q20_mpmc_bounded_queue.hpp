#ifndef HFT_Q20_MPMC_BOUNDED_QUEUE_HPP
#define HFT_Q20_MPMC_BOUNDED_QUEUE_HPP

// ============================================================================
// QUESTION 20 [TIER 5 - HARDEST]: LOCK-FREE MPMC BOUNDED QUEUE (VYUKOV TURN TICKETS)
// ============================================================================
// EXCLUSIVE PROBLEM SCOPE (NON-OVERLAPPING):
//   Multi-Producer Multi-Consumer (N:M) non-blocking bounded ring buffer using
//   per-slot atomic ticket sequence numbers (`sequence`) to separate
//   **ticket reservation** from **slot publication** and prevent ABA across laps.
//   (Contrast: Q16 is 1:1 SPSC with shadow indices; Q10 is a mutex/CV blocking
//    MPMC pipeline queue with back-pressure and stream close/drain.)
//
// KEY NUANCES & PROGRESS GUARANTEES:
//   1. Wrap-Safe Modular Comparison:
//      Never cast `seq` and `pos` to `intptr_t` BEFORE subtracting (`INT64_MAX - INT64_MIN`
//      is signed overflow UB!). Subtract in unsigned `std::size_t` first:
//      `static_cast<std::intptr_t>(seq - pos)` uses well-defined 2's-complement modulo
//      subtraction (valid as long as ticket distance < 2^63).
//   2. Progress Model (Lock-Free vs. Preempted Ticket Holder):
//      Strictly speaking, if a producer claims ticket `pos` via CAS and is OS-preempted
//      before storing `sequence = pos + 1`, consumers reaching `pos` cannot advance past
//      that slot until the producer resumes. In HFT, threads are pinned (`isolcpus`) to
//      dedicated cores so ticket holders are never preempted mid-store.
//   3. Raw Storage & Nothrow Post-Reservation Contract:
//      Once a producer claims ticket `pos`, construction into `storage` MUST NOT throw
//      (otherwise the slot would be permanently stranded!). Hence `try_push` requires
//      `is_nothrow_move_constructible_v<T>` (and `is_nothrow_copy_constructible_v<T>`
//      for the `const T&` overload).
// ============================================================================

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <optional>
#include <type_traits>
#include <utility>

namespace hft {

inline constexpr std::size_t kMpmcCacheLine = 64;

template <typename T, std::size_t Capacity>
    requires(Capacity >= 2 && ((Capacity & (Capacity - 1)) == 0) && std::is_nothrow_move_constructible_v<T>)
class MpmcBoundedQueue {
public:
    static constexpr std::size_t kMask = Capacity - 1;
    static constexpr std::size_t kSlotAlign = std::max(kMpmcCacheLine, alignof(T));

    MpmcBoundedQueue() noexcept {
        for (std::size_t i = 0; i < Capacity; ++i) {
            m_slots[i].sequence.store(i, std::memory_order_relaxed);
        }
    }

    ~MpmcBoundedQueue() noexcept {
        // Drain and destroy any remaining constructed elements on teardown
        while (try_pop().has_value()) {}
    }

    MpmcBoundedQueue(const MpmcBoundedQueue&) = delete;
    MpmcBoundedQueue& operator=(const MpmcBoundedQueue&) = delete;

    [[nodiscard]] bool try_push(const T& data) noexcept
        requires std::is_nothrow_copy_constructible_v<T>
    {
        return emplace_impl(data);
    }

    [[nodiscard]] bool try_push(T&& data) noexcept {
        return emplace_impl(std::move(data));
    }

    [[nodiscard]] bool try_pop(T& out_data) noexcept(std::is_nothrow_move_assignable_v<T>)
        requires std::is_move_assignable_v<T>
    {
        auto opt = try_pop();
        if (!opt.has_value()) {
            return false;
        }
        out_data = std::move(*opt);
        return true;
    }

    [[nodiscard]] std::optional<T> try_pop() noexcept {
        Slot* slot = nullptr;
        std::size_t pos = m_dequeue_pos.load(std::memory_order_relaxed);

        for (;;) {
            slot = &m_slots[pos & kMask];
            const std::size_t seq = slot->sequence.load(std::memory_order_acquire);
            // Unsigned subtraction first -> well-defined modular distance without signed overflow UB
            const auto diff = static_cast<std::intptr_t>(seq - (pos + 1));

            if (diff == 0) [[likely]] {
                if (m_dequeue_pos.compare_exchange_weak(
                        pos, pos + 1, std::memory_order_relaxed, std::memory_order_relaxed)) {
                    break;
                }
            } else if (diff < 0) {
                return std::nullopt; // Slot not yet published (queue empty or producer still writing)
            } else {
                pos = m_dequeue_pos.load(std::memory_order_relaxed);
            }
        }

        T* ptr = std::launder(reinterpret_cast<T*>(slot->storage));
        std::optional<T> result(std::move(*ptr));
        std::destroy_at(ptr);
        slot->sequence.store(pos + Capacity, std::memory_order_release);
        return result;
    }

private:
    template <typename U>
    [[nodiscard]] bool emplace_impl(U&& arg) noexcept {
        Slot* slot = nullptr;
        std::size_t pos = m_enqueue_pos.load(std::memory_order_relaxed);

        for (;;) {
            slot = &m_slots[pos & kMask];
            const std::size_t seq = slot->sequence.load(std::memory_order_acquire);
            // Unsigned subtraction first -> well-defined modular distance
            const auto diff = static_cast<std::intptr_t>(seq - pos);

            if (diff == 0) [[likely]] {
                if (m_enqueue_pos.compare_exchange_weak(
                        pos, pos + 1, std::memory_order_relaxed, std::memory_order_relaxed)) {
                    break;
                }
            } else if (diff < 0) {
                return false; // Queue full: `arg` is untouched!
            } else {
                pos = m_enqueue_pos.load(std::memory_order_relaxed);
            }
        }

        std::construct_at(reinterpret_cast<T*>(slot->storage), std::forward<U>(arg));
        slot->sequence.store(pos + 1, std::memory_order_release);
        return true;
    }

    struct alignas(kSlotAlign) Slot {
        std::atomic<std::size_t> sequence{0};
        alignas(T) std::byte storage[sizeof(T)];
    };

    alignas(kSlotAlign) std::array<Slot, Capacity> m_slots{};
    alignas(kMpmcCacheLine) std::atomic<std::size_t> m_enqueue_pos{0};
    alignas(kMpmcCacheLine) std::atomic<std::size_t> m_dequeue_pos{0};
};

} // namespace hft

#endif // HFT_Q20_MPMC_BOUNDED_QUEUE_HPP
