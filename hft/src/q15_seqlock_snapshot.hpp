#ifndef HFT_Q15_SEQLOCK_SNAPSHOT_HPP
#define HFT_Q15_SEQLOCK_SNAPSHOT_HPP

// ============================================================================
// QUESTION 15 [TIER 4 - HARD]: LOCK-FREE SEQLOCK OPTIMISTIC SNAPSHOT READER/WRITER
// ============================================================================
// EXCLUSIVE PROBLEM SCOPE (NON-OVERLAPPING):
//   Single-Writer / Multi-Reader optimistic sequence lock for broadcasting
//   small, trivially-copyable POD snapshots (e.g., Top-of-Book BBO quote,
//   index spot price, Greeks surface parameters) with zero reader cache-line
//   writes.
//   (Contrast: Q14 uses `atomic<shared_ptr<const T>>` for large heap-allocated
//    tables like `unordered_map`; Q17 maintains an append-only linked history.)
//
// KEY C++ MEMORY MODEL INVARIANTS:
//   1. Why plain `T data_` inside a Seqlock is Undefined Behavior in C++20:
//      Even if a reader discards a torn read when `seq1 != seq2`, concurrent
//      non-atomic write + non-atomic read on `T` is a C++ Data Race (UB),
//      allowing the compiler to hoist/sink loads across the sequence checks.
//      Storing the payload as an array of lock-free `std::atomic<uint64_t>`
//      words read/written with `memory_order_relaxed` eliminates the data race!
//   2. Progress Model:
//      Writer is wait-free (never blocked by readers). Readers are lock-free
//      assuming the single writer thread is pinned and not OS-preempted while
//      `seq_` is odd. Concurrent writers are NOT supported without an external
//      writer spinlock.
// ============================================================================

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace hft {

inline constexpr std::size_t kSeqlockCacheLine = 64;

template <typename T>
    requires(std::is_trivially_copyable_v<T> && std::is_nothrow_default_constructible_v<T>)
class SeqLock {
    static_assert(std::atomic<std::uint64_t>::is_always_lock_free,
                  "SeqLock requires hardware lock-free 64-bit atomics");

public:
    SeqLock() noexcept { store_words_relaxed(T{}); }
    explicit SeqLock(const T& initial) noexcept { store_words_relaxed(initial); }

    SeqLock(const SeqLock&) = delete;
    SeqLock& operator=(const SeqLock&) = delete;

    // SINGLE WRITER ONLY: Wait-free O(1) snapshot broadcast
    void write(const T& value) noexcept {
        const std::uint64_t seq = seq_.load(std::memory_order_relaxed);
        seq_.store(seq + 1, std::memory_order_release); // ODD = write in progress
        std::atomic_thread_fence(std::memory_order_release);

        store_words_relaxed(value);

        seq_.store(seq + 2, std::memory_order_release); // EVEN = stable snapshot published
    }

    // MULTI-READER: Optimistic read with zero writes to shared cache lines
    [[nodiscard]] T read() const noexcept {
        alignas(T) std::byte raw_out[sizeof(T)]{};
        for (;;) {
            const std::uint64_t seq1 = seq_.load(std::memory_order_acquire);
            if ((seq1 & 1ULL) != 0ULL) [[unlikely]] {
                continue; // Writer active; spin-retry
            }

            load_words_relaxed(raw_out);

            // Acquire fence prevents relaxed word loads from floating below seq2 load
            std::atomic_thread_fence(std::memory_order_acquire);
            const std::uint64_t seq2 = seq_.load(std::memory_order_relaxed);
            if (seq1 == seq2) [[likely]] {
                T snapshot;
                std::memcpy(&snapshot, raw_out, sizeof(T));
                return snapshot;
            }
        }
    }

private:
    static constexpr std::size_t kWordCount = (sizeof(T) + sizeof(std::uint64_t) - 1) / sizeof(std::uint64_t);

    void store_words_relaxed(const T& val) noexcept {
        std::array<std::uint64_t, kWordCount> raw{};
        std::memcpy(raw.data(), &val, sizeof(T));
        for (std::size_t i = 0; i < kWordCount; ++i) {
            words_[i].store(raw[i], std::memory_order_relaxed);
        }
    }

    void load_words_relaxed(std::byte (&out)[sizeof(T)]) const noexcept {
        std::array<std::uint64_t, kWordCount> raw{};
        for (std::size_t i = 0; i < kWordCount; ++i) {
            raw[i] = words_[i].load(std::memory_order_relaxed);
        }
        std::memcpy(out, raw.data(), sizeof(T));
    }

    alignas(kSeqlockCacheLine) std::atomic<std::uint64_t> seq_{0};
    std::array<std::atomic<std::uint64_t>, kWordCount> words_{};
};

} // namespace hft

#endif // HFT_Q15_SEQLOCK_SNAPSHOT_HPP
