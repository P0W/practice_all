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
//      removed from `free_head_` and `used_count_` is unchanged (zero leaked
//      pool slots on constructor failure!).
//   2. Automatic Destruction of Outstanding Objects in `~FixedObjectPool()`:
//      Tracks live slots via `in_use_` flags so destroying the pool cleanly
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
            slots_[i].next_free = &slots_[i + 1];
        }
        slots_[Capacity - 1].next_free = nullptr;
        free_head_ = &slots_[0];
    }

    ~FixedObjectPool() noexcept {
        for (std::size_t i = 0; i < Capacity; ++i) {
            if (in_use_[i]) {
                T* live_ptr = std::launder(reinterpret_cast<T*>(slots_[i].storage));
                std::destroy_at(live_ptr);
                in_use_[i] = false;
            }
        }
    }

    FixedObjectPool(const FixedObjectPool&) = delete;
    FixedObjectPool& operator=(const FixedObjectPool&) = delete;

    template <typename... Args>
    [[nodiscard]] T* allocate(Args&&... args) noexcept(std::is_nothrow_constructible_v<T, Args...>) {
        if (free_head_ == nullptr) [[unlikely]] {
            return nullptr;
        }

        Slot* candidate = free_head_;
        Slot* saved_next = candidate->next_free;
        const std::size_t idx = static_cast<std::size_t>(candidate - slots_.data());

        // Construct T BEFORE committing free_head_ / used_count_ (Strong Exception Guarantee!)
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

        free_head_ = saved_next;
        in_use_[idx] = true;
        ++used_count_;
        return obj_ptr;
    }

    void deallocate(T* ptr) noexcept {
        if (ptr == nullptr) [[unlikely]] {
            return;
        }
        assert(owns(ptr) && "Pointer does not belong to this FixedObjectPool!");
        Slot* slot = reinterpret_cast<Slot*>(ptr);
        const std::size_t idx = static_cast<std::size_t>(slot - slots_.data());
        assert(in_use_[idx] && "Double-free or deallocation of unallocated pool slot!");
        if (!in_use_[idx]) [[unlikely]] {
            return;
        }

        std::destroy_at(ptr);
        in_use_[idx] = false;
        slot->next_free = free_head_; // LIFO reuse keeps L1d cache hot
        free_head_ = slot;
        --used_count_;
    }

    [[nodiscard]] bool owns(const T* ptr) const noexcept {
        if (ptr == nullptr) return false;
        const auto addr  = reinterpret_cast<std::uintptr_t>(ptr);
        const auto start = reinterpret_cast<std::uintptr_t>(slots_.data());
        const auto bytes = Capacity * sizeof(Slot);
        if (addr < start || (addr - start) >= bytes) {
            return false;
        }
        return ((addr - start) % sizeof(Slot)) == 0;
    }

    [[nodiscard]] std::size_t used() const noexcept { return used_count_; }
    [[nodiscard]] std::size_t available() const noexcept { return Capacity - used_count_; }
    [[nodiscard]] static constexpr std::size_t capacity() noexcept { return Capacity; }

private:
    union Slot {
        Slot* next_free;
        alignas(T) std::byte storage[sizeof(T)];
    };

    std::array<Slot, Capacity> slots_{};
    std::array<bool, Capacity> in_use_{};
    Slot* free_head_{nullptr};
    std::size_t used_count_{0};
};

} // namespace hft

#endif // HFT_Q11_FIXED_OBJECT_POOL_HPP
