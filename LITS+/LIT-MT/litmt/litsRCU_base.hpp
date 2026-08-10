#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>

// #define ENABALE_REBUILD

#define unlikely(x) __builtin_expect(!!(x), 0)

namespace litsRCU {

    inline constexpr size_t MaxCompactLeafSize = 64;

    // For ASCII-Only strings, MaxChar is 128 (to save space in HPT)
    // For bit streams, MaxChar is 256
    inline constexpr std::size_t MaxChar = 128;

    // Return the common prefix length of string1 and string2
    inline int ustrcpl(const char* s1, const char* s2) {
        int i = 0;
        for (; s1[i] && s2[i] && s1[i] == s2[i]; ++i)
            ; /* do nothing, just iterate */
        return i;
    }
};