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
    std::string s1, s2;

    std::cin >> s1;
    std::cin>>s2;

    int n =  s1.size();
    int m = s2.size();
    if ( s1 == s2 ) {
        std::cout << 0 ;  
        return 0;
    }

    

    std::vector<std::vector<int>> dp(n+1, std::vector<int>(m+1));


    int i, j;


    for( i = 0; i <= n; ++i) {
        for(j = 0; j <= m; ++j) {
            if (i ==0 ) {
                dp[i][j]  = j; // add j chars
            } else if ( j ==0) {
                dp[i][j] =  i; // add i chars;
            }
            else if ( s1[i-1] == s2[j-1]) {
                dp[i][j] = dp[i-1][j-1];
            } else {
                dp[i][j] = 1 + std::min( { dp[i-1][j-1], dp[i-1][j], dp[i][j-1] }); 
            }
        }
    }

    std::cout << dp[n][m];
}
