#include "../src/q07_future_then_continuation.hpp"
#include <cstdlib>
#include <future>
#include <iostream>
#include <stdexcept>

#define HFT_VERIFY(expr) \
    do { \
        if (!(expr)) { \
            std::cerr << "[FAIL] " << __FILE__ << ":" << __LINE__ << " -> " #expr "\n"; \
            std::abort(); \
        } \
    } while (false)

int main() {
    // 1. Value -> Value -> Void continuation chain
    auto f1 = std::async(std::launch::async, [] { return 21; });
    auto f2 = hft::then(std::move(f1), [](int x) { return x * 2; });
    int final_val = 0;
    auto f3 = hft::then(std::move(f2), [&](int y) { final_val = y + 8; });
    f3.get();
    HFT_VERIFY(final_val == 50);

    // 2. Upstream exception propagation across Void -> Value stages
    auto fail_root = std::async(std::launch::async, []() -> void {
        throw std::runtime_error("upstream feed error");
    });
    auto downstream = hft::then(std::move(fail_root), [] { return 999; });

    bool caught = false;
    try {
        (void)downstream.get();
    } catch (const std::runtime_error&) {
        caught = true;
    }
    HFT_VERIFY(caught);

    // 3. Invalid empty std::future rejected with std::invalid_argument
    bool rejected_invalid = false;
    try {
        std::future<int> empty_fut;
        (void)hft::then(std::move(empty_fut), [](int x) { return x; });
    } catch (const std::invalid_argument&) {
        rejected_invalid = true;
    }
    HFT_VERIFY(rejected_invalid);

    std::cout << "[PASS] Q07 Exception-Propagating Future Continuation\n";
    return 0;
}
