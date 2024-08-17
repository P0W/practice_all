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
void perform_dfs(LL index, std::vector<Pair> & graph , std::vector<bool>& visited) {
    visited[index] =  true;
    // for each childs of graph[index]
    for(const auto& child: graph[index].first ) {
        if (!visited[child]) {
            graph[child].second  += graph[index].second;
            perform_dfs(child, graph, visited);
            
        }

    }
}

int main() {
    LL n;
    std::cin >> n ;

    if( n == 1) {
        std::cout <<0 ;
        return 0;
    }
    
    std::vector<Pair> graph(n);
    std::vector<bool> visited(n, false);

    LL i, a, b;
    // Build Tree
    for(i = 0; i < n - 1; ++i) {
        std::cin >> a >> b;
        graph[a-1].first.emplace_back(b - 1);
        graph[b-1].first.emplace_back(a - 1);
        graph[a-1].second = graph[b-1].second = 1;
    }
    

    perform_dfs(0, graph, visited);
    LL max_depth = 0;
    LL node1;
    for(i = 0; i < n; ++i) {
        if ( graph[i].second > max_depth) {
            max_depth = graph[i].second;
            node1 = i;
        }
    }
    
    // reset weights
    for(i = 0; i < n; ++i) {
         graph[i].second = 1;
        visited[i] = false;
    }    
    //std::cout << node1 << "\n";
    perform_dfs(node1, graph, visited);
    max_depth = 0;
    for(i = 0; i < n; ++i) {
        if ( graph[i].second > max_depth) {
            max_depth = graph[i].second;
        }
    }

    std::cout << max_depth -1;

}
