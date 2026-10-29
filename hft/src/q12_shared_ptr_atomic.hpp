#ifndef HFT_Q12_SHARED_PTR_ATOMIC_HPP
#define HFT_Q12_SHARED_PTR_ATOMIC_HPP

// ============================================================================
// QUESTION 12 [TIER 4 - HARD]: ATOMIC REFERENCE-COUNTED SHARED_PTR INTERNALS
// ============================================================================
// EXCLUSIVE PROBLEM SCOPE (NON-OVERLAPPING):
//   Custom reference-counted smart pointer (`SharedPtr<T>`) demonstrating:
//   1. Why copy increments use `fetch_add(1, memory_order_relaxed)` while
//      destructor decrements MUST use `fetch_sub(1, memory_order_acq_rel)`.
//   2. Exception-safe adopting constructor: if `new ControlBlock` throws
//      `std::bad_alloc`, the adopted `raw_ptr` is automatically deleted so
//      it never leaks!
//   3. Thread-Safety Boundary (Distinction from Q14 AtomicRcuStore):
//      `SharedPtr<T>` guarantees safe concurrent copy/destruction of **distinct**
//      `SharedPtr<T>` instances sharing the same control block. Concurrent
//      mutation of the **same** `SharedPtr<T>` variable requires Q14
//      (`std::atomic<std::shared_ptr<const T>>`).
// ============================================================================

#include <atomic>
#include <cstddef>
#include <utility>

namespace hft {

template <typename T>
class SharedPtr {
public:
    constexpr SharedPtr() noexcept = default;
    constexpr SharedPtr(std::nullptr_t) noexcept {}

    // Exception-safe adopting constructor: deletes raw_ptr if ControlBlock allocation throws
    explicit SharedPtr(T* raw_ptr) : ptr_(raw_ptr) {
        if (ptr_ != nullptr) {
            try {
                ctrl_ = new ControlBlock{1};
            } catch (...) {
                delete ptr_;
                ptr_ = nullptr;
                throw;
            }
        }
    }

    // Copy constructor: relaxed increment is sufficient because caller already owns a live ref
    SharedPtr(const SharedPtr& other) noexcept : ptr_(other.ptr_), ctrl_(other.ctrl_) {
        if (ctrl_ != nullptr) {
            ctrl_->ref_count.fetch_add(1, std::memory_order_relaxed);
        }
    }

    // Move constructor: zero atomic traffic
    SharedPtr(SharedPtr&& other) noexcept
        : ptr_(std::exchange(other.ptr_, nullptr)),
          ctrl_(std::exchange(other.ctrl_, nullptr)) {}

    // Copy assignment (handles self-assignment cleanly via copy-and-swap)
    SharedPtr& operator=(const SharedPtr& other) noexcept {
        if (this != &other) {
            SharedPtr tmp(other);
            swap(tmp);
        }
        return *this;
    }

    // Move assignment (handles self-move cleanly)
    SharedPtr& operator=(SharedPtr&& other) noexcept {
        if (this != &other) {
            release();
            ptr_ = std::exchange(other.ptr_, nullptr);
            ctrl_ = std::exchange(other.ctrl_, nullptr);
        }
        return *this;
    }

    ~SharedPtr() noexcept { release(); }

    void swap(SharedPtr& other) noexcept {
        std::swap(ptr_, other.ptr_);
        std::swap(ctrl_, other.ctrl_);
    }

    void reset(T* new_ptr = nullptr) {
        SharedPtr tmp(new_ptr);
        swap(tmp);
    }

    [[nodiscard]] T* get() const noexcept { return ptr_; }
    [[nodiscard]] T& operator*() const noexcept { return *ptr_; }
    [[nodiscard]] T* operator->() const noexcept { return ptr_; }
    [[nodiscard]] explicit operator bool() const noexcept { return ptr_ != nullptr; }

    [[nodiscard]] std::size_t use_count() const noexcept {
        return ctrl_ != nullptr ? ctrl_->ref_count.load(std::memory_order_relaxed) : 0;
    }

private:
    struct ControlBlock {
        std::atomic<std::size_t> ref_count{1};
    };

    void release() noexcept {
        if (ctrl_ != nullptr) {
            // acq_rel ensures all prior mutations on *ptr_ across all threads
            // happen-before the final thread runs `delete ptr_`!
            if (ctrl_->ref_count.fetch_sub(1, std::memory_order_acq_rel) == 1) {
                delete ptr_;
                delete ctrl_;
            }
            ptr_ = nullptr;
            ctrl_ = nullptr;
        }
    }

    T* ptr_{nullptr};
    ControlBlock* ctrl_{nullptr};
};

} // namespace hft

#endif // HFT_Q12_SHARED_PTR_ATOMIC_HPP
