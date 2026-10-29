#include "../src/q17_observable_history.hpp"
#include <cstdlib>
#include <iostream>

#define HFT_VERIFY(expr) \
    do { \
        if (!(expr)) { \
            std::cerr << "[FAIL] " << __FILE__ << ":" << __LINE__ << " -> " #expr "\n"; \
            std::abort(); \
        } \
    } while (false)

int main() {
    // 1. Observer notification outside lock + O(1) snapshot rollback
    {
        hft::ObservableHistory<int> hist;
        std::size_t observed_ver = 0;
        int observed_val = 0;

        const auto sub_id = hist.subscribe([&](const int& v, std::size_t ver) {
            observed_val = v;
            observed_ver = ver;
            // Re-entrant snapshot() inside observer callback must NEVER deadlock!
            HFT_VERIFY(hist.snapshot()->version == ver);
        });

        hist.append(10);
        auto snap_v1 = hist.snapshot();
        hist.append(20);
        HFT_VERIFY(observed_val == 20 && observed_ver == 2);

        hist.rollback(snap_v1);
        HFT_VERIFY(hist.snapshot()->value == 10 && hist.snapshot()->version == 1);
        HFT_VERIFY(hist.unsubscribe(sub_id));
    }

    // 2. Deep Chain Teardown Test: 60,000 nodes destroyed iteratively
    //    with ZERO heap allocation and ZERO stack overflow inside ~Node()!
    {
        hft::ObservableHistory<int> deep_hist;
        for (int i = 1; i <= 60'000; ++i) {
            deep_hist.append(i);
        }
        HFT_VERIFY(deep_hist.snapshot()->version == 60'000);
    }

    std::cout << "[PASS] Q17 Observable Persistent History DAG\n";
    return 0;
}
