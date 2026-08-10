# LITS+

LITS+ is a high-performance learned index structure. It contains two versions:

- **LIT+**: single-threaded version
- **LIT-MT**: multi-threaded version

### Common API (LIT+)

`Index` (defined in `litsPlus.hpp`) is the main entry class of the LIT+ index. Typical usage:

```cpp
litsPlus::Index<iter> idx;

// Bulk load: construct the index from sorted key-value pairs
idx.build(begin_iter, end_iter);

// Point lookup: returns Entry* on success, NULL if not found
Entry* e = idx.lookup(key, key_len);

// Upsert: insert a new key or update its value. Returns true for insert, false for update
bool inserted = idx.upsert(key, key_len, value);

// Scan: find() locates an existing key and returns an iterator; then use next()
// to scan forward. find() only supports positive search (key must exist).
auto iter = idx.find(key, key_len);
while (!iter.isFinish()) {
    Entry* e = iter.getEntry();
    // process e->key(), e->value() ...
    iter.next();
}

// Memory footprint in bytes
size_t sz = idx.getSizeInBytes();
```

The iterator also provides `isValid()` to check whether it is currently pointing to a valid entry. `append()` is a simpler insert-only variant that does not check for existing keys.

---

## Directory Structure

```
LITS+/               # Project overview (this file)
├── LIT+                    # Single-threaded version
│   ├── litsPlus/           # Core source code
│   │   ├── src/            # Index implementation
│   │   │   ├── litsPlus.hpp            # Main Index class
│   │   │   ├── litsPlus_base.hpp       # Base macros (USE_PMSS, etc.)
│   │   │   ├── litsPlus_mnode.hpp      # ModelNode (HPT node)
│   │   │   ├── litsPlus_cnode.hpp      # CompactModelNode
│   │   │   ├── litsPlus_cell.hpp       # CompactLeaf
│   │   │   ├── litsPlus_pmss.hpp       # PMSS latency prediction model
│   │   │   ├── litsPlus_hot.hpp        # HOT single-threaded trie wrapper
│   │   │   ├── litsPlus_iter.hpp       # Iterator
│   │   │   ├── litsPlus_slot.hpp       # Slot type definitions
│   │   │   └── ...
│   │   └── hot_src/        # HOT single-threaded trie source
│   ├── microbench/         # Performance benchmarks
│   │   ├── bench.cpp       # YCSB benchmark driver
│   │   └── utils.hpp
│   └── PMSS/               # PMSS latency model data, if you want to use LITS+ instead of LIT+, you should fill it by yourself
|
└── LIT-MT                  # Multi-threaded version
    ├── litmt/              # Core source code
    │   ├── litsRCU.hpp         # Main class
    │   ├── litsRCU_base.hpp    # Base macros (ENABLE_REBUILD, etc.)
    │   ├── litsRCU_mnode.hpp   # ModelNode
    │   ├── litsRCU_cnode.hpp   # CompactModelNode
    │   ├── litsRCU_ebr.hpp     # EBR (Epoch-Based Reclamation)
    │   ├── litsRCU_rebuild.hpp # Non-blocking rebuild
    │   └── ...
    └── microbench/         # Performance benchmarks
        └── insertScalability.cpp
```

---

## Build and Run

Both versions use the same build steps:

```bash
cd <version>      # enter LIT+ or LIT-MT
mkdir build && cd build
cmake ..
make -j$(nproc)
```

### Single-threaded Version (LIT+)

```bash
cd LIT+
mkdir build && cd build
cmake ..
make -j$(nproc)

# Run YCSB benchmark
./bench YCSB_C <data_file>
```

### Multi-threaded Version (LIT-MT)

```bash
cd LIT-MT
mkdir build && cd build
cmake ..
make -j$(nproc)

# Run benchmark
./insertScalability <data_file> <num_threads>
```

---

## Feature Configuration

### LIT+ Optional Features

Edit macros in `LIT+/litsPlus/src/litsPlus_base.hpp`:

| Macro | Description |
|---|---|
| `USE_PMSS` | Enable PMSS latency prediction model to run LITS+ |
| `USE_CELL_ROOT` | Enable Cell Root optimization to reduce root node memory usage |

PMSS model data must be placed in `LIT+/PMSS/` with files: `litR.csv`, `litW.csv`, `hotR.csv`, `hotW.csv`.

### LIT-MT Optional Features

Edit macros in `LIT-MT/litmt/litsRCU_base.hpp`:

| Macro | Description |
|---|---|
| `ENABLE_REBUILD` | Enable non-blocking rebuild so the index adapts to distribution changes |

---

## Dependencies

- C++17 compiler (GCC 7+ / Clang 9+)
- CMake >= 3.10
- Make
- LIT-MT additionally requires: TBB, jemalloc
