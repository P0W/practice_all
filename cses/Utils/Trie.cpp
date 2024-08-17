#include <unordered_map>
#include <string>
#include <memory>
#include <iostream>
#include <queue>
#include <ranges>

class Trie {

    public:
        Trie () : m_root(std::make_shared<Node>('*')) {
        
        }

        bool insertWord(const std::string&  word) {
            std::shared_ptr<Node> current = m_root;
            for(const auto& ch: word) {
                if(auto it = current->childs.find(ch); it != current->childs.end()) {
                    current = it->second;
                } else{
                    current->childs.emplace(ch, std::make_shared<Node>(ch));
                    current = current->childs[ch];
                }
            }
            current->isWord = true;
            

            return true;
        }

        bool searchWord(const std::string& word) const {
            std::shared_ptr<Node> current = m_root;
            for(const auto& ch: word) {
                if(auto it = current->childs.find(ch); it != current->childs.end()) {
                    current = it->second;
                } else{
                    return false;
                }
            }
            return current->isWord;
        }

        void display() const {
            std::queue<std::shared_ptr<Node>> q;
            q.emplace(m_root);
            while (!q.empty()) {
                size_t q_sz = q.size();
                while (q_sz--) {
                    auto curr = q.front();
                    q.pop();
                    std::cout << curr->ch << " ";
                    for(const auto& [ch, neigh]: curr->childs){
                        std::cout << "-> "<< ch ;
                        q.emplace(neigh);
                    }
                    std::string word = curr->isWord ? " (Word)" : "";
                    std::cout <<word <<"\n";
                }
            }
        }

    private:
    struct Node {
        bool isWord;
        std::unordered_map<char, std::shared_ptr<Node>> childs;
        char ch;
        explicit Node(char ch) : isWord{false},childs{}, ch{ch} {

        }
    };

    std::shared_ptr<Node> m_root;
};

int main() {
    Trie trieObj;

    trieObj.insertWord("jack");
    trieObj.insertWord("acne");
    

    std::string_view test_doc = "A quick brown fox jumps over a lazy dog";
    for (auto&& word : test_doc | std::ranges::views::split(' ')) {
        std::string curr_word {word.begin(), word.end()};
        trieObj.insertWord(curr_word);
    }
    
    trieObj.display();
    for (auto&& word : {"jack", "acne", "quick", "red", "fox", "jackal"}) {
        std::cout << "search for " << word << std::boolalpha << " " << trieObj.searchWord(word) << "\n";
    }
}