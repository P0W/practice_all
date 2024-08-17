#include <iostream>
#include <chrono>
#include <unordered_map>
#include <string>
#include <queue>
#include <list>
#include <concepts>
#include <thread>
#include <condition_variable>

using namespace std;
using namespace std::chrono;
using Timepoint = std::chrono::steady_clock::time_point;

template<typename K, typename V>
requires std::is_floating_point_v<V>
class TTLCache {
    public:
    struct Node {
      K key;
      V value;
      Timepoint expiration;
      Node(K k, V v, Timepoint t) : key(k), value(v),expiration(t)   {
      }

    bool operator < (const Node&rhs) const {
               return expiration > rhs.expiration;
    }
      
    };
    TTLCache(const TTLCache&) = delete;
    TTLCache& operator=(const TTLCache&) = delete;

    explicit TTLCache(long ttl = 0) : m_ttl(milliseconds(ttl)), m_total_sum(0),m_stop_flag(false) {
        std::jthread th([this]() {
            while(!m_stop_flag) {
                {
                    std::unique_lock lock(m_mutex);
                    m_cv.wait(lock, [this]() { return !m_queue.empty() || m_stop_flag;});
                    this->cleanup(get_current_time());
                }
            }
        });

        cleaner_thread = std::move(th);
        
    }

    ~TTLCache() {
        m_stop_flag = true;
        m_cv.notify_all();
    }

    bool put(const K& key, const V& value) {

        if(auto it = m_cache.find(key) ; it != m_cache.end()) {
            m_total_sum -= it->second->value;
            m_cache.erase(key);
            m_store.erase(it->second);
        }
        Timepoint current_time = get_current_time();
        Timepoint expiration =   current_time + m_ttl;
        std::cout << "Setting " << key << " expiration to: " ;
        displayTime(expiration);
        
        m_store.emplace_front(key, value, expiration);

        m_cache.emplace(key, m_store.begin());
        
        m_total_sum += value;

        {
            std::unique_lock lock(m_mutex);
            m_queue.emplace(key, value, expiration);
            m_cv.notify_one();
        }
        
        
        return true;
    }

    std::optional<V> get(const K& key) {
        
        if(auto it = m_cache.find(key) ; it != m_cache.end()) {
            return it->second->value;
        }
        return {};
    }

    double getAverage()  {
        size_t q_sz = 0;
        {
            std::unique_lock lock(m_mutex);
            if(m_queue.empty()) {
                return 0.0;
            }
            q_sz = m_queue.size();
        }

        return m_total_sum/ static_cast<double>(q_sz);
    }

    void displayTime(const Timepoint& timepoint) {
        std::cout << time_point_cast<milliseconds>(timepoint).time_since_epoch().count() << "\n";
    }

    private:

    Timepoint get_current_time() const {
        return std::chrono::steady_clock::now();
    }

    void cleanup(const Timepoint& thisTime) {
        //std::cout << "Cleaning at " ;
        //displayTime(thisTime);
        while (!m_queue.empty() && thisTime > m_queue.top().expiration  ) {
            //displayTime(m_queue.top().expiration );
            
            auto item = m_queue.top();
            //std::cout << "Cleaning expired " << item.key << "\n";
            m_total_sum -= item.value;
            m_queue.pop();
            m_store.erase(m_cache[item.key]);
            m_cache.erase(item.key);
        }
    }

    std::chrono::duration<long, std::milli> m_ttl;
    std::list<Node> m_store;
    std::unordered_map<K, typename std::list<Node>::iterator> m_cache;
    std::priority_queue<Node, std::vector<Node>> m_queue;
    double m_total_sum;
    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::atomic_bool m_stop_flag;
    std::jthread cleaner_thread;
};


int main()
{
    TTLCache<std::string, float> ttlcache(10);
    ttlcache.put("jack", 4);
    std::this_thread::sleep_for(milliseconds(3));
    ttlcache.put("king", 2);
    std::cout << "Average " << ttlcache.getAverage() << "\n";

    auto item =  ttlcache.get("jack");
    std::cout << item.value_or(-1) << "\n";

    std::this_thread::sleep_for(milliseconds(8));
    item =  ttlcache.get("jack");
    std::cout << item.value_or(-1) << "\n";
    std::cout << "Average " << ttlcache.getAverage() << "\n";

    std::this_thread::sleep_for(milliseconds(8));
    item =  ttlcache.get("king");
    std::cout << item.value_or(-1) << "\n";
    std::cout << "Average " << ttlcache.getAverage() << "\n";
    
    return 0;
}