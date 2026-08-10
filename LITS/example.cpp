#include "lits/lits.hpp"

#include <fstream>
#include <iostream>
#include <string>
#include <sys/time.h>
#include <vector>

#define RESET "\033[0m"
#define RED "\033[31m"
#define GREEN "\033[32m"
#define YELLOW "\033[33m"
#define BLUE "\033[34m"

std::vector<std::string> strings;

char *raw_data;
int num_of_keys;
char **keys;
uint64_t *vals;

void readWords() {
    std::ifstream file("words.txt");
    if (!file) {
        std::cerr << "Fail to open words.txt" << std::endl;
        return;
    }
    std::string line;
    while (std::getline(file, line)) {
        strings.push_back(line);
    }
    file.close();
}

void prepareData() {
    readWords();
    uint64_t byte_size = 0, ofs = 0;
    for (int i = 0; i < strings.size(); ++i) {
        byte_size += strings[i].length() + 1;
    }
    std::cout << "Read " << strings.size() << " Keys from words.txt, "
              << byte_size << " Bytes at total." << std::endl;
    num_of_keys = strings.size();

    raw_data = new char[byte_size];
    keys = new char *[num_of_keys];
    vals = new uint64_t[num_of_keys];

    for (int i = 0; i < strings.size(); ++i) {
        keys[i] = raw_data + ofs;
        vals[i] = i + 1;
        memcpy(raw_data + ofs, strings[i].c_str(), strings[i].length() + 1);
        ofs += strings[i].length() + 1;
    }
    RT_ASSERT(ofs == byte_size);
}

void freeData() {
    strings.clear();
    delete[] raw_data;
    delete[] keys;
    delete[] vals;
}

void exampleMain() {
    lits::LITS index;
    index.bulkload((const char **)(keys), (const uint64_t *)(vals),
                   num_of_keys);

    std::string word1 = "internation";
    std::string word2 = "internal";
    std::string word3 = "intern";
    uint64_t value1 = 123, value2 = 789;
    int scan_range = 6;

    //=====[Example 1: Lookup]==============================================
    std::cout << "[Example 1][Lookup]: Try to search (" << YELLOW << word1
              << RESET << ") in the index ... ";
    auto result1 = index.lookup(word1.c_str());
    std::cout << RED << (result1 ? "found" : "not found") << RESET;
    if (result1) {
        std::cout << ", the value is " << result1->read() << std::endl;
    } else {
        std::cout << std::endl;
    }

    //=====[Example 2: Insert]==============================================
    std::cout << "[Example 2][Insert]: Try to insert (" << YELLOW << word1
              << RESET << ", " << BLUE << value1 << RESET
              << ") into the index ... ";
    bool result2 = index.insert(word1.c_str(), value1);
    std::cout << GREEN << (result2 ? "success" : "fail") << RESET << std::endl;

    //=====[Example 3: Lookup]==============================================
    std::cout << "[Example 3][Lookup]: Try to search (" << YELLOW << word1
              << RESET << ") in the index ... ";
    auto result3 = index.lookup(word1.c_str());
    std::cout << GREEN << (result3 ? "found" : "not found") << RESET;
    if (result3) {
        std::cout << ", the value is " << BLUE << result3->read() << RESET
                  << std::endl;
    } else {
        std::cout << std::endl;
    }

    //=====[Example 4: Upsert]==============================================
    std::cout << "[Example 4][Upsert]: Try to upsert (" << YELLOW << word1
              << RESET << ", " << BLUE << value2 << RESET
              << ") into the index ... ";
    auto result4 = index.upsert(word1.c_str(), value2);
    if (result4) {
        std::cout << "the value: (" << BLUE << result4 << RESET << ") -> ("
                  << BLUE << value2 << RESET << ")" << std::endl;
    } else {
        std::cout << "the value: (NULL) -> (" << BLUE << value2 << RESET << ")"
                  << std::endl;
    }

    //=====[Example 5: Lookup]==============================================
    std::cout << "[Example 5][Lookup]: Try to search (" << YELLOW << word1
              << RESET << ") in the index ... ";
    auto result5 = index.lookup(word1.c_str());
    std::cout << GREEN << (result5 ? "found" : "not found") << RESET;
    if (result5) {
        std::cout << ", the value is " << BLUE << result5->read() << RESET
                  << std::endl;
    } else {
        std::cout << std::endl;
    }

    //=====[Example 6: Delete]==============================================
    std::cout << "[Example 6][Delete]: Try to delete (" << YELLOW << word2
              << RESET << ") in the index ... ";
    auto result6 = index.remove(word2.c_str());
    std::cout << GREEN << (result6 ? "success" : "fail") << RESET << std::endl;

    //=====[Example 7: Scan]==============================================
    std::cout << "[Example 7][Scan]: Try to find (" << YELLOW << word3 << RESET
              << ") in the index ... ";
    auto result7 = index.find(word3.c_str());
    if (result7.valid()) {
        std::cout << GREEN << "found" << RESET << ", do a range 6 scan"
                  << std::endl;
        for (int i = 0; i < scan_range && result7.not_finish(); ++i) {
            auto _kv = result7.getKV();
            std::cout << "[Example 7][Scan]: (" << YELLOW << _kv->k << RESET
                      << ", " << BLUE << _kv->read() << RESET << ")"
                      << std::endl;
            result7.next();
        }
    } else {
        std::cout << RED << "not found" << RESET << std::endl;
    }

    index.destroy();
}

int main() {
    prepareData();
    exampleMain();
    freeData();
}