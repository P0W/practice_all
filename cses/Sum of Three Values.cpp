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

int main() {
    int n, target_sum;
    std::cin >> n >> target_sum;
    std::vector<std::pair<LL, LL>> nums(n);

    //std::map<LL, LL> checker;
    LL i;
    
    for ( i = 0; i < n; ++i) {
        std::cin >> nums[i].first;
        nums[i].second = i + 1;
       // checker.emplace(i, i + 1);
    }

    std::sort( std::begin(nums), std::end(nums) );

    LL left = 0, right = n - 1;

    for ( i = 0; i < n - 2; ++i) {
        left = i + 1, right = n - 1;
        while (left < right) {
            LL three_sum =  nums[left].first + nums[right].first + nums[i].first;
            if (three_sum < target_sum) {
                left++;
            } else if ( three_sum > target_sum) {
                right--;
            } else {
                 LL a = nums[left].second ;
                 LL b = nums[right].second;
                 LL c = nums[i].second ;
                //if ( a!=b && b!= c && a !=c ) {
                    std::cout << a << " " << b << " " << c;
                    return 0;
                //}
            }
            
        }
    }


   /* for( i = 0; i < n - 1; ++i) {
        for (j = i +1; j < n; ++j) {
            LL two_sum =  nums[i] + nums[j];
            if ( auto it  = checker.find(target_sum -two_sum ); it != checker.end()) {
                if ( i != it->second && j != it->second) {
                    std::cout << i + 1 << " " << j + 1 << " " << it->second + 1;
                    return 0;
                }
            }
        }
    }*/

    std::cout << "IMPOSSIBLE";

}
