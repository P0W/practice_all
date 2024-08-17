#include <thread>
#include <functional>
#include <queue>
#include <vector>
#include <future>
#include <iostream>
#include <chrono>
#include <string_view>
#include <concepts>

class ThreadPool {
    
    public:
    ThreadPool(int numThreads = std::thread::hardware_concurrency()): m_num_threads(numThreads), m_stop_flag(false) {
        m_threads.reserve(m_num_threads);

        for(size_t index = 0; index < m_num_threads; ++index) {
            m_threads.emplace_back(&ThreadPool::worker, this);
        }
    }
    template<typename F, typename ... Args>
    requires std::invocable<F, Args...> 
    auto schedule_task(F && f, Args&&...args) -> std::future<std::invoke_result_t<F, Args...>> {
        using ReturnType = std::invoke_result_t<F, Args...>;
        auto task = std::make_shared<std::packaged_task<ReturnType()>>( 
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)
        );
        std::future<ReturnType> result;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            result = task->get_future();
            m_task_queue.emplace([task]{
                (*task)();
            });
        }
        m_cv.notify_one();

        return result;
    }

    ~ThreadPool() {
        m_stop_flag = true;
        m_cv.notify_all();
    }

    private:
    void worker() {
        while(!m_stop_flag) {
            std::function<void()> currentTask;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_cv.wait(lock, [this]{return !m_task_queue.empty() || m_stop_flag;});

            // re-check
            if(m_stop_flag || m_task_queue.empty()) {
                return;
            }
            currentTask = std::move(m_task_queue.front());
            m_task_queue.pop();
            
        }
            currentTask();
        }
    }


    size_t m_num_threads;
    std::atomic_bool m_stop_flag;
    std::vector<std::jthread> m_threads;
    std::queue<std::function<void()>> m_task_queue;
    std::mutex m_mutex;
    std::condition_variable m_cv;
    
};

std::string_view simulate_api_call(int duration) {
    std::this_thread::sleep_for(std::chrono::seconds(duration));
    return "slept for sometime";  
}

int main()
{
    ThreadPool pool;

    pool.schedule_task([]{std::cout << "task 1\n"; });
    auto fut = pool.schedule_task(simulate_api_call, 2);

    std::cout << "Result from pool operation : " << fut.get() << std::endl;

    return 0;
}