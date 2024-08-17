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
    LL n, m;
    std::cin >> n >> m ;

    std::vector<std::vector<char>> graph(n, std::vector<char>(m));

    
    std::vector<std::vector<bool>> visited(n, std::vector<bool>(m, false));
    for (LL i =0; i < n; ++i) {
        for(LL j = 0; j < m; ++j) {
            std::cin >> graph[i][j];
        }
    }

  
    
    LL rooms = 0;
    for (LL i =0; i < n; ++i) {
        for(LL j = 0; j < m; ++j) {
            if(!visited[i][j] && graph[i][j] == '.') {
                 ++rooms;
                perform_dfs(i, j, graph, visited);
                //std::cout << rooms << "\n";
            }
        }
    }

    std::cout << rooms ;
    
}
