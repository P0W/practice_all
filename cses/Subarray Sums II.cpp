#include <iostream>
#include <deque>
#include <vector>
#include <unordered_map>

using LL = long long;

void print(std::deque<LL> &ans) {
    for (LL num : ans) {
        std::cout << num << " ";
    }
    std::cout << "\n";
}

int main() {
    int n, target_sum;
    std::cin >> n >> target_sum;
    std::vector<LL> nums(n);
    
    for (int i = 0; i < n; ++i) {
        std::cin >> nums[i];
    }
    
    LL count = 0;
    //int i = 0;
   // int j = 0;
    LL sum = 0;
    //std::deque<LL> ans;

    std::unordered_map<LL, LL> prefix_sum;
    prefix_sum[0] = 1;
 
    for ( const auto& x: nums) {
        sum += x;
        if ( auto it = prefix_sum.find(sum- target_sum); it != prefix_sum.end()) {
            count += it->second;
        }
        prefix_sum[sum]++;
    }
    
    std::cout << count << "\n";
    return 0;
}
