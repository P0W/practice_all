#include "../src/q13_inplace_function_sbo.hpp"
#include <cstdlib>
#include <iostream>
#include <memory>

#define HFT_VERIFY(expr) \
    do { \
        if (!(expr)) { \
            std::cerr << "[FAIL] " << __FILE__ << ":" << __LINE__ << " -> " #expr "\n"; \
            std::abort(); \
        } \
    } while (false)

int main() {
    // 1. Move-only unique_ptr capture inside 32-byte SBO buffer
    auto resource = std::make_unique<int>(100);
    hft::InplaceFunction<int(int), 32> fn = [ptr = std::move(resource)](int delta) noexcept {
        *ptr += delta;
        return *ptr;
    };
    HFT_VERIFY(static_cast<bool>(fn));
    HFT_VERIFY(fn(25) == 125);

    // 2. Move construction transfers inline storage and empties source
    hft::InplaceFunction<int(int), 32> moved_fn = std::move(fn);
    HFT_VERIFY(!static_cast<bool>(fn));
    HFT_VERIFY(moved_fn(50) == 175);

    // 3. Void-returning InplaceFunction<void()> wrapping non-void lambda (std::invoke_r semantics)
    int side_effect = 0;
    hft::InplaceFunction<void(), 32> void_fn = [&side_effect]() noexcept -> int {
        return ++side_effect;
    };
    void_fn();
    HFT_VERIFY(side_effect == 1);

    std::cout << "[PASS] Q13 Small-Buffer-Optimized InplaceFunction\n";
    return 0;
}
