/**
 * LITS is a learned index optimized for strings, it can support lookup, scan,
 * as well as online update.
 *
 * Author:  yifan yang
 * Company: ICT DB
 * Contact: yangyifan22z@ict.ac.cn
 * Last Modified Date: 2024-03-21 09:13:52 (Beijing Time)
 *
 * Copyright (c) 2024 ICT DB. All rights reserved.
 */

#pragma once

#include <assert.h>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

// The MAX alphabet size which LITS supports.
#define MAX_CH 128

// The MAX length for strings
#define MAX_DEPTH 256

// The MAX DEPTH for insertion stack
#define MAX_STACK 128

// Build-in branch prediction hinter
#define likely(x) __builtin_expect((x), 1)
#define unlikely(x) __builtin_expect((x), 0)

// Runtime Assertion and Static Assertion
#define RT_ASSERT(expr) assert(expr)
#define ST_ASSERT(expr) static_assert(expr)

// Pointer RAW mask
#define PTR_MASK 0xffffffffffffUL
#define PTR_RAW(p) ((void*)(((uint64_t)(void*)(p)) & PTR_MASK))

// Debug COUT
#define COUT_VAR(x) std::cout << #x << " = " << (x) << std::endl
#define COUT_THIS(this)                 \
    do {                                \
        std::cout << this << std::endl; \
    } while (0)

// #define LIT
#define CNODE_SIZE 16

// The scale factor of the sparse item array
#define ScaleFactor 2

// lits namespace
namespace lits {

    // Key type: str
    using str = char*;

    // Value type: uint64_t
    using val = uint64_t;

};  // namespace lits
