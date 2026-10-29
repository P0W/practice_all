#include "../src/q15_seqlock_snapshot.hpp"
#include <atomic>
#include <cstdlib>
#include <iostream>
#include <thread>
#include <vector>

#define HFT_VERIFY(expr) \
    do { \
        if (!(expr)) { \
            std::cerr << "[FAIL] " << __FILE__ << ":" << __LINE__ << " -> " #expr "\n"; \
            std::abort(); \
        } \
    } while (false)

struct BboQuote {
    std::int64_t bid_px{0};
    std::int64_t ask_px{0};
    std::int64_t checksum{0}; // Must always equal bid_px ^ ask_px ^ 0x5A5A5A5ALL
};

int main() {
    hft::SeqLock<BboQuote> seqlock(BboQuote{100, 101, 100 ^ 101 ^ 0x5A5A5A5ALL});
    std::atomic<bool> done{false};

    std::thread writer([&] {
        for (std::int64_t i = 1; i <= 300'000; ++i) {
            const std::int64_t bid = i * 10;
            const std::int64_t ask = bid + 5;
            seqlock.write(BboQuote{bid, ask, bid ^ ask ^ 0x5A5A5A5ALL});
        }
        done.store(true, std::memory_order_release);
    });

    std::vector<std::thread> readers;
    for (int r = 0; r < 4; ++r) {
        readers.emplace_back([&] {
            while (!done.load(std::memory_order_acquire)) {
                const BboQuote snap = seqlock.read();
                HFT_VERIFY(snap.ask_px > snap.bid_px);
                HFT_VERIFY(snap.checksum == (snap.bid_px ^ snap.ask_px ^ 0x5A5A5A5ALL));
            }
        });
    }

    writer.join();
    for (auto& t : readers) t.join();
    const BboQuote final_snap = seqlock.read();
    HFT_VERIFY(final_snap.bid_px == 3'000'000LL);
    std::cout << "[PASS] Q15 Seqlock Optimistic Snapshot\n";
    return 0;
}
