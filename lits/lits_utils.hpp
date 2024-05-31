#pragma once

#include "lits_base.hpp"
#include "lits_gpkl.hpp"

namespace lits {

/**
 * Check if the input data is sorted and unique
 * @return true if unique and sorted, false otherwise
 */
inline bool checkSortedUnique(const str *keys, const int len) {
    for (int i = 1; i < len; ++i) {
        if (ustrcmp(keys[i], keys[i - 1]) <= 0) {
            return false;
        }
    }
    return true;
}

/**
 * A simple inline hash function for string without go through all bytes.
 */
inline uint16_t hashStr(const str key) {
    uint16_t ret = ustrlen(key);
    uint16_t c1 = key[ret / 2];
    uint16_t c2 = key[2 * ret / 3];
    uint16_t c3 = key[4 * ret / 5];
    return ret ^ c1 ^ c2 ^ c3;
}

/**
 * Return the smallest 2**k which is bigger or equal than n.
 * Return 2 for 2
 * Return 4 for (2, 4]
 * Return 8 for (4, 8]
 */
inline uint64_t quick2(uint64_t n) {
    n--;
    n |= n >> 1;
    n |= n >> 2;
    n |= n >> 4;
    n |= n >> 8;
    n |= n >> 16;
    n |= n >> 32;
    n++;
    return n;
}

/**
 * Return a randomly generated uint64_t.
 */
inline uint64_t rand64() {
    return ((uint64_t)rand() << 60) ^ ((uint64_t)rand() << 45) ^
           ((uint64_t)rand() << 30) ^ ((uint64_t)rand() << 15) ^
           ((uint64_t)rand());
}

/**
 * Return a randomly generated workload.
 */
std::vector<std::string> randomInput(str *keys, int len) {
    const long basic_cnt = 1000000;
    std::vector<std::string> res;
    int idx;

    for (long i = 0; i < basic_cnt; ++i) {
        idx = rand64() % len;
        res.push_back(std::string(keys[idx]));
    }

    return res;
}

}; // namespace lits