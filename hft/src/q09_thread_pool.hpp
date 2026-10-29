#ifndef HFT_Q09_THREAD_POOL_HPP
#define HFT_Q09_THREAD_POOL_HPP

// ============================================================================
// QUESTION 09 [TIER 3 - MEDIUM-HARD]: FIXED-WORKER THREAD POOL & SHUTDOWN POLICIES
// ============================================================================
// EXCLUSIVE PROBLEM SCOPE (NON-OVERLAPPING):
//   General-purpose off-hot-path task executor (`ThreadPool`) using `std::jthread`
//   and `std::condition_variable_any`, supporting both **Drain-Queued** and
//   **Discard-Queued** shutdown policies (Concurrency-Ninja Ch11 Pattern 2).
//   (Contrast: Q07 is a lightweight `std::future` continuation adapter.)
//
// KEY LIFETIME INVARIANTS:
//   1. Reverse Member Destruction Order:
//      `workers_` is declared LAST so `~ThreadPool()` joins all `std::jthread`
//      workers BEFORE `mu_`, `cv_`, `q_`, and `stop_` are destroyed.
//   2. Explicit Shutdown Policy (`DrainQueued` vs. `DiscardQueued`):
//      - `DrainQueued` (default): completes all accepted tasks in `q_` before
//        workers exit.
//      - `DiscardQueued`: exits immediately after in-flight tasks finish;
//        unrun queued tasks break their `std::packaged_task` promises cleanly.
// ============================================================================

#include <condition_variable>
#include <cstddef>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace hft {

enum class PoolShutdownPolicy : unsigned char {
    DrainQueued = 0,   // Complete all queued tasks before worker exit
    DiscardQueued = 1  // Exit immediately; queued futures throw broken_promise
};

class ThreadPool {
public:
    explicit ThreadPool(std::size_t num_workers,
                        PoolShutdownPolicy policy = PoolShutdownPolicy::DrainQueued)
        : policy_(policy)
    {
        if (num_workers == 0) {
            throw std::invalid_argument("ThreadPool: num_workers must be > 0");
        }
        workers_.reserve(num_workers);
        for (std::size_t i = 0; i < num_workers; ++i) {
            workers_.emplace_back([this](std::stop_token st) { worker_loop(st); });
        }
    }

    ~ThreadPool() noexcept {
        {
            std::unique_lock lk(mu_);
            stop_ = true;
        }
        cv_.notify_all();
        workers_.clear(); // Joins all worker jthreads while mu_/cv_/q_ remain valid
    }

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    template <typename F, typename... Args>
    [[nodiscard]] auto submit(F&& f, Args&&... args)
        -> std::future<std::invoke_result_t<std::decay_t<F>, std::decay_t<Args>...>>
    {
        using R = std::invoke_result_t<std::decay_t<F>, std::decay_t<Args>...>;

        auto bound = [fn = std::forward<F>(f), ... a = std::forward<Args>(args)]() mutable -> R {
            return std::invoke(std::move(fn), std::move(a)...);
        };

        auto task = std::make_shared<std::packaged_task<R()>>(std::move(bound));
        auto fut  = task->get_future();

        {
            std::unique_lock lk(mu_);
            if (stop_) {
                throw std::runtime_error("ThreadPool: cannot submit while shutting down");
            }
            q_.push([task]() { (*task)(); });
        }
        cv_.notify_one();
        return fut;
    }

    [[nodiscard]] std::size_t pending_count() const {
        std::unique_lock lk(mu_);
        return q_.size();
    }

private:
    void worker_loop(std::stop_token st) {
        for (;;) {
            std::function<void()> task;
            {
                std::unique_lock lk(mu_);
                if (!cv_.wait(lk, st, [this] { return stop_ || !q_.empty(); })) {
                    // Interrupted by external stop_token (e.g., constructor throw unwind)
                    if (policy_ == PoolShutdownPolicy::DiscardQueued || q_.empty()) {
                        return;
                    }
                }
                if (stop_) {
                    if (policy_ == PoolShutdownPolicy::DiscardQueued || q_.empty()) {
                        return;
                    }
                }
                if (q_.empty()) {
                    continue;
                }
                task = std::move(q_.front());
                q_.pop();
            }
            task();
        }
    }

    const PoolShutdownPolicy          policy_;
    mutable std::mutex                mu_;
    std::condition_variable_any       cv_;
    std::queue<std::function<void()>> q_;
    bool                              stop_{false};
    std::vector<std::jthread>         workers_; // MUST BE LAST
};

} // namespace hft

#endif // HFT_Q09_THREAD_POOL_HPP
