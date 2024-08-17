#include <iostream>
#include <string>
#include <vector>
#include <iterator>
#include <algorithm>
#include <numeric>
#include <cmath>
#include <map>

using LL = long long;

int main() {
    LL n, target;
    std::cin >> n >> target;
    std::vector<LL> nums(n);
    std::map<LL, LL> sum_set;
    LL index = 0;
    std::copy( std::istream_iterator<LL>(std::cin), std::istream_iterator<LL>(), std::begin(nums));
    for( LL index=0; index < n; ++index) {
        sum_set.emplace(nums[index], index); 
    }
    
    while (index < n) {
        if(auto it = sum_set.find(target - nums[index]); it != sum_set.end()) {
            if (index != it->second) {
              
            std::cout <<index + 1 <<" " << it->second + 1 ;
            return 0;
                
        }
            }
        index ++;
           
    }

    std::cout << "IMPOSSIBLE";
    
}