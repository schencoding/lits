#pragma once

#include <nmmintrin.h>
#include <cstdint>
#include <cstring>

inline uint8_t hashStr8(const char* key, int len) {
    uint32_t h = len;
    if (len) {
        h = _mm_crc32_u8(h, key[len - 1]);
        h = _mm_crc32_u8(h, key[len >> 1]);
    }
    return static_cast<uint8_t>(h & 0xff);
}
