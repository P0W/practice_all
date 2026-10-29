#ifndef HFT_Q13_INPLACE_FUNCTION_SBO_HPP
#define HFT_Q13_INPLACE_FUNCTION_SBO_HPP

// ============================================================================
// QUESTION 13 [TIER 4 - HARD]: SMALL-BUFFER-OPTIMIZED INPLACE_FUNCTION (ZERO HEAP)
// ============================================================================
// EXCLUSIVE PROBLEM SCOPE (NON-OVERLAPPING):
//   Move-only inline type-erased callback (`InplaceFunction<Ret(Args...), Capacity>`)
//   for event loops and order gateways where `std::function` heap allocations
//   are forbidden.
//   (Contrast: Q01 covers compile-time static polymorphism via CRTP/Concepts.)
//
// KEY C++20 CORRECTNESS DETAILS:
//   1. `std::invoke` + `if constexpr (std::is_void_v<Ret>)`:
//      Properly handles member function pointers AND allows `InplaceFunction<void()>`
//      to wrap callables that return a non-void value (discarding the return value
//       cleanly, matching `std::is_invocable_r_v<void, ...>`).
//   2. Dangling Reference Protection:
//      If `Ret` is a reference type (`std::is_reference_v<Ret>`), requires the
//      underlying callable's raw return type `RawRet` to ALSO be a reference so
//      a temporary value is never bound to a returned reference!
//   3. Accurate `noexcept` Specification:
//      The constructor is `noexcept(std::is_nothrow_constructible_v<Decayed, F&&>)`
//      rather than unconditionally `noexcept` when copying an lvalue callable.
// ============================================================================

#include <cassert>
#include <cstddef>
#include <functional>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>

namespace hft {

template <typename Signature, std::size_t Capacity = 32>
class InplaceFunction;

template <typename Ret, typename... Args, std::size_t Capacity>
class InplaceFunction<Ret(Args...), Capacity> {
public:
    constexpr InplaceFunction() noexcept = default;
    constexpr InplaceFunction(std::nullptr_t) noexcept {}

    template <typename F, typename Decayed = std::decay_t<F>>
        requires(!std::is_same_v< std::remove_cvref_t<F>, InplaceFunction > &&
                 std::is_invocable_r_v<Ret, Decayed&, Args...> &&
                 (!std::is_reference_v<Ret> || std::is_reference_v<std::invoke_result_t<Decayed&, Args...>>))
    InplaceFunction(F&& func) noexcept(std::is_nothrow_constructible_v<Decayed, F&&>) {
        static_assert(sizeof(Decayed) <= Capacity,
                      "Callable exceeds InplaceFunction inline Capacity (heap allocation forbidden)!");
        static_assert(alignof(Decayed) <= alignof(std::max_align_t),
                      "Callable alignment exceeds InplaceFunction alignment!");
        static_assert(std::is_nothrow_move_constructible_v<Decayed>,
                      "Callable must be nothrow move constructible!");

        std::construct_at(reinterpret_cast<Decayed*>(storage_), std::forward<F>(func));

        invoker_ = [](void* ptr, Args... args) -> Ret {
            auto* callable = std::launder(reinterpret_cast<Decayed*>(ptr));
            if constexpr (std::is_void_v<Ret>) {
                std::invoke(*callable, std::forward<Args>(args)...);
            } else {
                return std::invoke(*callable, std::forward<Args>(args)...);
            }
        };

        manager_ = [](void* dst, void* src) noexcept {
            auto* src_obj = std::launder(reinterpret_cast<Decayed*>(src));
            if (dst != nullptr) {
                std::construct_at(reinterpret_cast<Decayed*>(dst), std::move(*src_obj));
            }
            std::destroy_at(src_obj);
        };
    }

    InplaceFunction(const InplaceFunction&) = delete;
    InplaceFunction& operator=(const InplaceFunction&) = delete;

    InplaceFunction(InplaceFunction&& other) noexcept { move_from(std::move(other)); }

    InplaceFunction& operator=(InplaceFunction&& other) noexcept {
        if (this != &other) {
            reset();
            move_from(std::move(other));
        }
        return *this;
    }

    ~InplaceFunction() noexcept { reset(); }

    Ret operator()(Args... args) {
        assert(invoker_ != nullptr && "InplaceFunction invoked when empty!");
        return invoker_(storage_, std::forward<Args>(args)...);
    }

    [[nodiscard]] explicit constexpr operator bool() const noexcept { return invoker_ != nullptr; }

    void reset() noexcept {
        if (manager_ != nullptr) {
            manager_(nullptr, storage_);
            invoker_ = nullptr;
            manager_ = nullptr;
        }
    }

private:
    using InvokerFn = Ret (*)(void*, Args...);
    using ManagerFn = void (*)(void* dst, void* src) noexcept;

    void move_from(InplaceFunction&& other) noexcept {
        if (other.manager_ != nullptr) {
            invoker_ = other.invoker_;
            manager_ = other.manager_;
            other.manager_(storage_, other.storage_);
            other.invoker_ = nullptr;
            other.manager_ = nullptr;
        }
    }

    alignas(std::max_align_t) std::byte storage_[Capacity]{};
    InvokerFn invoker_{nullptr};
    ManagerFn manager_{nullptr};
};

} // namespace hft

#endif // HFT_Q13_INPLACE_FUNCTION_SBO_HPP
