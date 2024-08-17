#include <iostream>
#include <deque>
#include <vector>

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
    
    int count = 0;
    int i = 0;
    int j = 0;
    LL sum = 0;
    //std::deque<LL> ans;

   while ( i < n) {
       if (j < n && nums[j] + sum <= target_sum) {
           sum += nums[j];
           ++j;
       } else {
           if ( sum == target_sum) {
               ++count;
           }
           sum -= nums[i];
           ++i;
       }
   }
    
    std::cout << count << "\n";
    return 0;
}
