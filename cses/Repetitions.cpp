#include <iostream>
#include <string>


using LL = long long;

int main() {
    
    std::string input;
    std::cin >> input;
    char previous_char = input[0];

    int n = input.size();
    int cur_len = 1;
    int best = 1;
    for( int i= 1; i < n; ++i) {
        if(input[i] == previous_char ) {
            cur_len++;
            best = std::max(best, cur_len);
        } else {
            cur_len = 1;
        }
        previous_char = input[i];
    }

    std::cout << best;
}