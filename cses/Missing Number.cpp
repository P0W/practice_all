#include <iostream>
#include <string>
#include <vector>

using LL = long long;

int main() {
    
    LL n, item;
    std::cin >> n;
    
	LL  elements_sum = 0 ;
    for( LL i = 1; i < n ; ++i) {
		std::cin >> item;
        elements_sum += item;
    }

    LL  sum = n * (n+1) / 2;

    std::cout << sum - elements_sum;
    
    return 0;
}