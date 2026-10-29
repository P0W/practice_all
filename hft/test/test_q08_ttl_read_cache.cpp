#include "../src/q08_ttl_read_cache.hpp"
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

#define HFT_VERIFY(expr) \
    do { \
        if (!(expr)) { \
            std::cerr << "[FAIL] " << __FILE__ << ":" << __LINE__ << " -> " #expr "\n"; \
            std::abort(); \
        } \
    } while (false)

int main() {
    using namespace std::chrono_literals;

    // 1. Verify non-positive TTL/sweep_interval throws std::invalid_argument
    bool rejected_zero_ttl = false;
    try {
        hft::TtlCache<int, int> bad_cache(0ms, 10ms);
    } catch (const std::invalid_argument&) {
        rejected_zero_ttl = true;
    }
    HFT_VERIFY(rejected_zero_ttl);

    // 2. Verify live read hit, lazy expiration, and background sweep
    hft::TtlCache<std::string, int> cache(40ms, 15ms);
    cache.put("NIFTY_FUT", 22500);
    HFT_VERIFY(cache.get("NIFTY_FUT").value_or(0) == 22500);

    std::this_thread::sleep_for(90ms);
    HFT_VERIFY(!cache.get("NIFTY_FUT").has_value());
    HFT_VERIFY(cache.size_including_expired() == 0);

    std::cout << "[PASS] Q08 Concurrent Read-Mostly TTL Cache\n";
    return 0;
}
