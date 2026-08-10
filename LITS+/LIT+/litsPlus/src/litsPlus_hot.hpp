#pragma once

// For HOT tail trie"
#include "hot_src/HOTSingleThreaded.hpp"
#include "hot_src/HOTSingleThreadedInterface.hpp"

#include <litsPlus_entry.hpp>

#include <cmath>
#include <unordered_map>

namespace litsPlus {

    /**
     * Sub-Trie KV
     */
    class ST_kv {
    public:
        Entry* _kv;
        ST_kv()
            : _kv(NULL) {};
        ST_kv(Entry* kv)
            : _kv(kv) {};
        ST_kv(const char* _k, const uint64_t _v) { _kv = Entry::create(_k, _v); }
        inline uint64_t read() const { return _kv->value(); }
        inline const char* getKey() const { return _kv->key(); }
        inline Entry* getKV() const { return _kv; }
    };

    // Key extractor used in HOT
    template <typename T>
    class KeyExtracter {
    public:
        using KeyType = char const*;
        KeyType operator()(const T& value) { return value._kv->key(); }
        KeyType operator()(const KeyType k) { return k; }
    };

    using HOTIterInLIT = hot::singlethreaded::HOTSingleThreadedIterator<ST_kv>;
    using HOTIndexInLIT = hot::singlethreaded::HOTSingleThreaded<ST_kv, KeyExtracter>;

    // Assert the size of HOTIndexInLIT, for single threaded LITS, the sizeof HOTIndexInLIT
    // must be 8 bytes
    static_assert(sizeof(HOTIndexInLIT) == sizeof(uint64_t));

    /**
     * @brief Find a key in the HOTIndexInLIT.
     *
     * @param index The HOTIndexInLIT object to be searched.
     * @param k The key to be found.
     *
     * @return An iterator to the found key or index.end() if not found.
     */
    inline auto HOTFind(const HOTIndexInLIT& index, const char* k) -> HOTIterInLIT {
        return index.find(k);
    }

    /**
     * @brief Returns an iterator to the first element of the HOTIndexInLIT.
     *
     * @param index The HOTIndexInLIT object to be iterated over.
     *
     * @return An iterator to the first element of the HOTIndexInLIT.
     */
    inline auto HOTBegin(const HOTIndexInLIT& index) -> HOTIterInLIT {
        // Returns an iterator to the first element of the HOTIndexInLIT.
        return index.begin();
    }

    /**
     * @brief Insert a key-value pair into the HOTIndexInLIT.
     *
     * @param index The HOTIndexInLIT object to be inserted into.
     * @param k The key to be inserted.
     * @param v The value to be inserted.
     *
     * @return true if the insertion is successful, false if the key already
     * exists.
     */
    inline bool HOTInsert(HOTIndexInLIT& index, const char* k, const uint64_t v) {
        // Try to insert the key-value pair into the HOTIndexInLIT.
        auto __kv = ST_kv(k, v);
        return index.insert(__kv);
    }

    /**
     * @brief Insert a key-value pair into the HOTIndexInLIT.
     *
     * @param index The HOTIndexInLIT object to be inserted into.
     * @param _kv The key-value pair to be inserted.
     *
     * @return true if the insertion is successful, false if the key already
     * exists.
     */
    inline bool HOTInsert(HOTIndexInLIT& index, Entry* _kv) {
        // Insert the key-value pair into the HOTIndexInLIT.
        auto __kv = ST_kv(_kv);
        return index.insert(__kv);
    }

    /**
     * @brief Lookup a key in the HOTIndexInLIT.
     *
     * @param index The HOTIndexInLIT object to be searched.
     * @param k The key to be found.
     *
     * @return A pointer to the found key-value pair or NULL if not found.
     */
    inline Entry* HOTLookup(HOTIndexInLIT& index, const char* k) {
        // Lookup the key in the HOTIndexInLIT.
        auto ret = index.lookup(k);

        // If the key is found, return a pointer to the key-value pair,
        // otherwise return NULL.
        return ret.mIsValid ? ret.mValue.getKV() : NULL;
    }

    /**
     * @brief Insert or update a key-value pair in the HOTIndexInLIT.
     *
     * @param index The HOTIndexInLIT object to be inserted or updated.
     * @param k The key to be inserted or updated.
     * @param v The value to be inserted or updated.
     *
     * @return A pointer to the inserted or updated key-value pair, or NULL if
     * insertion fails due to key already existing.
     */
    inline Entry* HOTUpsert(HOTIndexInLIT& index, const char* k, const uint64_t v) {
        auto res = index.upsert(ST_kv(k, v));
        /// Return the inserted or updated key-value pair if successful,
        /// otherwise return NULL.
        return res.mIsValid ? res.mValue.getKV() : NULL;
    }

    inline bool HOTRemove(HOTIndexInLIT& index, const char* k) {
        return index.remove(k);
    }

    template <typename EntryIter>
    inline void HOTBulkload(HOTIndexInLIT& index, EntryIter begin, EntryIter end) {
        // Insert the key-value pairs into the HOTIndexInLIT in bulk.
        for (auto it = begin; it != end; ++it) {
            // index.insert(ST_kv(kvs[i].k, kvs[i].v));
            HOTInsert(index, (*it));
        }
    }

};  // namespace lits
