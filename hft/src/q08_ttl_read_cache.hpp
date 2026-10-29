#ifndef HFT_Q08_TTL_READ_CACHE_HPP
#define HFT_Q08_TTL_READ_CACHE_HPP

// ============================================================================
// QUESTION 08 [TIER 3 - MEDIUM]: CONCURRENT READ-MOSTLY TTL CACHE + SWEEPER
// ============================================================================
// EXCLUSIVE PROBLEM SCOPE (NON-OVERLAPPING):
//   Read-mostly concurrent key-value cache (`TtlCache<K, V>`) using
//   `std::shared_mutex` (`std::shared_lock` for concurrent readers,
//   `std::unique_lock` for writers/eviction) and an interruptible background
//   `std::jthread` sweeper (Concurrency-Ninja Ch11 Pattern 6).
//
// KEY C++20 INVARIANTS:
//   1. Strict Positive Duration Validation:
//      Constructor validates `ttl > Duration::zero()` and
//      `sweep_interval > Duration::zero()`, throwing `std::invalid_argument`
//      to prevent a 100% CPU busy-spinning sweeper thread.
//   2. Lazy Read Expiration + Periodic Write Sweep:
//      `get()` checks `Clock::now() < it->second.expires_at` under `shared_lock`
//      without mutating the map (preserving full reader concurrency), while
//      the sweeper thread periodically purges expired entries via `std::erase_if`.
//   3. Safe Member Destruction Order:
//      `sweeper_` (`std::jthread`) is declared LAST so its destructor requests
//      stop and joins BEFORE `rw_mu_` and `map_` are destroyed.
// ============================================================================

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <stdexcept>
#include <stop_token>
#include <thread>
#include <unordered_map>
#include <utility>

namespace hft {

template <typename K, typename V, typename Hash = std::hash<K>, typename KeyEqual = std::equal_to<K>>
class TtlCache {
public:
    using Clock     = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;
    using Duration  = Clock::duration;

    explicit TtlCache(Duration ttl, Duration sweep_interval = std::chrono::milliseconds(50))
        : ttl_(ttl), sweep_interval_(sweep_interval)
    {
        if (ttl_ <= Duration::zero()) {
            throw std::invalid_argument("TtlCache: ttl must be > 0");
        }
        if (sweep_interval_ <= Duration::zero()) {
            throw std::invalid_argument("TtlCache: sweep_interval must be > 0");
        }
        sweeper_ = std::jthread([this](std::stop_token st) { sweep_loop(st); });
    }

    ~TtlCache() noexcept = default; // sweeper_ destroyed first -> requests stop & joins

    TtlCache(const TtlCache&) = delete;
    TtlCache& operator=(const TtlCache&) = delete;

    void put(K key, V value, std::optional<Duration> custom_ttl = std::nullopt) {
        const Duration effective_ttl = custom_ttl.value_or(ttl_);
        if (effective_ttl <= Duration::zero()) {
            throw std::invalid_argument("TtlCache::put: custom_ttl must be > 0");
        }
        const TimePoint exp = Clock::now() + effective_ttl;
        std::unique_lock lk(rw_mu_);
        map_.insert_or_assign(std::move(key), Entry{std::move(value), exp});
    }

    [[nodiscard]] std::optional<V> get(const K& key) const {
        const TimePoint now = Clock::now();
        std::shared_lock lk(rw_mu_);
        auto it = map_.find(key);
        if (it == map_.end() || now >= it->second.expires_at) {
            return std::nullopt;
        }
        return it->second.value;
    }

    bool erase(const K& key) {
        std::unique_lock lk(rw_mu_);
        return map_.erase(key) > 0;
    }

    std::size_t evict_expired() {
        const TimePoint now = Clock::now();
        std::unique_lock lk(rw_mu_);
        return std::erase_if(map_, [now](const auto& kv) {
            return now >= kv.second.expires_at;
        });
    }

    [[nodiscard]] std::size_t size_including_expired() const {
        std::shared_lock lk(rw_mu_);
        return map_.size();
    }

private:
    struct Entry {
        V         value;
        TimePoint expires_at;
    };

    void sweep_loop(std::stop_token st) {
        std::mutex sleep_mu;
        std::condition_variable_any sleep_cv;
        while (!st.stop_requested()) {
            {
                std::unique_lock lk(sleep_mu);
                sleep_cv.wait_for(lk, st, sweep_interval_, [] { return false; });
            }
            if (st.stop_requested()) {
                break;
            }
            evict_expired();
        }
    }

    const Duration                                ttl_;
    const Duration                                sweep_interval_;
    mutable std::shared_mutex                     rw_mu_;
    std::unordered_map<K, Entry, Hash, KeyEqual>  map_;
    std::jthread                                  sweeper_; // MUST BE LAST
};

} // namespace hft

#endif // HFT_Q08_TTL_READ_CACHE_HPP
