#ifndef HFT_Q16_SPSC_RING_BUFFER_HPP
#define HFT_Q16_SPSC_RING_BUFFER_HPP

// ============================================================================
// QUESTION 16 [TIER 5 - VERY HARD]: LOCK-FREE SPSC RING BUFFER (SHADOW INDICES)
// ============================================================================
// EXCLUSIVE PROBLEM SCOPE (NON-OVERLAPPING):
//   Single-Producer Single-Consumer (1:1) wait-free ring buffer with:
//   1. Raw uninitialized slot storage (`alignas(T) std::byte`) managed via
//      `std::construct_at` / `std::destroy_at` / `std::launder` so `T` does
//      NOT need to be default-constructible and popped items are destroyed
//      immediately (matching Concurrency-Ninja Ch11 Pattern 1).
//   2. Non-destructive failed push: `try_push(T&&)` only moves from `item`
//      AFTER verifying a slot is free.
//   3. Shadow-index caching (`m_read_idx_cache`, `m_write_idx_cache`) on separate
//      64-byte cache lines to eliminate false sharing and cross-core L1 invalidation.
//   4. Supports over-aligned types (`alignof(T) > 64`) via `std::max`.
//   (Contrast: Q20 covers MPMC per-slot sequence CAS; Q10 covers blocking back-pressure.)
// ============================================================================

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <memory>
#include <new>
#include <optional>
#include <type_traits>
#include <utility>

namespace hft {

inline constexpr std::size_t kCacheLineSize = 64;

template <typename T, std::size_t Capacity>
    requires(Capacity >= 2 && ((Capacity & (Capacity - 1)) == 0) && std::is_nothrow_move_constructible_v<T>)
class SpscRingBuffer {
public:
    static constexpr std::size_t kMask = Capacity - 1;
    static constexpr std::size_t kSlotAlign = std::max(kCacheLineSize, alignof(T));

    constexpr SpscRingBuffer() noexcept = default;

    ~SpscRingBuffer() noexcept {
        std::size_t r = m_read_idx.load(std::memory_order_relaxed);
        const std::size_t w = m_write_idx.load(std::memory_order_relaxed);
        while (r != w) {
            std::destroy_at(slot_ptr(r));
            ++r;
        }
    }

    SpscRingBuffer(const SpscRingBuffer&) = delete;
    SpscRingBuffer& operator=(const SpscRingBuffer&) = delete;

    // PRODUCER ONLY — Wait-free O(1) copy push
    [[nodiscard]] bool try_push(const T& item) noexcept(std::is_nothrow_copy_constructible_v<T>)
        requires std::is_copy_constructible_v<T>
    {
        const std::size_t w = m_write_idx.load(std::memory_order_relaxed);
        if (!has_space(w)) [[unlikely]] {
            return false;
        }
        std::construct_at(reinterpret_cast<T*>(m_slots[w & kMask].storage), item);
        m_write_idx.store(w + 1, std::memory_order_release);
        return true;
    }

    // PRODUCER ONLY — Wait-free O(1) move push (leaves `item` intact if full!)
    [[nodiscard]] bool try_push(T&& item) noexcept {
        const std::size_t w = m_write_idx.load(std::memory_order_relaxed);
        if (!has_space(w)) [[unlikely]] {
            return false; // Full: `item` is NOT moved from!
        }
        std::construct_at(reinterpret_cast<T*>(m_slots[w & kMask].storage), std::move(item));
        m_write_idx.store(w + 1, std::memory_order_release);
        return true;
    }

    // CONSUMER ONLY — Wait-free O(1) pop into out-parameter
    [[nodiscard]] bool try_pop(T& out_item) noexcept(std::is_nothrow_move_assignable_v<T>)
        requires std::is_move_assignable_v<T>
    {
        auto opt = try_pop();
        if (!opt.has_value()) {
            return false;
        }
        out_item = std::move(*opt);
        return true;
    }

    // CONSUMER ONLY — Wait-free O(1) pop returning std::optional<T> (supports non-default-constructible T)
    [[nodiscard]] std::optional<T> try_pop() noexcept {
        const std::size_t r = m_read_idx.load(std::memory_order_relaxed);
        if (r == m_write_idx_cache) [[unlikely]] {
            m_write_idx_cache = m_write_idx.load(std::memory_order_acquire);
            if (r == m_write_idx_cache) [[unlikely]] {
                return std::nullopt;
            }
        }

        T* ptr = slot_ptr(r);
        std::optional<T> result(std::move(*ptr));
        std::destroy_at(ptr);
        m_read_idx.store(r + 1, std::memory_order_release);
        return result;
    }

    // Approximate size (clamped against concurrent wrap/underflow)
    [[nodiscard]] std::size_t size_approx() const noexcept {
        const std::size_t r = m_read_idx.load(std::memory_order_acquire);
        const std::size_t w = m_write_idx.load(std::memory_order_acquire);
        const std::size_t diff = w - r;
        return diff <= Capacity ? diff : 0;
    }

    [[nodiscard]] static constexpr std::size_t capacity() noexcept { return Capacity; }

private:
    struct alignas(alignof(T)) RawSlot {
        alignas(T) std::byte storage[sizeof(T)];
    };

    [[nodiscard]] bool has_space(std::size_t w) noexcept {
        if (w - m_read_idx_cache == Capacity) [[unlikely]] {
            m_read_idx_cache = m_read_idx.load(std::memory_order_acquire);
            if (w - m_read_idx_cache == Capacity) [[unlikely]] {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] T* slot_ptr(std::size_t idx) noexcept {
        return std::launder(reinterpret_cast<T*>(m_slots[idx & kMask].storage));
    }

    alignas(kCacheLineSize) std::atomic<std::size_t> m_write_idx{0};
    std::size_t m_read_idx_cache{0};

    alignas(kCacheLineSize) std::atomic<std::size_t> m_read_idx{0};
    std::size_t m_write_idx_cache{0};

    alignas(kSlotAlign) std::array<RawSlot, Capacity> m_slots{};
};

} // namespace hft

#endif // HFT_Q16_SPSC_RING_BUFFER_HPP
