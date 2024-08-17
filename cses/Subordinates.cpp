#include <iostream>
#include <deque>
#include <vector>
#include <map>
#include <algorithm>

using LL = long long;

template <typename Container>
void print(const Container& container) {
    for (const auto& element : container) {
        std::cout << element << " ";
    }
    std::cout << "\n";
}
using Pair = std::pair<std::vector<LL>, LL>;
void perform_dfs(LL index, std::vector<Pair> & graph) {

    // for each childs of graph[index]
    for(const auto& child: graph[index].first ) {
        perform_dfs(child, graph);
        graph[index].second  += graph[child].second;
    }
}

int main() {
    LL n;
    std::cin >> n ;
    
    std::vector<Pair> graph(n);

    LL i, boss;
    for(i = 0; i < n - 1; ++i) {
        std::cin >> boss;
        graph[boss-1].first.emplace_back(i+1);
    }

    for( auto& emp: graph) {
        emp.second += emp.first.size();
    }
    
    perform_dfs(0, graph);
    for(const auto& emp: graph) {
        std::cout << emp.second << " ";
    }

}
