#ifndef HFT_Q11_FIXED_OBJECT_POOL_HPP
#define HFT_Q11_FIXED_OBJECT_POOL_HPP

// ============================================================================
// QUESTION 11 [TIER 3 - MEDIUM-HARD]: O(1) FIXED-BLOCK SLAB OBJECT POOL
// ============================================================================
// EXCLUSIVE PROBLEM SCOPE (NON-OVERLAPPING):
//   Single-threaded, deterministic $O(1)$ pre-allocated object pool using an
//   intrusive LIFO free-list over raw aligned storage.
//   (Contrast: Q04 covers intrusive doubly-linked FIFO lists; Q19 composes Q11
//    + Q04 into an L3 Limit Order Book.)
//
// KEY SAFETY & LIFETIME INVARIANTS:
//   1. Strong Exception Safety on `allocate(args...)`:
//      If `T(args...)` throws during `std::construct_at`, the slot is NOT
//      removed from `m_free_head` and `m_used_count` is unchanged (zero leaked
//      pool slots on constructor failure!).
//   2. Automatic Destruction of Outstanding Objects in `~FixedObjectPool()`:
//      Tracks live slots via `m_in_use` flags so destroying the pool cleanly
//      invokes `std::destroy_at` on any still-allocated objects AND detects
//      invalid/double-free calls in `deallocate()`.
//   3. Strict Address + Alignment Validation in `owns(ptr)`:
//      Uses `std::uintptr_t` arithmetic (avoiding UB from relational `<` comparisons
//      on unrelated pointers) and verifies exact `sizeof(Slot)` stride alignment!
// ============================================================================

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>

namespace hft {

template <typename T, std::size_t Capacity>
    requires(Capacity > 0)
class FixedObjectPool {
public:
    FixedObjectPool() noexcept {
        for (std::size_t i = 0; i < Capacity - 1; ++i) {
            m_slots[i].next_free = &m_slots[i + 1];
        }
        m_slots[Capacity - 1].next_free = nullptr;
        m_free_head = &m_slots[0];
    }

    ~FixedObjectPool() noexcept {
        for (std::size_t i = 0; i < Capacity; ++i) {
            if (m_in_use[i]) {
                T* live_ptr = std::launder(reinterpret_cast<T*>(m_slots[i].storage));
                std::destroy_at(live_ptr);
                m_in_use[i] = false;
            }
        }
    }

    FixedObjectPool(const FixedObjectPool&) = delete;
    FixedObjectPool& operator=(const FixedObjectPool&) = delete;

    template <typename... Args>
    [[nodiscard]] T* allocate(Args&&... args) noexcept(std::is_nothrow_constructible_v<T, Args...>) {
        if (m_free_head == nullptr) [[unlikely]] {
            return nullptr;
        }

        Slot* candidate = m_free_head;
        Slot* saved_next = candidate->next_free;
        const std::size_t idx = static_cast<std::size_t>(candidate - m_slots.data());

        // Construct T BEFORE committing m_free_head / m_used_count (Strong Exception Guarantee!)
        T* obj_ptr = nullptr;
        if constexpr (std::is_nothrow_constructible_v<T, Args...>) {
            obj_ptr = std::construct_at(reinterpret_cast<T*>(candidate->storage), std::forward<Args>(args)...);
        } else {
            try {
                obj_ptr = std::construct_at(reinterpret_cast<T*>(candidate->storage), std::forward<Args>(args)...);
            } catch (...) {
                candidate->next_free = saved_next;
                throw;
            }
        }

        m_free_head = saved_next;
        m_in_use[idx] = true;
        ++m_used_count;
        return obj_ptr;
    }

    void deallocate(T* ptr) noexcept {
        if (ptr == nullptr) [[unlikely]] {
            return;
        }
        assert(owns(ptr) && "Pointer does not belong to this FixedObjectPool!");
        Slot* slot = reinterpret_cast<Slot*>(ptr);
        const std::size_t idx = static_cast<std::size_t>(slot - m_slots.data());
        assert(m_in_use[idx] && "Double-free or deallocation of unallocated pool slot!");
        if (!m_in_use[idx]) [[unlikely]] {
            return;
        }

        std::destroy_at(ptr);
        m_in_use[idx] = false;
        slot->next_free = m_free_head; // LIFO reuse keeps L1d cache hot
        m_free_head = slot;
        --m_used_count;
    }

    [[nodiscard]] bool owns(const T* ptr) const noexcept {
        if (ptr == nullptr) return false;
        const auto addr  = reinterpret_cast<std::uintptr_t>(ptr);
        const auto start = reinterpret_cast<std::uintptr_t>(m_slots.data());
        const auto bytes = Capacity * sizeof(Slot);
        if (addr < start || (addr - start) >= bytes) {
            return false;
        }
        return ((addr - start) % sizeof(Slot)) == 0;
    }

    [[nodiscard]] std::size_t used() const noexcept { return m_used_count; }
    [[nodiscard]] std::size_t available() const noexcept { return Capacity - m_used_count; }
    [[nodiscard]] static constexpr std::size_t capacity() noexcept { return Capacity; }

private:
    union Slot {
        Slot* next_free;
        alignas(T) std::byte storage[sizeof(T)];
    };

    std::array<Slot, Capacity> m_slots{};
    std::array<bool, Capacity> m_in_use{};
    Slot* m_free_head{nullptr};
    std::size_t m_used_count{0};
};

} // namespace hft

#endif // HFT_Q11_FIXED_OBJECT_POOL_HPP
