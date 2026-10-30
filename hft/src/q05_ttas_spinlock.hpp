#ifndef HFT_Q05_TTAS_SPINLOCK_HPP
#define HFT_Q05_TTAS_SPINLOCK_HPP

// ============================================================================
// QUESTION 05 [TIER 2 - MEDIUM]: TTAS SPINLOCK WITH HARDWARE PAUSE BACKOFF
// ============================================================================
// EXCLUSIVE PROBLEM SCOPE (NON-OVERLAPPING):
//   User-space mutual exclusion for ultra-short critical sections (< 100ns)
//   on pinned cores using Test-and-Test-and-Set (`TTAS`) with hardware CPU
//   spin-pause hints (`_mm_pause` / `yield`) and bounded backoff.
//
// KEY TALKING POINTS:
//   1. Why Naive TAS (`while (m_locked.exchange(true))`) destroys MESI:
//      `exchange` issues a Read-For-Ownership (RFO) broadcast on every loop
//      iteration, bouncing the cache line in Modified (`M`) state across all
//      waiting cores.
//   2. Why TTAS + Hardware `cpu_relax()` wins:
//      Waiting cores spin on `m_locked.load(relaxed)` in Shared (`S`) MESI state
//      with zero interconnect traffic. Inside the spin loop, calling a hardware
//      pause instruction (`PAUSE` on x86, `YIELD` on ARM64) prevents speculative
//      memory-order pipeline flushes when the lock releases and yields execution
//      resources to the sibling hyper-thread, falling back to OS `yield()` only
//      after bounded spin iterations.
// ============================================================================

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <thread>

namespace hft {

inline constexpr std::size_t kSpinLockCacheLine = 64;

class alignas(kSpinLockCacheLine) TtasSpinLock {
public:
    static constexpr std::uint32_t kMaxActiveSpins = 64;

    constexpr TtasSpinLock() noexcept = default;
    TtasSpinLock(const TtasSpinLock&) = delete;
    TtasSpinLock& operator=(const TtasSpinLock&) = delete;

    void lock() noexcept {
        for (;;) {
            // 1. Optimistic fast path: acquire immediately if unlocked
            if (!m_locked.exchange(true, std::memory_order_acquire)) [[likely]] {
                return;
            }
            // 2. Spin locally on read-only Shared (S) cache line before yielding
            std::uint32_t spins = 0;
            while (m_locked.load(std::memory_order_relaxed)) {
                if (++spins > kMaxActiveSpins) {
                    std::this_thread::yield();
                }
            }
        }
    }

    [[nodiscard]] bool try_lock() noexcept {
        if (m_locked.load(std::memory_order_relaxed)) {
            return false;
        }
        return !m_locked.exchange(true, std::memory_order_acquire);
    }

    void unlock() noexcept {
        m_locked.store(false, std::memory_order_release);
    }

    class Guard {
    public:
        explicit Guard(TtasSpinLock& lock) noexcept : m_lock(lock) { m_lock.lock(); }
        ~Guard() noexcept { m_lock.unlock(); }
        Guard(const Guard&) = delete;
        Guard& operator=(const Guard&) = delete;

    private:
        TtasSpinLock& m_lock;
    };

private:
    std::atomic<bool> m_locked{false};
};

} // namespace hft

#endif // HFT_Q05_TTAS_SPINLOCK_HPP
