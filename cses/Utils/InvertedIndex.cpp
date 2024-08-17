#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <ctime>
#include <functional>
#include <future>
#include <iostream>
#include <iterator>
#include <memory>
#include <mutex>
#include <queue>
#include <ratio>
#include <thread>
#include <type_traits>
#include <typeinfo>
#include <unordered_map>
#include <utility>
#include <ranges>

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
        std::bind(std::forward<F&&>(f), std::forward<Args&&>(args)...));

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

class InvertedIndex {
  unordered_map<string, vector<size_t>> m_inverted_index;

  vector<string> stop_words = {",", "the", "a", "an", "is", "that"};
  vector<string> tokenize(const std::string &text) {
    auto toLower = [](char c) {
      return std::tolower(static_cast<unsigned char>(c));
    };
    auto toWord = [](auto &&item) { return string{begin(item), end(item)}; };
    

    auto words = text | views::transform(toLower) | views::split(' ') |
                 views::transform(toWord) |
                 views::filter([this](auto &&word) {
                   for (const auto &stop_word : stop_words) {
                     if (stop_word == word)
                       return false;
                   }
                   return true;
                 });

    return std::vector<string>{begin(words), end(words)};
  }

public:
  void addDocument(int docId, const std::string &text) {
    for (const auto& word: tokenize(text)) {
      m_inverted_index[word].emplace_back(docId);
    }
  }

  std::vector<size_t> get_docIds(const std::string &text) {
    auto words_to_search = tokenize(text);
    if (words_to_search.empty()) {
      return {};
    }
    vector<size_t> result = m_inverted_index[words_to_search[0]];
    if (result.empty()) {
      return {};
    }
    for (const auto &word : words_to_search | views::drop(1)) {
      if (auto it = m_inverted_index.find(word); it == m_inverted_index.end()) {
        return {};
      } else {
        vector<size_t> temp;
        set_intersection(begin(it->second), end(it->second), begin(result),
                         end(result), back_inserter(temp));
        result = std::move(temp);
      }
    }

    return result;
  }
};

class SearchFunctions {
public:
  template <typename T> string get_type_name() { return typeid(T).name(); }

  template <typename... Args>
  void register_func(const string &func_name, std::function<void(Args...)> f) {
    vector<string> args_names = {get_type_name<Args>()...};

    m_func_mapper.emplace(
        func_name,
        FunctionNode{func_name,
                     [&] {  },
                     args_names});
  }

private:
  struct FunctionNode {
    string func_name;
    function<void()> task;
    vector<string> args_list;
    FunctionNode(const string &func_name, const function<void()> &func,
                 const vector<string> &args)
        : func_name{func_name}, task{func}, args_list{args} {}
  };

  std::unordered_map<string, FunctionNode> m_func_mapper;
};

int main() {

  JobScheduler js;
  js.schedule_task(
      "task-1", [] { cout << "Hello World! \n"; }, 300us);
  js.schedule_task(
      "task-2", [] { cout << "Hello Jack! \n"; }, 10ms);
  js.schedule_task(
      "task-3",
      [](int a, int b) { cout << "Hello King! : " << a * b << " \n"; }, 10ms,
      42, 618);

  InvertedIndex obj;

  obj.addDocument(1, "Cloud computing is the on-demand availability of "
                     "computer system resources.");
  obj.addDocument(
      2,
      "One integrated service for metrics uptime cloud monitoring dashboards "
      "and alerts reduces time spent navigating between systems.");
  obj.addDocument(3, "Monitor entire cloud infrastructure, whether in the "
                     "cloud computing is or in virtualized data centers.");
  obj.addDocument(4, "The demand  for cloud computing is nowday literally recahing to cloud");

  auto result = obj.get_docIds("cloud computing is");
  for (const auto& item: result) {
    cout << item << " ";
  }

  SearchFunctions sf;
  sf.register_func("adder", std::function<void(int, int)>([](int a, int b) { cout << "Addition: " << a + b << "\n"; }));


  this_thread::sleep_for(1s); // To allow tasks to complete before program exits

  return 0;
}
