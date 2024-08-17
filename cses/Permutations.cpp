#include <iostream>
#include <string>


using LL = long long;

int main() {
    
    LL n;
    std::cin >> n;
    if ( n!=1 && n<=3) {
        std::cout << "NO SOLUTION";
    } else {
        int i = 0;
        //if ( n & 1) {
            for ( i = n-1; i >= 1; i-=2) {
                std::cout << i << " ";
            }
            for ( i = n; i >=1; i-=2) {
                std::cout << i << " ";
            }
        //}
        
        
    }
    
}