#include <iostream>
#include <string>
#include <algorithm>
#include <unordered_map>

using LL = long long;

std::unordered_map<char, int> freq;

LL fact(LL n) {
    LL result = 1;
    for( int i =2; i <=n; ++i) {
        result *=i;
    }
    return result;
}
int main() {
    std::string input;
    std::cin >> input;
    int n = input.size();
    for( int i = 0; i < n; ++i) {
        freq[input[i]]++;
    }
    int result = fact(n) ;
    for ( const auto& [k, v]: freq) {
        result /= fact(v);
    }
    std::cout << result << "\n";

    std::sort(input.begin(), input.end());
    do {
        std::cout << input << "\n";
    }while(std::next_permutation(input.begin(), input.end()));
}

// 3 2 7 4 1
// 1 2 3 4 100 =>110/2 =>55