#pragma once

#include "lits_base.hpp"
#include "lits_kv.hpp"

// For HOT tail trie
// #include "hot_src/HOTSingleThreaded.hpp"
// #include "hot_src/HOTSingleThreadedInterface.hpp"
#include "hot_src/HOTSingleThreaded.hpp"
#include "hot_src/HOTSingleThreadedInterface.hpp"

#include <cmath>

namespace lits {

// Key extractor used in HOT
template <typename T> class KeyExtracter {
  public:
    using KeyType = char const *;
    KeyType operator()(const T &value) { return value._kv->k; }
    KeyType operator()(const KeyType k) { return k; }
};

using HOTIter = hot::singlethreaded::HOTSingleThreadedIterator<ST_kv>;
using HOTIndex = hot::singlethreaded::HOTSingleThreaded<ST_kv, KeyExtracter>;

// Assert the size of HOTIndex, for single threaded LITS, the sizeof HOTIndex
// must be 8 bytes
ST_ASSERT(sizeof(HOTIndex) == sizeof(uint64_t));

/**
 * @brief Find a key in the HOTIndex.
 *
 * @param index The HOTIndex object to be searched.
 * @param k The key to be found.
 *
 * @return An iterator to the found key or index.end() if not found.
 */
inline auto HOTFind(const HOTIndex &index, const str k) -> HOTIter {
    return index.find(k);
}

/**
 * @brief Returns an iterator to the first element of the HOTIndex.
 *
 * @param index The HOTIndex object to be iterated over.
 *
 * @return An iterator to the first element of the HOTIndex.
 */
inline auto HOTBegin(const HOTIndex &index) -> HOTIter {
    // Returns an iterator to the first element of the HOTIndex.
    return index.begin();
}

/**
 * @brief Insert a key-value pair into the HOTIndex.
 *
 * @param index The HOTIndex object to be inserted into.
 * @param k The key to be inserted.
 * @param v The value to be inserted.
 *
 * @return true if the insertion is successful, false if the key already
 * exists.
 */
inline bool HOTInsert(HOTIndex &index, const str k, const uint64_t v) {
    // Try to insert the key-value pair into the HOTIndex.
    return index.insert(ST_kv(k, v));
}

/**
 * @brief Insert a key-value pair into the HOTIndex.
 *
 * @param index The HOTIndex object to be inserted into.
 * @param _kv The key-value pair to be inserted.
 *
 * @return true if the insertion is successful, false if the key already
 * exists.
 */
inline bool HOTInsert(HOTIndex &index, kv *_kv) {
    // Insert the key-value pair into the HOTIndex.
    return index.insert(ST_kv(_kv));
}

/**
 * @brief Lookup a key in the HOTIndex.
 *
 * @param index The HOTIndex object to be searched.
 * @param k The key to be found.
 *
 * @return A pointer to the found key-value pair or NULL if not found.
 */
inline kv *HOTLookup(HOTIndex &index, const str k) {
    // Lookup the key in the HOTIndex.
    auto ret = index.lookup(k);

    // If the key is found, return a pointer to the key-value pair,
    // otherwise return NULL.
    return ret.mIsValid ? ret.mValue.getKV() : NULL;
}

/**
 * @brief Insert or update a key-value pair in the HOTIndex.
 *
 * @param index The HOTIndex object to be inserted or updated.
 * @param k The key to be inserted or updated.
 * @param v The value to be inserted or updated.
 *
 * @return A pointer to the inserted or updated key-value pair, or NULL if
 * insertion fails due to key already existing.
 */
inline kv *HOTUpsert(HOTIndex &index, const str k, const uint64_t v) {
    auto res = index.upsert(ST_kv(k, v));
    /// Return the inserted or updated key-value pair if successful,
    /// otherwise return NULL.
    return res.mIsValid ? res.mValue.getKV() : NULL;
}

inline bool HOTRemove(HOTIndex &index, const str k) { return index.remove(k); }

/**
 * @brief Bulkload a range of key-value pairs into the HOTIndex.
 *
 * This function inserts a range of key-value pairs into the HOTIndex in
 * bulk. The range of key-value pairs to be inserted is specified by the
 * half-open interval [l, r).
 *
 * @param index The HOTIndex object to be bulkloaded.
 * @param kvs A container of key-value pairs to be bulkloaded.
 * @param l The start index of the range of key-value pairs to be bulkloaded
 *          (inclusive).
 * @param r The end index of the range of key-value pairs to be bulkloaded
 *          (exclusive).
 */
template <class record>
inline void HOTBulkload(HOTIndex &index, const record &kvs, const int l,
                        const int r) {
    // Insert the key-value pairs into the HOTIndex in bulk.
    for (int i = l; i < r; ++i) {
        index.insert(ST_kv(kvs[i].k, kvs[i].v));
    }
}

}; // namespace lits
