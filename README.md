# LITS Project

This repository contains the implementation of **LITS** (Learned Index for Strings) and its extended version **LITS+**.

## Overview

- **LITS** (VLDB 2024): A learned index optimized for string keys.
  - Paper: [LITS: An Optimized Learned Index for Strings](https://www.vldb.org/pvldb/vol17/p3415-chen.pdf)

- **LITS+** (TODS 2025): An extended version with improved performance and additional features, including both single-threaded (LIT+) and multi-threaded (LIT-MT) implementations.
  - Paper: To be announced

## Repository Structure

```
lits/
├── LITS/          # Original VLDB 2024 implementation
├── LITS+/         # Extended TODS 2025 implementation
└── README.md      # This file
```

## Quick Start

### LITS+ (Extended Version)

Both single-threaded (LIT+) and multi-threaded (LIT-MT) versions follow the same build process:

```bash
cd LITS+/<version>   # <version> is either LIT+ or LIT-MT
mkdir build && cd build
cmake ..
make -j$(nproc)
```

Then run the respective benchmarks as described in the subdirectory READMEs.

## Datasets and Training Code

The datasets used in the papers and the synthetic datasets for PMSS, along with the training code, are available at:
https://pan.baidu.com/s/1ZyA6YsdZoO4Pt6t2suKoMw?pwd=m5dl

## Dependencies

- C++17 compiler (GCC 7+ / Clang 9+)
- CMake >= 3.10
- Make
- For LIT-MT (multi-threaded version): TBB, jemalloc
