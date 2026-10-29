#ifndef HFT_Q07_FUTURE_THEN_CONTINUATION_HPP
#define HFT_Q07_FUTURE_THEN_CONTINUATION_HPP

// ============================================================================
// QUESTION 07 [TIER 2 - MEDIUM]: EXCEPTION-PROPAGATING FUTURE CONTINUATION (then)
// ============================================================================
// EXCLUSIVE PROBLEM SCOPE (NON-OVERLAPPING):
//   Composable `then(std::future<T>, F)` continuation adapter bridging C++20's
//   lack of native `std::future::then` with full `void` and non-`void` return
//   support and automatic exception propagation across chain stages
//   (Concurrency-Ninja Ch11 Pattern 4).
//
// KEY C++20 ARCHITECTURAL TRADEOFFS (IMPORTANT INTERVIEW DISCUSSION):
//   1. Why `fut.get()` is called inside the `std::async` body:
//      If the upstream future holds an exception (`std::exception_ptr`),
//      calling `fut.get()` inside `std::async` rethrows it inside the async
//      worker, which `std::async`'s shared state catches and stores into the
//      returned downstream `std::future<R>` automatically!
//   2. C++ Standard `[futures.async]/5` Caveat:
//      Each `std::async(std::launch::async, ...)` creates an OS/runtime task
//      whose returned `std::future` blocks in its destructor if discarded
//      without `.get()` / `.share()`. This pattern is designed for coarse-grained
//      off-hot-path control-plane workflows (e.g., config fetch -> parse -> warm
//      cache); for high-rate task graphs, use Q09 (`ThreadPool`) instead.
// ============================================================================

#include <functional>
#include <future>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace hft {

// Overload 1: Non-void upstream std::future<T>
template <typename T, typename F>
    requires(!std::is_void_v<T> && std::invocable<std::decay_t<F>, T>)
[[nodiscard]] auto then(std::future<T> fut, F&& fn)
    -> std::future<std::invoke_result_t<std::decay_t<F>, T>>
{
    if (!fut.valid()) {
        throw std::invalid_argument("hft::then: input std::future<T> has no shared state");
    }
    using R = std::invoke_result_t<std::decay_t<F>, T>;
    return std::async(
        std::launch::async,
        [f = std::move(fut), func = std::forward<F>(fn)]() mutable -> R {
            if constexpr (std::is_void_v<R>) {
                std::invoke(std::move(func), f.get());
            } else {
                return std::invoke(std::move(func), f.get());
            }
        });
}

// Overload 2: Void upstream std::future<void>
template <typename F>
    requires std::invocable<std::decay_t<F>>
[[nodiscard]] auto then(std::future<void> fut, F&& fn)
    -> std::future<std::invoke_result_t<std::decay_t<F>>>
{
    if (!fut.valid()) {
        throw std::invalid_argument("hft::then: input std::future<void> has no shared state");
    }
    using R = std::invoke_result_t<std::decay_t<F>>;
    return std::async(
        std::launch::async,
        [f = std::move(fut), func = std::forward<F>(fn)]() mutable -> R {
            f.get(); // Propagates any upstream exception into the downstream future
            if constexpr (std::is_void_v<R>) {
                std::invoke(std::move(func));
            } else {
                return std::invoke(std::move(func));
            }
        });
}

} // namespace hft

#endif // HFT_Q07_FUTURE_THEN_CONTINUATION_HPP
