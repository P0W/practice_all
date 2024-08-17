#include <iostream>
#include <string>


using LL = long long;

int main() {
    
    LL n, result = 0;
    std::cin >> n;
    while ( n) {
        result += n/5;
        n/=5;
    }
    std::cout << result; 
}