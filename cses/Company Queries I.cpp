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
void perform_dfs(LL row, LL col,  const std::vector<std::vector<char>> & graph ,std::vector<std::vector<bool>> & visited) {
    visited[row][col] =  true;
    // for each childs of graph[index]
    int dirs[4][2] = { {1,0}, {0,1}, {-1, 0}, {0, -1}};
    
    for( LL k = 0; k <4; ++k) {
        LL a = row + dirs[k][0];
        LL b = col + dirs[k][1];
        if ( (a >=0 && a< (LL)graph.size()) && (b>=0 && b< (LL)graph[0].size()) && !visited[a][b] && graph[a][b] == '.') {

            //std::cout << "Exploring " << a << b << "\n";
            perform_dfs(a, b, graph, visited);
        }
        //std::cout << a << ":" << b << "\n";
    }
}
int main() {
    LL n, q;
    std::cin >> n >> q ;
    LL boss;

    std::vector<Pair> graph(n);

    graph[0].second = -2;

    for(LL i = 0; i < n -1; ++i) {
        std::cin >> boss;
        graph[boss-1].first.emplace_back(i+1);
        //if ( i == 0) { graph[i+1].second = -1;}
        //else
        graph[i+1].second = boss-1; // parent
    }
    LL i = 0;
    /*for( const auto& boss: graph) {
        std::cout << i +1 << ":";
        for( const auto& emp: boss.first) {
            std::cout << emp + 1<< " " ;
        }
        std::cout << " (" << boss.second +1 << ")\n";
        ++i;
    }*/
     LL x, k;
    for(i = 0; i < q; ++i) {
        std::cin >> x >> k;
        boss = graph[x-1].second;
        while (--k) {
            boss = graph[boss].second;
        }
        std::cout << boss +1  << "\n";
    }


    /**
5
3
4
6
2
3
-1
1
3
1*/
    
}
