#include <iostream>
#include <string>
#include <vector>
#include <iterator>
#include <algorithm>
#include <numeric>
#include <cmath>
#include <set>

using LL = long long;

int main() {
    LL n, num, prev = 0;
    std::cin >> n;
   
    
    std::set<LL> checker;
    while (n--) {
        std::cin >> num;
        if (prev == num) continue;
        if ( auto it = checker.find(num); it  == checker.end()) {
            checker.emplace_hint( it, num);
        }
        
        prev = num;
    }

    std::cout << checker.size() ;
    
}