#pragma once

#include <byteswap.h>
#include <math.h>
#include <cassert>
#include <fstream>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

#include "code_assigner_factory.hpp"
#include "double_char_encoder.hpp"
#include "rs/multi_map.h"
#include "symbol_selector_factory.hpp"

// Span is 8 bytes
#define LEN 8

// Key-Value pair in RSS
typedef std::pair<std::string, uint64_t> kv_pair;

// Convert the string's 8 bytes section to a uint64_t

inline uint64_t Str2Int(const std::string& str, int depth) {
    uint8_t buf[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    for (int i = 0; i < 8 && i + depth < str.length(); ++i) {
        buf[i] = *(str.c_str() + depth + i);
    }
    return bswap_64(*(uint64_t*)(buf));
}
// inline uint64_t Str2Int(const std::string& str, int depth) {
//     if (depth == str.length())
//         return 0;
//     uint64_t ret = 0;
//     for (int i = 0; i < LEN; ++i) {
//         ret |= ((uint64_t)(str.c_str()[depth + i]) << (56 - i * LEN));
//         if ((depth + i) == str.length() - 1)
//             break;
//     }
//     return ret;
// }

// Binary Search for kv_pair
inline kv_pair BinarySearch(const std::vector<kv_pair>& kvs,
                            const std::string& key,
                            int l,
                            int r) {
    const int l_raw = l;
    const int r_raw = r;
    while (l <= r) {
        int mid = l + (r - l) / 2;
        if (kvs[mid].first == key) {
            return kvs[mid];
        } else if (kvs[mid].first < key) {
            l = mid + 1;
        } else {
            r = mid - 1;
        }
    }

    // Return a default kv_pair if fail to find
    return kv_pair{"", 0};
}

int GetByteLen(const int bitlen) {
    return ((bitlen + 7) & ~7) / 8;
}

std::vector<std::string> LoadAllData(const std::string& fname) {
    std::vector<std::string> keys;
    std::ifstream infile(fname);
    std::string key;

    if (!infile) {
        std::cerr << "Failed to open file: " << fname << std::endl;
        exit(0);
    }

    while (getline(infile, key)) {
        // Skip strings whose length > 255
        if (key.length() > 255) {
            continue;
        }

        // Skip strings whose length == 0
        if (key.length() == 0) {
            continue;
        }

        // Skip strings whose char is non-ASCII
        bool is_nonASCII = false;
        for (auto& c : key)
            if ((((uint8_t)c) >= 128) || ((uint8_t)c < 32)) {
                is_nonASCII = true;
                break;
            }
        if (is_nonASCII) {
            continue;
        }
        keys.push_back(key);
    }

    std::sort(keys.begin(), keys.end());

    infile.close();

    return keys;
}

std::vector<std::string> codeKeys(std::vector<std::string>& keys,
                                  hope::DoubleCharEncoder& coder) {
    std::vector<std::string> ckeys;
    coder.build(keys, 65536);
    auto buffer = new uint8_t[4096];
    std::string lastString = "";

    for (int i = 0; i < static_cast<int>(keys.size()); i++) {
        int len = coder.encode(keys[i], buffer);
        std::string str1 = std::string((const char*)buffer, GetByteLen(len));
        ckeys.push_back(str1);
        assert(str1 > lastString);
        lastString = str1;
    }

    return ckeys;
}
