#ifndef HFT_Q17_OBSERVABLE_HISTORY_HPP
#define HFT_Q17_OBSERVABLE_HISTORY_HPP

// ============================================================================
// QUESTION 17 [TIER 5 - VERY HARD]: OBSERVABLE PERSISTENT DAG (ZERO-ALLOC TEARDOWN)
// ============================================================================
// EXCLUSIVE PROBLEM SCOPE (NON-OVERLAPPING):
//   Wait-free-read, lock-based-append persistent immutable version chain
//   (`ObservableHistory<T>`) with synchronous observer notification OUTSIDE
//   the lock, O(1) rollback, and **zero-allocation iterative `~Node()` teardown**
//   (Concurrency-Ninja Ch11 Pattern 7).
//
// KEY C++20 FIX FOR `~Node()` (ZERO-ALLOCATION, TLS-ORDER-SAFE):
//   - Naive `std::shared_ptr<const Node> next` recurses $O(N)$ frames on the
//     call stack when a long history chain (>50,000 nodes) is dropped, causing
//     `SIGSEGV` stack overflow.
//   - Naive `thread_local std::vector<std::shared_ptr<const Node>>` inside
//     `~Node() noexcept` has two subtle bugs:
//       (a) `vector::push_back` can throw `std::bad_alloc` inside `~Node() noexcept`
//           -> `std::terminate()`!
//       (b) If a static/global `ObservableHistory` is destroyed during thread/process
//           exit after a `thread_local std::vector` is already destroyed, accessing
//           it is Undefined Behavior!
//   - **Iterative Zero-Allocation Solution**:
//     Use a trivially destructible `constinit thread_local std::shared_ptr<const Node>*`
//     pointing to a stack-local `deferred_next` cursor in the outermost `~Node()`
//     frame! When the outermost `~Node()` resets `cur.reset()`, the nested
//     `~Node()` simply moves its `next` into `*tl_drain_slot` in $O(1)$ with
//     **zero heap allocation**, **zero recursion**, and **zero non-trivial TLS**!
// ============================================================================

#include <atomic>
#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

namespace hft {

template <typename T>
class ObservableHistory {
public:
    struct Node {
        T value;
        std::shared_ptr<const Node> next;
        std::size_t version{0};

        Node(T v, std::shared_ptr<const Node> n, std::size_t ver)
            : value(std::move(v)), next(std::move(n)), version(ver) {}

        ~Node() noexcept {
            if (!next) {
                return;
            }
            // Trivially destructible TLS pointer: safe even during static/thread teardown
            static thread_local std::shared_ptr<const Node>* tl_drain_slot = nullptr;

            if (tl_drain_slot != nullptr) {
                // Nested ~Node() triggered by the active drainer on this thread:
                // Hand off `next` to the outer loop in O(1) with ZERO heap allocation!
                *tl_drain_slot = std::move(next);
                return;
            }

            // Outermost ~Node() on this thread: iteratively drain unique chain links
            std::shared_ptr<const Node> deferred;
            tl_drain_slot = &deferred;
            std::shared_ptr<const Node> cur = std::move(next);
            while (cur) {
                cur.reset(); // If cur's use_count was 1, its ~Node() moves cur->next into `deferred`
                cur = std::move(deferred);
            }
            tl_drain_slot = nullptr;
        }
    };

    using ObserverId = std::size_t;
    using Callback   = std::function<void(const T&, std::size_t)>;

    ObservableHistory() = default;
    ObservableHistory(const ObservableHistory&) = delete;
    ObservableHistory& operator=(const ObservableHistory&) = delete;

    std::size_t append(T val) {
        std::shared_ptr<const Node> new_head;
        std::vector<Callback>       cbs;
        {
            std::unique_lock lk(m_mu);
            auto cur = m_head.load(std::memory_order_relaxed);
            const std::size_t next_ver = cur ? (cur->version + 1) : 1;
            new_head = std::make_shared<const Node>(std::move(val), std::move(cur), next_ver);
            m_head.store(new_head, std::memory_order_release);

            cbs.reserve(m_observers.size());
            for (const auto& [id, cb] : m_observers) {
                cbs.push_back(cb);
            }
        }
        // Invoke observers OUTSIDE the lock so slow/re-entrant callbacks never deadlock
        for (const auto& cb : cbs) {
            cb(new_head->value, new_head->version);
        }
        return new_head->version;
    }

    // O(1) Wait-free snapshot acquisition
    [[nodiscard]] std::shared_ptr<const Node> snapshot() const noexcept {
        return m_head.load(std::memory_order_acquire);
    }

    // O(1) Rollback to an earlier snapshot node
    void rollback(std::shared_ptr<const Node> target) noexcept {
        std::unique_lock lk(m_mu);
        m_head.store(std::move(target), std::memory_order_release);
    }

    ObserverId subscribe(Callback cb) {
        std::unique_lock lk(m_mu);
        const ObserverId id = ++m_next_observer_id;
        m_observers.emplace_back(id, std::move(cb));
        return id;
    }

    bool unsubscribe(ObserverId id) {
        std::unique_lock lk(m_mu);
        const auto erased = std::erase_if(m_observers, [id](const auto& pair) {
            return pair.first == id;
        });
        return erased > 0;
    }

private:
    mutable std::mutex                             m_mu;
    std::atomic<std::shared_ptr<const Node>>       m_head{nullptr};
    ObserverId                                     m_next_observer_id{0};
    std::vector<std::pair<ObserverId, Callback>>   m_observers;
};

} // namespace hft

#endif // HFT_Q17_OBSERVABLE_HISTORY_HPP
