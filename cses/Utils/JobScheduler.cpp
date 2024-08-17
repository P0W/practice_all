#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <future>
#include <iostream>
#include <memory>
#include <mutex>
#include <queue>
#include <ratio>
#include <thread>
#include <type_traits>
#include <utility>

using namespace std;
using namespace std::chrono_literals;

class JobScheduler {
public:
  using Timepoint = std::chrono::steady_clock::time_point;
  using Duration = std::chrono::duration<long, std::micro>;

  JobScheduler() {
    runnner_thread = jthread([this]() { run(); });
  }

  ~JobScheduler() {
    m_stop_flag = true;
    m_cv.notify_all();
  }

  template <typename F, typename... Args>
  auto schedule_task(const string &task_name, F &&f, Duration duration,
                     Args &&...args)
      -> future<invoke_result_t<F &&, Args &&...>> {
    using ResultType = invoke_result_t<F &&, Args &&...>;

    auto task = make_shared<packaged_task<ResultType()>>(
        std::bind(std::forward<F>(f), std::forward<Args>(args)...));

    auto result = task->get_future();
    {
      unique_lock lock(m_mutex);
      Timepoint cur_time = chrono::steady_clock::now();
      m_queue.emplace(
          task_name, [task] { (*task)(); }, cur_time + duration);

      m_cv.notify_one();
    }
    return result;
  }

private:
  void run() {
    while (!m_stop_flag) {
      {
        unique_lock lock(m_mutex);
        m_cv.wait(lock, [this]() { return !m_queue.empty() || m_stop_flag; });

        if (m_stop_flag || m_queue.empty()) {
          break;
        }
        Timepoint cur_time = chrono::steady_clock::now();

        auto curr_task = m_queue.top();
        if (cur_time >= curr_task.wait_time) {
          m_queue.pop();
          curr_task.task();
        } else {
          m_cv.wait_until(lock, curr_task.wait_time);
        }
      }
    }
  }

  struct Node {
    string task_name;
    std::function<void()> task;
    Timepoint wait_time;

    bool operator<(const Node &rhs) const { return wait_time > rhs.wait_time; }

    explicit Node(const string &func_name, const std::function<void()> &func,
                  const Timepoint &dur)
        : task_name{func_name}, task{func}, wait_time(dur) {}
  };
  std::jthread runnner_thread;
  priority_queue<Node> m_queue;
  std::mutex m_mutex;
  condition_variable m_cv;
  atomic_bool m_stop_flag = false;
};

// To execute C++, please define "int main()"
int main() {

  JobScheduler js;
  js.schedule_task(
      "task-1", [] { cout << "Hello World! \n"; }, 300ms);
  js.schedule_task(
      "task-2", [] { cout << "Hello Jack! \n"; }, 10ms);
  js.schedule_task(
      "task-3",
      [](int a, int b) { cout << "Hello King! : " << a * b << " \n"; }, 10ms,
      42, 618);

  this_thread::sleep_for(1s); // To allow tasks to complete before program exits

  return 0;
}
