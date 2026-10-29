#ifndef HFT_Q06_SHARDED_COUNTER_MAP_HPP
#define HFT_Q06_SHARDED_COUNTER_MAP_HPP

// ============================================================================
// QUESTION 06 [TIER 2 - MEDIUM]: CACHE-LINE-ALIGNED SHARDED COUNTER MAP
// ============================================================================
// EXCLUSIVE PROBLEM SCOPE (NON-OVERLAPPING):
//   Striped/sharded associative counter map (`ShardedCounterMap<K, Shards>`)
//   where each shard owns its own `std::shared_mutex` and `std::unordered_map`
//   aligned to a 64-byte cache line (`alignas(64)`) to eliminate cross-shard
//   false sharing (Concurrency-Ninja Ch11 Pattern 8).
//
// KEY C++20 DESIGN DETAILS:
//   - Accepts any `Shards > 0` (`hash_(key) % Shards`), matching Ch11 Pattern 8;
//     when `Shards` is a power of two (default 16), the compiler optimizes
//     `% Shards` into a single bitwise `AND` instruction.
//   - Conditional `noexcept(noexcept(hash_(key)))` on `shard_for()`.
//   - Note on `total()` snapshot consistency: sums shards sequentially without
//     a global stop-the-world lock (eventual/relaxed aggregate telemetry).
// ============================================================================

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>

namespace hft {

template <
    typename K,
    std::size_t Shards = 16,
    typename Hash = std::hash<K>,
    typename KeyEqual = std::equal_to<K>>
    requires(Shards > 0)
class ShardedCounterMap {
public:
    ShardedCounterMap() = default;
    ShardedCounterMap(const ShardedCounterMap&) = delete;
    ShardedCounterMap& operator=(const ShardedCounterMap&) = delete;

    std::int64_t increment(const K& key, std::int64_t delta = 1) {
        Shard& s = shard_for(key);
        std::unique_lock lk(s.mu);
        return (s.counts[key] += delta);
    }

    [[nodiscard]] std::int64_t get(const K& key) const {
        const Shard& s = shard_for(key);
        std::shared_lock lk(s.mu);
        auto it = s.counts.find(key);
        return (it != s.counts.end()) ? it->second : 0;
    }

    bool erase(const K& key) {
        Shard& s = shard_for(key);
        std::unique_lock lk(s.mu);
        return s.counts.erase(key) > 0;
    }

    // Non-atomic point-in-time telemetry sum across shards (acquires one shard at a time)
    [[nodiscard]] std::int64_t total() const {
        std::int64_t sum = 0;
        for (const auto& s : shards_) {
            std::shared_lock lk(s.mu);
            for (const auto& [k, v] : s.counts) {
                sum += v;
            }
        }
        return sum;
    }

private:
    struct alignas(64) Shard {
        mutable std::shared_mutex                           mu;
        std::unordered_map<K, std::int64_t, Hash, KeyEqual> counts;
    };

    [[nodiscard]] Shard& shard_for(const K& key) noexcept(noexcept(hash_(key))) {
        return shards_[hash_(key) % Shards];
    }

    [[nodiscard]] const Shard& shard_for(const K& key) const noexcept(noexcept(hash_(key))) {
        return shards_[hash_(key) % Shards];
    }

    [[no_unique_address]] Hash    hash_{};
    std::array<Shard, Shards>     shards_{};
};

} // namespace hft

#endif // HFT_Q06_SHARDED_COUNTER_MAP_HPP
