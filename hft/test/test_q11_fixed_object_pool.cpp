#include "../src/q11_fixed_object_pool.hpp"
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <stdexcept>

#define HFT_VERIFY(expr) \
    do { \
        if (!(expr)) { \
            std::cerr << "[FAIL] " << __FILE__ << ":" << __LINE__ << " -> " #expr "\n"; \
            std::abort(); \
        } \
    } while (false)

namespace {
struct TrackedOrder {
    static inline int active_instances = 0;
    std::uint64_t id;
    explicit TrackedOrder(std::uint64_t i, bool should_throw = false) : id(i) {
        if (should_throw) {
            throw std::runtime_error("simulated constructor failure");
        }
        ++active_instances;
    }
    ~TrackedOrder() noexcept { --active_instances; }
};
} // namespace

int main() {
    {
        hft::FixedObjectPool<TrackedOrder, 4> pool;
        HFT_VERIFY(pool.available() == 4);

        // 1. Verify strong exception safety when constructor throws
        bool caught = false;
        try {
            (void)pool.allocate(999ULL, true);
        } catch (const std::runtime_error&) {
            caught = true;
        }
        HFT_VERIFY(caught);
        HFT_VERIFY(pool.available() == 4); // Slot must be rolled back onto free-list!
        HFT_VERIFY(TrackedOrder::active_instances == 0);

        // 2. Allocate all 4 slots
        auto* o1 = pool.allocate(101ULL);
        auto* o2 = pool.allocate(102ULL);
        auto* o3 = pool.allocate(103ULL);
        auto* o4 = pool.allocate(104ULL);
        HFT_VERIFY(pool.available() == 0);
        HFT_VERIFY(pool.allocate(105ULL) == nullptr);
        HFT_VERIFY(TrackedOrder::active_instances == 4);

        // 3. Verify misaligned interior pointer is rejected by owns()
        auto* misaligned = reinterpret_cast<const TrackedOrder*>(
            reinterpret_cast<const char*>(o1) + 1);
        HFT_VERIFY(pool.owns(o1));
        HFT_VERIFY(!pool.owns(misaligned));

        // 4. Deallocate 2 objects and leave 2 live objects for ~FixedObjectPool() cleanup
        pool.deallocate(o2);
        pool.deallocate(o3);
        HFT_VERIFY(pool.available() == 2);
        HFT_VERIFY(TrackedOrder::active_instances == 2);
        (void)o1;
        (void)o4;
    }
    // 5. ~FixedObjectPool() must have destroyed the remaining 2 live objects!
    HFT_VERIFY(TrackedOrder::active_instances == 0);

    std::cout << "[PASS] Q11 Fixed-Block Slab Object Pool\n";
    return 0;
}
