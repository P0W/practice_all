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
    explicit SharedPtr(T* raw_ptr) : m_ptr(raw_ptr) {
        if (m_ptr != nullptr) {
            try {
                m_ctrl = new ControlBlock{1};
            } catch (...) {
                delete m_ptr;
                m_ptr = nullptr;
                throw;
            }
        }
    }

    // Copy constructor: relaxed increment is sufficient because caller already owns a live ref
    SharedPtr(const SharedPtr& other) noexcept : m_ptr(other.m_ptr), m_ctrl(other.m_ctrl) {
        if (m_ctrl != nullptr) {
            m_ctrl->ref_count.fetch_add(1, std::memory_order_relaxed);
        }
    }

    // Move constructor: zero atomic traffic
    SharedPtr(SharedPtr&& other) noexcept
        : m_ptr(std::exchange(other.m_ptr, nullptr)),
          m_ctrl(std::exchange(other.m_ctrl, nullptr)) {}

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
            m_ptr = std::exchange(other.m_ptr, nullptr);
            m_ctrl = std::exchange(other.m_ctrl, nullptr);
        }
        return *this;
    }

    ~SharedPtr() noexcept { release(); }

    void swap(SharedPtr& other) noexcept {
        std::swap(m_ptr, other.m_ptr);
        std::swap(m_ctrl, other.m_ctrl);
    }

    void reset(T* new_ptr = nullptr) {
        SharedPtr tmp(new_ptr);
        swap(tmp);
    }

    [[nodiscard]] T* get() const noexcept { return m_ptr; }
    [[nodiscard]] T& operator*() const noexcept { return *m_ptr; }
    [[nodiscard]] T* operator->() const noexcept { return m_ptr; }
    [[nodiscard]] explicit operator bool() const noexcept { return m_ptr != nullptr; }

    [[nodiscard]] std::size_t use_count() const noexcept {
        return m_ctrl != nullptr ? m_ctrl->ref_count.load(std::memory_order_relaxed) : 0;
    }

private:
    struct ControlBlock {
        std::atomic<std::size_t> ref_count{1};
    };

    void release() noexcept {
        if (m_ctrl != nullptr) {
            // acq_rel ensures all prior mutations on *m_ptr across all threads
            // happen-before the final thread runs `delete m_ptr`!
            if (m_ctrl->ref_count.fetch_sub(1, std::memory_order_acq_rel) == 1) {
                delete m_ptr;
                delete m_ctrl;
            }
            m_ptr = nullptr;
            m_ctrl = nullptr;
        }
    }

    T* m_ptr{nullptr};
    ControlBlock* m_ctrl{nullptr};
};

} // namespace hft

#endif // HFT_Q12_SHARED_PTR_ATOMIC_HPP
