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
//      `m_sweeper` (`std::jthread`) is declared LAST so its destructor requests
//      stop and joins BEFORE `m_rw_mu` and `m_map` are destroyed.
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
        : m_ttl(ttl), m_sweep_interval(sweep_interval)
    {
        if (m_ttl <= Duration::zero()) {
            throw std::invalid_argument("TtlCache: ttl must be > 0");
        }
        if (m_sweep_interval <= Duration::zero()) {
            throw std::invalid_argument("TtlCache: sweep_interval must be > 0");
        }
        m_sweeper = std::jthread([this](std::stop_token st) { sweep_loop(st); });
    }

    ~TtlCache() noexcept = default; // m_sweeper destroyed first -> requests stop & joins

    TtlCache(const TtlCache&) = delete;
    TtlCache& operator=(const TtlCache&) = delete;

    void put(K key, V value, std::optional<Duration> custom_ttl = std::nullopt) {
        const Duration effective_ttl = custom_ttl.value_or(m_ttl);
        if (effective_ttl <= Duration::zero()) {
            throw std::invalid_argument("TtlCache::put: custom_ttl must be > 0");
        }
        const TimePoint exp = Clock::now() + effective_ttl;
        std::unique_lock lk(m_rw_mu);
        m_map.insert_or_assign(std::move(key), Entry{std::move(value), exp});
    }

    [[nodiscard]] std::optional<V> get(const K& key) const {
        const TimePoint now = Clock::now();
        std::shared_lock lk(m_rw_mu);
        auto it = m_map.find(key);
        if (it == m_map.end() || now >= it->second.expires_at) {
            return std::nullopt;
        }
        return it->second.value;
    }

    bool erase(const K& key) {
        std::unique_lock lk(m_rw_mu);
        return m_map.erase(key) > 0;
    }

    std::size_t evict_expired() {
        const TimePoint now = Clock::now();
        std::unique_lock lk(m_rw_mu);
        return std::erase_if(m_map, [now](const auto& kv) {
            return now >= kv.second.expires_at;
        });
    }

    [[nodiscard]] std::size_t size_including_expired() const {
        std::shared_lock lk(m_rw_mu);
        return m_map.size();
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
                sleep_cv.wait_for(lk, st, m_sweep_interval, [] { return false; });
            }
            if (st.stop_requested()) {
                break;
            }
            evict_expired();
        }
    }

    const Duration                                m_ttl;
    const Duration                                m_sweep_interval;
    mutable std::shared_mutex                     m_rw_mu;
    std::unordered_map<K, Entry, Hash, KeyEqual>  m_map;
    std::jthread                                  m_sweeper; // MUST BE LAST
};

} // namespace hft

#endif // HFT_Q08_TTL_READ_CACHE_HPP
