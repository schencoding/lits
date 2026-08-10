#pragma once

#include <litsPlus_base.hpp>
#include <litsPlus_slot.hpp>

namespace litsPlus {

    class Cell {
    public:
        static constexpr int BitPerWord = 64;
        static constexpr int WordPerCell = 4;
        static constexpr int SlotPerCell = 3;
        static constexpr int BitsPerCell = WordPerCell * BitPerWord;

    public:
        static size_t getSizeInBytes() {
            return sizeof(Cell);
        }

        void setBit(uint32_t ofs) {
            const int word_idx = ofs / BitPerWord;
            const int word_ofs = ofs % BitPerWord;
            this->bits[word_idx] |= (1ULL << word_ofs);
        }

        inline int rank(const uint32_t ofs) const {
            const int word_idx = ofs / BitPerWord;
            const int word_ofs = ofs % BitPerWord;
            const uint64_t bitmask = ((~0ULL) >> (63 - word_ofs));
            int rankSum = rankValue;
            for (int i = 0; i < word_idx; ++i) {
                rankSum += __builtin_popcountll(this->bits[i]);
            }
            rankSum += __builtin_popcountll(bitmask & this->bits[word_idx]);
            return rankSum - 1;
        }

        void setCachedSlot(Slot s, uint32_t ofs, int i) {
            this->cached_slots[i] = s;
            this->cache_offsets[i] = ofs;
            this->validSlotCnt = i + 1;
        }

        inline Slot findCache(const uint32_t ofs, bool& found) const {
            for (int i = 0; i < validSlotCnt; ++i) {
                if (this->cache_offsets[i] == ofs) {
                    return this->cached_slots[i];
                }
            }
            found = false;
            return Slot();
        }

        inline bool invalidCache(const uint32_t ofs) {
            for (int i = 0; i < validSlotCnt; ++i) {
                if (this->cache_offsets[i] == ofs) {
                    for (int j = i; j < validSlotCnt; ++j) {
                        this->cached_slots[j] = this->cached_slots[j + 1];
                        this->cache_offsets[j] = this->cache_offsets[j + 1];
                    }
                    validSlotCnt--;
                    return true;
                }
            }
            return false;
        }

        uint32_t getAndSetRank(uint32_t _rankValue) {
            this->rankValue = _rankValue;
            for (int i = 0; i < WordPerCell; ++i) {
                _rankValue += __builtin_popcountll(this->bits[i]);
            }
            return _rankValue;
        }

    public:
        Slot cached_slots[SlotPerCell];
        uint32_t rankValue = 0;
        uint8_t validSlotCnt = 0;
        uint8_t cache_offsets[SlotPerCell] = {0};
        uint64_t bits[WordPerCell] = {0};
    };
};
