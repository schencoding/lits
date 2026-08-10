#pragma once

#include <litsRCU_base.hpp>
#include <litsRCU_entry.hpp>
#include <litsRCU_hash.hpp>

#include <cassert>
#include <immintrin.h>
#include <string>
#include <vector>

namespace litsRCU {

/**
 * There is only at most one writer happens on Cnode.
 */

class CompactLeaf {
  private:
    uint8_t fingerPrints[MaxCompactLeafSize];
    std::vector<Entry *> records;

  public:
    CompactLeaf() = delete;
    CompactLeaf(CompactLeaf &&) = delete;
    CompactLeaf &operator=(const CompactLeaf &) = delete;
    CompactLeaf &operator=(CompactLeaf &&) = delete;

    CompactLeaf(const CompactLeaf &other) {
        memcpy(fingerPrints, other.fingerPrints, MaxCompactLeafSize);

        records.resize(other.records.size());
        for (int i = 0; i < other.records.size(); ++i) {
            records[i] = other.records[i];
        }
    }

    template <typename EntryIter> CompactLeaf(EntryIter begin, EntryIter end) {
        // The data size
        const int dataSize = std::distance(begin, end);

        // Reserve the space for records
        records.resize(dataSize);

        // Compact Leaf Capacity
        assert(dataSize <= MaxCompactLeafSize);

        // Put the entries
        size_t idx = 0;
        for (auto it = begin; it != end; ++it, ++idx) {
            fingerPrints[idx] = hashStr8((*it)->key(), (*it)->len());
            records[idx] = *it;
        }
    }

    CompactLeaf(Entry *e1, Entry *e2) {
        records.resize(2);
        fingerPrints[0] = hashStr8(e1->key(), e1->len());
        fingerPrints[1] = hashStr8(e2->key(), e2->len());
        records[0] = e1;
        records[1] = e2;
    }

    bool append(Entry *newEntry, const int ccpl, bool &exist) {
        // Get the hash value
        const uint8_t fp = hashStr8(newEntry->key(), newEntry->len());

        // Get the dataSize
        const int dataSize = this->records.size();

        // Store the 512-vec
        const uint8_t *fptr = fingerPrints;
        uint64_t umask = (dataSize >= 64) ? ~0ULL : ((1ULL << dataSize) - 1);
        __m512i v = _mm512_maskz_loadu_epi8(umask, fptr);

        // Compare the fingerprints
        uint64_t mask = _mm512_cmpeq_epi8_mask(v, _mm512_set1_epi8(static_cast<char>(fp)));

        // Clean the dirty upper bits
        mask &= umask;

        // Double check all the mask matched
        while (mask) {
            unsigned idx = _tzcnt_u64(mask);
            if (records[idx]->verify(newEntry->key(), newEntry->len(), ccpl)) {
                exist = true;
                return false;
            }
            mask &= mask - 1;
        }

        // The node is full
        if (dataSize == MaxCompactLeafSize - 1) {
            return false;
        }

        // Set the finger prints
        fingerPrints[dataSize] = fp;

        // Append the new entry
        this->records.push_back(newEntry);

        return true;
    }

    Entry *lookup(const char *key, const int keyLen, const int ccpl = 0) const {
        // Get the hash value
        const uint8_t fp = hashStr8(key, keyLen);

        // Get the dataSize
        const int dataSize = this->records.size();

        // Store the 512-vec
        const uint8_t *fptr = fingerPrints;
        uint64_t umask = (dataSize >= 64) ? ~0ULL : ((1ULL << dataSize) - 1);
        __m512i v = _mm512_maskz_loadu_epi8(umask, fptr);

        // Compare the fingerprints
        uint64_t mask = _mm512_cmpeq_epi8_mask(v, _mm512_set1_epi8(static_cast<char>(fp)));

        // Clean the dirty upper bits
        mask &= umask;

        // Double check all the mask matched
        while (mask) {
            unsigned idx = _tzcnt_u64(mask);
            if (records[idx]->verify(key, keyLen, ccpl)) {
                return records[idx];
            }
            mask &= mask - 1;
        }

        // No records matched
        return NULL;
    }

    std::vector<Entry *> extract() const {
        std::vector<Entry *> result;
        for (auto it = records.begin(); it != records.end(); ++it) {
            result.push_back(*it);
        }
        return result;
    }

    size_t getSizeInBytes() const {
        return sizeof(fingerPrints) + records.size() * sizeof(Entry *);
    }
};
}; // namespace litsRCU