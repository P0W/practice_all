#include <iostream>
#include <string>
#include <vector>
#include <iterator>
#include <algorithm>
#include <numeric>
#include <cmath>
#include <set>
#include <stack>

using LL = long long;

std::set<LL> s1;
bool solve(LL n, LL desired_sum, std::set<LL>& current_set) {
    if (desired_sum == 0) {
        s1 = current_set;
        return true;
    }
    if (n <= 0 || desired_sum < 0) {
        return false;
    }
    
    // Try taking the current number
    current_set.insert(n);
    if (solve(n - 1, desired_sum - n, current_set)) {
        return true;
    }
    current_set.erase(n);

    // Try skipping the current number
    if (solve(n - 1, desired_sum, current_set)) {
        return true;
    }

    return false;
}

// 1 2 3 4 5

int main() {
    LL n;
    std::cin >> n;
    LL total_sum = n*(n + 1);
    if ( total_sum % 4) {
        std::cout << "NO";
        return 0;
    }
    std::cout <<"YES\n";
    LL half_sum = total_sum >> 2;
    std::set<LL> temp_set;
    solve(n, half_sum, temp_set);

    std::cout << s1.size() << "\n";
    for( const auto& item: s1) {
        std::cout << item << " ";
    }
    std::cout << "\n" << n - s1.size() << "\n";
    for( LL i = 1; i <= n; ++i) {
        if(s1.find(i) == s1.end()) {
            std::cout << i << " ";
        }
        }
}