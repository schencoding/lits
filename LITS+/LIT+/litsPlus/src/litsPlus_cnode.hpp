#pragma once

#include <litsPlus_base.hpp>
#include <litsPlus_entry.hpp>
#include <litsPlus_hash.hpp>

#include <immintrin.h>
#include <bitset>
#include <cassert>
#include <string>
#include <vector>

namespace litsPlus {

    class CompactLeaf {
    private:
        uint8_t fingerPrints[MaxCompactLeafSize];
        bool sorted = false;
        std::vector<Entry*> records;

    public:
        CompactLeaf() = delete;
        CompactLeaf(const CompactLeaf& other) = delete;
        CompactLeaf(CompactLeaf&&) = delete;
        CompactLeaf& operator=(const CompactLeaf&) = delete;
        CompactLeaf& operator=(CompactLeaf&&) = delete;

        bool isSorted() const {
            return this->sorted;
        }

        template <typename EntryIter>
        CompactLeaf(EntryIter begin, EntryIter end) {
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

        void SelfSort() {
            const size_t sz = records.size();
            if (sz <= 1)
                return;

            std::sort(records.begin(), records.end(),
                      [](const Entry* a, const Entry* b) {
                          return std::strcmp(a->key(), b->key()) < 0;
                      });

            for (size_t i = 0; i < sz; ++i)
                fingerPrints[i] = hashStr8(records[i]->key(), records[i]->len());

            this->sorted = true;
        }

        CompactLeaf(Entry* e1, Entry* e2) {
            records.resize(2);
            fingerPrints[0] = hashStr8(e1->key(), e1->len());
            fingerPrints[1] = hashStr8(e2->key(), e2->len());
            records[0] = e1;
            records[1] = e2;
        }

        bool append(Entry* newEntry) {
            // Get the hash value
            const uint8_t fp = hashStr8(newEntry->key(), newEntry->len());

            // Get the dataSize
            const int dataSize = this->records.size();

            // The node is full
            if (dataSize == MaxCompactLeafSize - 1) {
                return false;
            }

            // Set the finger prints
            fingerPrints[dataSize] = fp;

            // Append the new entry
            this->records.push_back(newEntry);

            // Unset the sorted flag
            this->sorted = false;

            return true;
        }

        bool upsert(const char* key, const int keyLen, const uint64_t value, int ccpl, bool& exists) {
            // Get the hash value
            const uint8_t fp = hashStr8(key, keyLen);

            // Get the dataSize
            const int dataSize = this->records.size();

            // Store the 512-vec
            const uint8_t* fptr = fingerPrints;
            uint64_t umask = (dataSize >= 64) ? ~0ULL : ((1ULL << dataSize) - 1);
            __m512i v = _mm512_maskz_loadu_epi8(umask, fptr);

            // Compare the fingerprints
            uint64_t mask = _mm512_cmpeq_epi8_mask(v,
                                                   _mm512_set1_epi8(static_cast<char>(fp)));
            mask &= umask;

            // Double check all the mask matched
            while (mask) {
                unsigned idx = _tzcnt_u64(mask);
                if (records[idx]->verify(key, keyLen, ccpl)) {
                    exists = true;
                    records[idx]->setValue(value);
                    return true;
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
            this->records.push_back(Entry::create(key, keyLen, value));

            // Unset the sorted flag
            this->sorted = false;

            return true;
        }

        Entry* lookup(const char* key, const int keyLen, const int ccpl = 0) const {
            // Get the hash value
            const uint8_t fp = hashStr8(key, keyLen);

            // Get the dataSize
            const int dataSize = this->records.size();

            // Store the 512-vec
            const uint8_t* fptr = fingerPrints;
            uint64_t umask = (dataSize >= 64) ? ~0ULL : ((1ULL << dataSize) - 1);
            __m512i v = _mm512_maskz_loadu_epi8(umask, fptr);

            // Compare the fingerprints
            uint64_t mask = _mm512_cmpeq_epi8_mask(v,
                                                   _mm512_set1_epi8(static_cast<char>(fp)));
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

        // In CompactLeaf: Implement the returnKeyIdx method
        int returnKeyIdx(const char* key, const int keyLen, const int ccpl = 0) const {
            // 1. Get the fingerprint hash value for the key
            const uint8_t fp = hashStr8(key, keyLen);

            // 2. Get the size of the data (number of records in the leaf)
            const int dataSize = this->records.size();

            // 3. Get the fingerprint array
            const uint8_t* fptr = fingerPrints;
            // Generate a mask for the number of records (only relevant for dataSize < 64)
            uint64_t umask = (dataSize >= 64) ? ~0ULL : ((1ULL << dataSize) - 1);
            __m512i v = _mm512_maskz_loadu_epi8(umask, fptr);

            // 4. Compare the fingerprints of records with the given key's fingerprint
            uint64_t mask = _mm512_cmpeq_epi8_mask(v,
                                                   _mm512_set1_epi8(static_cast<char>(fp)));
            mask &= umask;

            // 5. Check all matching fingerprints for the actual record match
            while (mask) {
                // Get the index of the matched fingerprint
                unsigned idx = _tzcnt_u64(mask);

                // If the fingerprint matches, check if the record also matches by verifying the key
                if (records[idx]->verify(key, keyLen, ccpl)) {
                    return idx;  // Return the index of the matching record
                }

                // Clear the matched fingerprint from the mask
                mask &= mask - 1;
            }

            // 6. No matching record found
            return -1;  // Return -1 to indicate no match found
        }

        Entry* getEntry(const int idx) const {
            return records[idx];
        }

        int getCount() const {
            return records.size();
        }

        std::vector<Entry*> extract() {
            std::vector<Entry*> result;
            for (auto it = records.begin(); it != records.end(); ++it) {
                result.push_back(*it);
            }
            return result;
        }

        size_t getSizeInBytes() const {
            return sizeof(fingerPrints) + records.size() * sizeof(Entry*);
        }
    };
};