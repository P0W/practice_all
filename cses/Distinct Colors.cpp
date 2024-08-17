#include <iostream>
#include <deque>
#include <vector>
#include <unordered_set>
#include <algorithm>

using LL = long long;

template <typename Container>
void print(const Container& container) {
    for (const auto& element : container) {
        std::cout << element << " ";
    }
    std::cout << "\n";
}
using Pair = std::pair<std::vector<LL>, std::unordered_set<LL>>;
void perform_dfs(LL index, std::vector<Pair>& graph, std::vector<bool>& visited) {
    visited[index ] = true;
    for ( auto& child: graph[index].first) {
        if (visited[child]) continue;
        // childs
        perform_dfs(child, graph, visited);
        // update colors
        graph[index].second.insert(graph[child].second.begin(), graph[child].second.end());
    }
}
int main() {
   LL n;
    std::cin >> n;
    std::vector<LL> colors (n);
    for(LL i= 0; i < n; ++i) {
        std::cin >> colors[i];
    }
    std::vector<Pair> graph(n);

    std::vector<bool> visited(n);

    LL a , b;
    graph[0].second.emplace(colors[0]);
    for(LL i =0; i < n - 1; ++i) {
        std::cin >> a >> b;
        graph[a - 1].first.emplace_back( b - 1);
        graph[b - 1].first.emplace_back( a - 1);
    }
    for (LL i = 0; i < n; ++i) {
        graph[i].second.emplace(colors[i]);
    }

    perform_dfs(0, graph, visited);
    for(const auto& node: graph) {
        std::cout << node.second.size() << " ";
    }

}
