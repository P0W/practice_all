#include <optional>
#include <unordered_map>
#include <list>
#include <cstdint>
#include <iostream>
#include <chrono>
#include <string_view>

class Timer {
    public:
    using Clock = std::chrono::steady_clock;
    Timer (const std::string_view& message) : m_start(Clock::now()), m_message {message} {
        
    }

    ~Timer() {
        m_end = Clock::now();
        const auto& duration = std::chrono::duration_cast<std::chrono::nanoseconds>(m_end-m_start);
        std::cout << m_message << " Elapsed :" << duration.count() << " ns.\n";
    }

    private:
    Clock::time_point m_start, m_end;
    const std::string_view& m_message;
};

template<typename K, typename V>
class LRUCache {
public:
    
    LRUCache(size_t capacity) : m_capacity{capacity} {Timer x{__PRETTY_FUNCTION__};}

    std::optional<V> get(K key) {
        Timer x{__PRETTY_FUNCTION__ };
        if(auto it = m_cache.find(key); it != m_cache.end()) {
            // Move accessed item to the front of the list
            auto listIt = it->second;
            V value = listIt->second;
            m_data.erase(listIt);
            m_data.emplace_front(key, value);
            m_cache[key] = m_data.begin();
            return std::optional<V>{value};
        }
        return {};
    }

    bool put(K key, V value) {
        Timer x{__PRETTY_FUNCTION__};
        if(const auto& it = m_cache.find(key); it != m_cache.end()) {
            // Update value and move to front
            auto listIt = it->second;
            m_data.erase(listIt);
            m_data.emplace_front(key, value);
            m_cache[key] = m_data.begin();
            return true;
        }
        // Check for capacity
        if (m_cache.size() >= m_capacity) {
            auto last = m_data.back();
            m_cache.erase(last.first);
            m_data.pop_back();
        }
        // Insert new key-value pair
        m_data.emplace_front(key, value);
        m_cache[key] = m_data.begin();
        return true;
    }

    void display() {
        for (const auto& pair : m_data) {
            std::cout << "{" << pair.first << ": " << pair.second << "} ";
        }
        std::cout << "\n";
    }
    
    ~LRUCache() {
        Timer x{__PRETTY_FUNCTION__};
    }
private:
    size_t m_capacity;
    std::list<std::pair<K, V>> m_data;
    std::unordered_map<K, typename std::list<std::pair<K, V>>::const_iterator> m_cache;
};

int main() {
    Timer x{__PRETTY_FUNCTION__};
    LRUCache<int, char> cache(3);

    cache.put(1, 'a');
    cache.put(2, 'b');
    cache.put(3, 'c');
    cache.display();  // Expected: {3: c} {2: b} {1: a} 

    cache.put(4, 'd');
    cache.display();  // Expected: {4: d} {3: c} {2: b} 

    std::cout << cache.get(2).value_or((char)1) << "\n";
    cache.display();  // Expected: {2: b} {4: d} {3: c} 

    cache.put(5, 'e');
    cache.display();  // Expected: {5: e} {2: b} {4: d} 
}
