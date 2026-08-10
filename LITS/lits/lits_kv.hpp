#pragma once

#include <cstring>

#include "lits_base.hpp"
#include "lits_utils.hpp"

#define RAW_KV(x) ((kv *)PTR_RAW(x))

namespace lits {

class kv {
  public:
    uint64_t v; // The value
    char k[];   // The key

    /**
     * Set the key and value of the kv_load.
     *
     * @param key The key to be set
     * @param val The value to be set
     */
    void set(const str key, const uint64_t val) {
        // Copy the key to the memory of the kv_load
        memcpy(this->k, key, ustrlen(key) + 1);
        // Set the value of the kv_load
        this->v = val;
    }

    /**
     * Get the value of the kv_load.
     *
     * @return The value of the kv_load
     */
    inline uint64_t read() const {
        // Return the value of the kv_load
        return v;
    }

    /**
     * Verify that the remain part of the string is equal to the given key.
     *
     * @param key The key to be verified.
     * @param ofs The offset of the key to be verified (default: 0).
     *
     * @return True if the remain part of the string is equal to the given key,
     *         False otherwise.
     */
    inline bool verify(const str key, const int ofs = 0) const {
        // Compare the remain part of the string with the given key
        return ustrcmp(key + ofs, (char *)(this->k + ofs)) == 0;
    }

    /**
     * Compare the given key with the key of the kv_load.
     *
     * This function compares the given key with the key of the kv_load, and
     * returns an integer less than, equal to, or greater than zero if key is
     * found, respectively, to be less than, to match, or be greater than the
     * key of the kv_load.
     *
     * @param key The key to be compared with the key of the kv_load.
     * @param ofs The offset of the key to be compared (default: 0).
     *
     * @return An integer less than, equal to, or greater than zero if key is
     *         found, respectively, to be less than, to match, or be greater
     *         than the key of the kv_load.
     */
    inline int keycmp(const str key, const int ofs = 0) const {
        return ustrcmp(key + ofs, (char *)(this->k + ofs));
    }

    /**
     * Update the value of the kv_load.
     *
     * @param val The new value of the kv_load.
     */
    inline void update(const uint64_t val) { this->v = val; }

    /**
     * Verify a part of the string.
     *
     * This function checks if the given part of the string is equal to the
     * part of the key in the kv_load.
     *
     * @param key The key to be verified.
     * @param begin The starting index of the part of the key to be verified
     *              (inclusive).
     * @param end The ending index of the part of the key to be verified
     *            (exclusive).
     *
     * @return True if the given part of the string is equal to the part of the
     *         key in the kv_load, False otherwise.
     */
    inline bool part_verify(const str key, const int begin,
                            const int end) const {
        for (int i = begin; i < end; ++i) {
            if (key[i] != k[i]) {
                return false;
            }
        }
        return true;
    }

    /**
     * Get the size of the kv_load in bytes.
     *
     * This function returns the total size of the kv_load in bytes which
     * includes the size of the key, the value and the null terminator of the
     * key.
     *
     * @return The size of the kv_load in bytes.
     */
    inline size_t _len() const {
        size_t byte_sz = sizeof(uint64_t) + strlen(k) + 1;
        return byte_sz;
    }
};

/**
 * Create a new kv_pair.
 *
 * This function allocates memory for a new kv_pair and sets the key and value
 * of the new kv_pair. It returns a pointer to the new kv_pair.
 *
 * @param k The key of the new kv_pair.
 * @param v The value of the new kv_pair.
 *
 * @return A pointer to the new kv_pair.
 */
inline kv *new_kv(const str k, const uint64_t v) {
    size_t sz = sizeof(kv) + ustrlen(k) + 1; // +1 for null terminator
    kv *kvload = (kv *)new uint8_t[sz];      // allocate memory
    kvload->set(k, v);                       // set key and value
    return kvload;                           // return pointer to new kv_pair
}

/**
 * Create a new kv_pair with a hash value embedded in the pointer.
 *
 * This function allocates memory for a new kv_pair, sets the key and value of
 * the new kv_pair and embeds the hash value of the key in the pointer to the
 * kv_pair. It returns a pointer to the new kv_pair.
 *
 * @param k The key of the new kv_pair.
 * @param v The value of the new kv_pair.
 *
 * @return A pointer to the new kv_pair with the hash value embedded in the
 *         pointer.
 */
inline kv *new_hash_kv(const str k, const uint64_t v) {
    kv *ret = new_kv(k, v);
    uint64_t hash = hashStr(k);           // Calculate the hash value of the key
    uint64_t ptr = (uint64_t)(void *)ret; // Get the pointer to the kv_pair
    ptr = ptr | (hash << 48);             // Embed the hash value in the pointer
    return (kv *)(void *)ptr; // Return the pointer to the kv_pair with the hash
                              // value embedded in it
}

/**
 * Create a new kv_pair with a hash value embedded in the pointer from an
 * existing kv_pair.
 *
 * This function creates a new kv_pair with a hash value embedded in the pointer
 * to the kv_pair. The hash value is calculated from the key of the given
 * kv_pair and the pointer to the new kv_pair is returned.
 *
 * @param _kv The kv_pair to create a new kv_pair from.
 *
 * @return A pointer to the new kv_pair with the hash value embedded in the
 *         pointer.
 */
inline kv *new_hash_kv(kv *_kv) {
    uint64_t hash = hashStr(_kv->k);      // Calculate the hash value of the key
    uint64_t ptr = (uint64_t)(void *)_kv; // Get the pointer to the kv_pair
    ptr = ptr | (hash << 48);             // Embed the hash value in the pointer
    return (kv *)(void *)ptr; // Return the pointer to the kv_pair with the hash
                              // value embedded in it
}

/**
 * Extract the hash value from a pointer to a kv_pair with a hash value
 * embedded in it.
 *
 * This function extracts the hash value from the given pointer to a kv_pair
 * with a hash value embedded in it. The hash value is assumed to be in the
 * most significant 16 bits of the pointer value.
 *
 * @param ptr The pointer to the kv_pair with the hash value embedded in it.
 *
 * @return The hash value of the pointer.
 */
inline uint16_t getHashVal(const kv *ptr) {
    uint64_t v = (uint64_t)(void *)ptr; // Cast the pointer to an integer
    return (v >> 48) & 0xffff;          // Extract the hash value and return it
}

/**
 * @brief Destroy a kv_pair.
 *
 * This function destroys a kv_pair allocated using new_kv. The kv_pair
 * pointer should not be used after this function has been called.
 *
 * @param kv_load The kv_pair to destroy.
 */
void free_kv(kv *kv_load) {
    kv *raw_kv = (kv *)PTR_RAW(kv_load); // Get the pointer to the raw kv_pair
    delete[] reinterpret_cast<uint8_t *>(
        raw_kv); // Delete the raw kv_pair from memory
}

/**
 * Sub-Trie KV
 */
class ST_kv {
  public:
    kv *_kv;
    ST_kv() : _kv(NULL){};
    ST_kv(kv *kv) : _kv(kv){};
    ST_kv(const str _k, const uint64_t _v) { _kv = new_kv(_k, _v); }
    inline uint64_t read() const { return _kv->read(); }
    inline const char *getKey() const { return _kv->k; }
    inline kv *getKV() const { return _kv; }
};

}; // namespace lits