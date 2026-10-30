#ifndef HFT_Q10_BACKPRESSURE_PIPELINE_HPP
#define HFT_Q10_BACKPRESSURE_PIPELINE_HPP

// ============================================================================
// QUESTION 10 [TIER 3 - MEDIUM-HARD]: BOUNDED BACKPRESSURE CHANNEL & PIPELINE GUARD
// ============================================================================
// EXCLUSIVE PROBLEM SCOPE (NON-OVERLAPPING):
//   Blocking condition-variable bounded channel (`BoundedChannel<T>`) with
//   explicit **Overflow Policy** (`Block`, `DropNewest`, `DropOldest`),
//   paired with an exception-safe RAII **`PipelineStageGuard`** (Concurrency-Ninja
//   Ch11 Patterns 1, 5 & 9).
//   (Contrast: Q16/Q20 are non-blocking lock-free spin queues.)
//
// KEY EXCEPTION & CONCURRENCY INVARIANTS:
//   1. Lost-Wakeup Prevention on Throwing `T` Construction:
//      In `push()` and `push_for()`, `m_not_full.wait()` consumes a notification
//      from a consumer. If `m_q.emplace(...)` subsequently throws an exception
//      during `T`'s copy/move construction, we MUST call `m_not_full.notify_one()`
//      inside `catch (...)` before rethrowing so another waiting producer is
//      not permanently stranded!
//   2. Accurate Exception Specifications:
//      Methods that lock `std::mutex` (`close()`, `closed()`, `size()`) are not
//      marked `noexcept` because `std::mutex::lock()` can throw `std::system_error`.
//   3. `PipelineStageGuard` (Pattern 9 `QaGuard` equivalent):
//      Guarantees that if a multi-stage pipeline throws midway, `close()` is
//      called on all stage channels BEFORE their worker `std::jthread`s join,
//      preventing infinite deadlock during stack unwinding!
// ============================================================================

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

namespace hft {

enum class OverflowPolicy : unsigned char {
    Block = 0,       // Wait until space is available or channel closes
    DropNewest = 1,  // Discard the incoming item immediately when full
    DropOldest = 2   // Evict the oldest buffered item to admit the incoming item
};

template <typename T>
class BoundedChannel {
public:
    explicit BoundedChannel(std::size_t capacity, OverflowPolicy policy = OverflowPolicy::Block)
        : m_cap(capacity), m_policy(policy)
    {
        if (m_cap == 0) {
            throw std::invalid_argument("BoundedChannel: capacity must be > 0");
        }
    }

    BoundedChannel(const BoundedChannel&) = delete;
    BoundedChannel& operator=(const BoundedChannel&) = delete;

    template <typename U>
    bool push(U&& item) {
        std::unique_lock lk(m_mu);
        if (m_policy == OverflowPolicy::Block) {
            m_not_full.wait(lk, [this] { return m_closed || m_q.size() < m_cap; });
            if (m_closed) return false;
            try {
                m_q.emplace_back(std::forward<U>(item));
            } catch (...) {
                // Relay the consumed m_not_full notification to another waiting producer!
                m_not_full.notify_one();
                throw;
            }
            lk.unlock();
            m_not_empty.notify_one();
            return true;
        }
        return push_nonblocking_locked(lk, std::forward<U>(item));
    }

    template <typename U>
    [[nodiscard]] bool try_push(U&& item) {
        std::unique_lock lk(m_mu);
        if (m_closed) return false;
        if (m_q.size() >= m_cap) {
            if (m_policy == OverflowPolicy::DropOldest) {
                // Construct replacement first before popping oldest for strong exception safety
                T replacement(std::forward<U>(item));
                m_q.pop_front();
                m_q.push_back(std::move(replacement));
                lk.unlock();
                m_not_empty.notify_one();
                return true;
            }
            return false;
        }
        m_q.emplace_back(std::forward<U>(item));
        lk.unlock();
        m_not_empty.notify_one();
        return true;
    }

    template <typename U, typename Rep, typename Period>
    [[nodiscard]] bool push_for(U&& item, const std::chrono::duration<Rep, Period>& timeout) {
        std::unique_lock lk(m_mu);
        if (m_policy == OverflowPolicy::Block) {
            if (!m_not_full.wait_for(lk, timeout, [this] { return m_closed || m_q.size() < m_cap; })) {
                return false;
            }
            if (m_closed) return false;
            try {
                m_q.emplace_back(std::forward<U>(item));
            } catch (...) {
                m_not_full.notify_one();
                throw;
            }
            lk.unlock();
            m_not_empty.notify_one();
            return true;
        }
        return push_nonblocking_locked(lk, std::forward<U>(item));
    }

    [[nodiscard]] std::optional<T> pop() {
        std::unique_lock lk(m_mu);
        m_not_empty.wait(lk, [this] { return m_closed || !m_q.empty(); });
        if (m_q.empty()) {
            return std::nullopt;
        }
        T val = std::move(m_q.front());
        m_q.pop_front();
        lk.unlock();
        m_not_full.notify_one();
        return val;
    }

    void close() {
        {
            std::unique_lock lk(m_mu);
            m_closed = true;
        }
        m_not_empty.notify_all();
        m_not_full.notify_all();
    }

    [[nodiscard]] bool closed() const {
        std::unique_lock lk(m_mu);
        return m_closed;
    }

    [[nodiscard]] std::size_t size() const {
        std::unique_lock lk(m_mu);
        return m_q.size();
    }

private:
    template <typename U>
    bool push_nonblocking_locked(std::unique_lock<std::mutex>& lk, U&& item) {
        if (m_closed) return false;
        if (m_q.size() >= m_cap) {
            if (m_policy == OverflowPolicy::DropNewest) {
                return false;
            }
            if (m_policy == OverflowPolicy::DropOldest) {
                T replacement(std::forward<U>(item));
                m_q.pop_front();
                m_q.push_back(std::move(replacement));
                lk.unlock();
                m_not_empty.notify_one();
                return true;
            }
        }
        m_q.emplace_back(std::forward<U>(item));
        lk.unlock();
        m_not_empty.notify_one();
        return true;
    }

    const std::size_t       m_cap;
    const OverflowPolicy    m_policy;
    mutable std::mutex      m_mu;
    std::condition_variable m_not_empty;
    std::condition_variable m_not_full;
    std::deque<T>           m_q;
    bool                    m_closed{false};
};

// Pattern 9 (`QaGuard`): Closes all registered channels BEFORE joining stage threads
class PipelineStageGuard {
public:
    PipelineStageGuard() = default;
    PipelineStageGuard(const PipelineStageGuard&) = delete;
    PipelineStageGuard& operator=(const PipelineStageGuard&) = delete;

    template <typename ChannelT>
    void register_channel(ChannelT& ch) {
        m_closers.emplace_back([&ch]() noexcept {
            try { ch.close(); } catch (...) {}
        });
    }

    void add_worker(std::jthread&& worker) {
        m_workers.push_back(std::move(worker));
    }

    ~PipelineStageGuard() noexcept {
        for (auto& close_fn : m_closers) {
            close_fn();
        }
        m_workers.clear(); // Joins all workers after channels are closed
    }

private:
    std::vector<std::function<void()>> m_closers;
    std::vector<std::jthread>          m_workers;
};

} // namespace hft

#endif // HFT_Q10_BACKPRESSURE_PIPELINE_HPP
