#include <iostream>
#include <string>
#include <vector>

using LL = long long;

int main() {
    
    LL input;
    std::cin >> input;
    std::cout << input << " ";
    while(input > 1) {
        if(input & 1) {
            input  =input*3 + 1;
            std::cout << input << " ";
        } else {
            input /= 2;
            std::cout << input << " ";
        }
    }
    
    return 0;
}