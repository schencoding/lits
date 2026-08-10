#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>

// #define USE_CELL_ROOT
// #define USE_PMSS

namespace litsPlus {

inline constexpr size_t MaxCompactLeafSize = 64;

// For ASCII-Only strings, MaxChar is 128 (to save space in HPT)
// For bit streams, MaxChar is 256
inline constexpr size_t MaxChar = 128;

inline constexpr size_t MaxStack = 256;

// Return the common prefix length of string1 and string2
inline int ustrcpl(const char* s1, const char* s2)
{
    int i = 0;
    for (; s1[i] && s2[i] && s1[i] == s2[i]; ++i)
        ; /* do nothing, just iterate */
    return i;
}
};
