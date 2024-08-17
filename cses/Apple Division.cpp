#include <iostream>
#include <string>
#include <vector>
#include <iterator>
#include <algorithm>
#include <numeric>
#include <cmath>

using LL = long long;


LL solve(const std::vector<LL>& nums, LL g1, LL g2, LL index) {

    if ( index == (LL)nums.size()) {
        return std::abs(g1 - g2);
    }
    LL put_in_g1 = solve(nums,g1 +nums[index] , g2, index + 1 );
    
    LL put_in_g2 = solve(nums,g1, g2 +nums[index], index + 1 );
    LL min_sum = std::min( put_in_g1, put_in_g2 );
    return min_sum;
}


int main() {
    int n;
    std::cin >> n;
    std::vector<LL> nums(n);
    std::copy(std::istream_iterator<LL>(std::cin), 
        std::istream_iterator<LL>(), std::begin(nums));
    std:: cout << solve(nums, 0, 0, 0);

    
}