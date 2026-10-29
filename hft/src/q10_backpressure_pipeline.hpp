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
//      In `push()` and `push_for()`, `not_full_.wait()` consumes a notification
//      from a consumer. If `q_.emplace(...)` subsequently throws an exception
//      during `T`'s copy/move construction, we MUST call `not_full_.notify_one()`
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
        : cap_(capacity), policy_(policy)
    {
        if (cap_ == 0) {
            throw std::invalid_argument("BoundedChannel: capacity must be > 0");
        }
    }

    BoundedChannel(const BoundedChannel&) = delete;
    BoundedChannel& operator=(const BoundedChannel&) = delete;

    template <typename U>
    bool push(U&& item) {
        std::unique_lock lk(mu_);
        if (policy_ == OverflowPolicy::Block) {
            not_full_.wait(lk, [this] { return closed_ || q_.size() < cap_; });
            if (closed_) return false;
            try {
                q_.emplace_back(std::forward<U>(item));
            } catch (...) {
                // Relay the consumed not_full_ notification to another waiting producer!
                not_full_.notify_one();
                throw;
            }
            lk.unlock();
            not_empty_.notify_one();
            return true;
        }
        return push_nonblocking_locked(lk, std::forward<U>(item));
    }

    template <typename U>
    [[nodiscard]] bool try_push(U&& item) {
        std::unique_lock lk(mu_);
        if (closed_) return false;
        if (q_.size() >= cap_) {
            if (policy_ == OverflowPolicy::DropOldest) {
                // Construct replacement first before popping oldest for strong exception safety
                T replacement(std::forward<U>(item));
                q_.pop_front();
                q_.push_back(std::move(replacement));
                lk.unlock();
                not_empty_.notify_one();
                return true;
            }
            return false;
        }
        q_.emplace_back(std::forward<U>(item));
        lk.unlock();
        not_empty_.notify_one();
        return true;
    }

    template <typename U, typename Rep, typename Period>
    [[nodiscard]] bool push_for(U&& item, const std::chrono::duration<Rep, Period>& timeout) {
        std::unique_lock lk(mu_);
        if (policy_ == OverflowPolicy::Block) {
            if (!not_full_.wait_for(lk, timeout, [this] { return closed_ || q_.size() < cap_; })) {
                return false;
            }
            if (closed_) return false;
            try {
                q_.emplace_back(std::forward<U>(item));
            } catch (...) {
                not_full_.notify_one();
                throw;
            }
            lk.unlock();
            not_empty_.notify_one();
            return true;
        }
        return push_nonblocking_locked(lk, std::forward<U>(item));
    }

    [[nodiscard]] std::optional<T> pop() {
        std::unique_lock lk(mu_);
        not_empty_.wait(lk, [this] { return closed_ || !q_.empty(); });
        if (q_.empty()) {
            return std::nullopt;
        }
        T val = std::move(q_.front());
        q_.pop_front();
        lk.unlock();
        not_full_.notify_one();
        return val;
    }

    void close() {
        {
            std::unique_lock lk(mu_);
            closed_ = true;
        }
        not_empty_.notify_all();
        not_full_.notify_all();
    }

    [[nodiscard]] bool closed() const {
        std::unique_lock lk(mu_);
        return closed_;
    }

    [[nodiscard]] std::size_t size() const {
        std::unique_lock lk(mu_);
        return q_.size();
    }

private:
    template <typename U>
    bool push_nonblocking_locked(std::unique_lock<std::mutex>& lk, U&& item) {
        if (closed_) return false;
        if (q_.size() >= cap_) {
            if (policy_ == OverflowPolicy::DropNewest) {
                return false;
            }
            if (policy_ == OverflowPolicy::DropOldest) {
                T replacement(std::forward<U>(item));
                q_.pop_front();
                q_.push_back(std::move(replacement));
                lk.unlock();
                not_empty_.notify_one();
                return true;
            }
        }
        q_.emplace_back(std::forward<U>(item));
        lk.unlock();
        not_empty_.notify_one();
        return true;
    }

    const std::size_t       cap_;
    const OverflowPolicy    policy_;
    mutable std::mutex      mu_;
    std::condition_variable not_empty_;
    std::condition_variable not_full_;
    std::deque<T>           q_;
    bool                    closed_{false};
};

// Pattern 9 (`QaGuard`): Closes all registered channels BEFORE joining stage threads
class PipelineStageGuard {
public:
    PipelineStageGuard() = default;
    PipelineStageGuard(const PipelineStageGuard&) = delete;
    PipelineStageGuard& operator=(const PipelineStageGuard&) = delete;

    template <typename ChannelT>
    void register_channel(ChannelT& ch) {
        closers_.emplace_back([&ch]() noexcept {
            try { ch.close(); } catch (...) {}
        });
    }

    void add_worker(std::jthread&& worker) {
        workers_.push_back(std::move(worker));
    }

    ~PipelineStageGuard() noexcept {
        for (auto& close_fn : closers_) {
            close_fn();
        }
        workers_.clear(); // Joins all workers after channels are closed
    }

private:
    std::vector<std::function<void()>> closers_;
    std::vector<std::jthread>          workers_;
};

} // namespace hft

#endif // HFT_Q10_BACKPRESSURE_PIPELINE_HPP
