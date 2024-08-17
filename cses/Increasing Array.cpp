#include <iostream>
#include <string>


using LL = long long;

int main() {
    
    LL n, item, result = 0;
    std::cin >> n;
    std::cin >> item ;
    LL max_so_far = item;
    for ( LL i = 1; i < n; ++i) {
        std::cin >> item ;
        max_so_far = std::max(max_so_far, item);
        result += max_so_far - item;
    }

    std::cout << result;
    
}