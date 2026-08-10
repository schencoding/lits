#pragma once

#include <litsPlus_cnode.hpp>
#include <litsPlus_mnode.hpp>

#ifdef USE_PMSS
#include <litsPlus_hot.hpp>
#endif

#ifndef MAX_STACK
#define MAX_STACK 256
#endif

namespace litsPlus {

    template <typename EntryIter>
    class litsPlusIter {
        typedef struct {
            ModelNode<EntryIter>* mnode;
            CompactLeaf* cnode;

            /* Position in mnode/cnode */
            int idx;

        } info;

    private:
        bool is_valid;  // Whether the itertor is valid?
        bool in_cnode;  // Whether the iterator is in a cnode currently?
        bool is_end;    // Whether the iterator meets the end?
        int depth;      // Current depth
#ifdef USE_PMSS
        bool in_subtrie;        // Whether the iterator is in a subtree currently?
        HOTIterInLIT hot_iter;  // The iterator in HOT subtrie, only valid when in_subtrie
#endif
        info path[MaxStack];  // Recorded path

    public:
        /**
         * Constructor
         *
         * Initialize all the private fields
         */
        litsPlusIter() {
            // Whether the iterator is in a cnode currently?
            in_cnode = false;
            // Whether the iterator meets the end?
            is_end = false;
            // Current depth
            depth = -1;

#ifdef USE_PMSS
            in_subtrie = false;
            hot_iter = HOTIterInLIT();
#endif
            // Recorded path
            memset(path, 0, sizeof(info) * MAX_STACK);
        }

        inline bool isFinish() const {
            // Whether the iterator is finished
            return is_end;
        }

        inline bool isValid() const {
            return is_valid;
        }

        inline Entry* getEntry(void) const {
#ifdef USE_PMSS
            if (in_subtrie) {
                return (*hot_iter).getKV();
            }
#endif

            auto& _info = path[depth];

            if (in_cnode) {
                return _info.cnode->getEntry(_info.idx);
            }

            Slot s = _info.mnode->getSlot(_info.idx);
            return static_cast<Entry*>(s.getPointer());
        }

        void next() {
#ifdef USE_PMSS
            if (in_subtrie) {
                ++hot_iter;
                if (hot_iter == HOTIndexInLIT::END_ITERATOR) {
                    in_subtrie = false;
                } else {
                    return;
                }
            }
#endif
            while (depth >= 0) {
                if (ADVANCE()) {
                    return;
                }
                depth = depth - 1;
            }

            // No previous level, quit
            is_end = true;
            return;
        }

        /**
         * Find the first valid item in a slot, record the path.
         */
        void FIRST(Slot _) {
            auto t = _.getType();

            if (t == Slot::Type::CLeafSlot) {
                in_cnode = true;
                CompactLeaf* cleaf = static_cast<CompactLeaf*>(_.getPointer());
                if (cleaf->isSorted() == false) {
                    cleaf->SelfSort();
                }
                ++depth;
                path[depth].cnode = cleaf;
                path[depth].idx = 0;
                return;
            } else if (t == Slot::Type::MNodeSlot) {
                ModelNode<EntryIter>* mnode = static_cast<ModelNode<EntryIter>*>(_.getPointer());
                for (int idx = 0; idx < mnode->slotArrayLen(); ++idx) {
                    Slot s = mnode->getSlot(idx);
                    auto tt = s.getType();
                    if (tt == Slot::Type::EmptySlot) {
                        continue;
                    } else {
                        ++depth;
                        path[depth].mnode = mnode;
                        path[depth].idx = idx;

                        if (tt == Slot::Type::MNodeSlot || tt == Slot::Type::CLeafSlot) {
                            FIRST(s);
                        }
                        return;
                    }
                }
            }
#ifdef USE_PMSS
            else if (t == Slot::Type::SubTreeSlot) {
                in_subtrie = true;
                uint64_t subTrie = _.getData();
                hot_iter = HOTBegin((HOTIndexInLIT&)subTrie);
                return;
            }
#endif

            /*
             * If we reach here, no valid item is found.
             */
            assert(false);
            return;
        }

        /**
         * Advance to the next valid slot in the current level
         *
         * This function iterates the slots in the current level and find the
         * first valid one. If a valid slot is found, the iterator is updated
         * to point to the new slot. Otherwise, this function will return false.
         */
        bool ADVANCE() {
            // Extract the last level's information
            info& cli = path[depth];

            // If the iterator is in cnode currently
            if (in_cnode) {
                // Increment the index
                ++cli.idx;

                // Jump out of the cnode
                if (cli.idx >= cli.cnode->getCount()) {
                    in_cnode = false;
                    return false;
                }

                return true;
            }

            // Iterate through the remain items
            for (int i = cli.idx + 1; i < cli.mnode->slotArrayLen(); ++i) {
                Slot s = cli.mnode->getSlot(i);
                auto type = s.getType();

                if (type == Slot::Type::EmptySlot)
                    continue;

                // Record the new index
                cli.idx = i;

                bool use_first = type == Slot::Type::MNodeSlot || type == Slot::Type::CLeafSlot;

#ifdef USE_PMSS
                use_first |= type == Slot::Type::SubTreeSlot;
#endif

                if (use_first) {
                    // If hit the multi/Cnode/Subtrie-item, find the first valid item
                    FIRST(s);
                    return true;

                } else if (type == Slot::Type::SingleSlot) {
                    // If hit the single-item, return it
                    return true;
                }
            }

            // Current level fail to find a valid item
            return false;
        }

        static litsPlusIter find(Slot root, const char* key, const int keyLen) {
            litsPlusIter iter;
            Slot _ = root;  // Start from the root Slot
            int ccpl = 0;   // Initial comparison length (ccpl)

            // Traverse the structure and find the entry
            do {
                auto t = _.getType();  // Get the type of the slot
                switch (t) {
                case Slot::Type::SingleSlot: {
                    // If it's a SingleSlot, check the entry directly
                    Entry* record = static_cast<Entry*>(_.getPointer());
                    if (record->verify(key, keyLen, ccpl)) {
                        // Mark the iterator as valid if found
                        iter.is_valid = true;
                        return iter;  // Found entry, return the iterator
                    } else {
                        iter.is_valid = false;
                        return iter;  // Return iterator in end state if not found
                    }
                }
                case Slot::Type::CLeafSlot: {
                    // If it's a CompactLeaf slot, perform lookup on the leaf
                    CompactLeaf* cleaf = static_cast<CompactLeaf*>(_.getPointer());

                    // If the CompactLeaf is not sorted, sort it first
                    if (!cleaf->isSorted()) {
                        cleaf->SelfSort();
                    }

                    // Use returnKeyIdx to get the index of the matching entry in the CompactLeaf
                    int idx = cleaf->returnKeyIdx(key, keyLen, ccpl);

                    if (idx >= 0) {
                        // Record the path and increment depth for CLeafSlot
                        iter.depth++;
                        iter.path[iter.depth].mnode = nullptr;
                        iter.path[iter.depth].cnode = cleaf;
                        iter.path[iter.depth].idx = idx;  // Set the correct index from returnKeyIdx
                        iter.in_cnode = true;
                        iter.is_valid = true;
                        return iter;
                    } else {
                        iter.is_valid = false;
                        return iter;  // Return iterator in end state if not found
                    }
                }
#ifdef USE_PMSS
                case Slot::Type::SubTreeSlot: {
                    // If it's a subtrie slot, return the hot's iterator
                    uint64_t subTrie = _.getData();
                    HOTIterInLIT subiter = HOTFind((HOTIndexInLIT&)(subTrie), key);
                    if (subiter != HOTIndexInLIT::END_ITERATOR) {
                        iter.is_valid = true;
                        iter.in_subtrie = true;
                    } else {
                        iter.is_valid = false;
                    }
                    return iter;
                }
#endif
                case Slot::Type::MNodeSlot: {
                    // If it's a ModelNode slot, navigate down the tree
                    ModelNode<EntryIter>* mnode = static_cast<ModelNode<EntryIter>*>(_.getPointer());
                    int slotIdx;
                    _ = mnode->locateSlotReturnIdx(key, ccpl, slotIdx);

                    // Record the path in the iter and increment depth for MNodeSlot
                    iter.depth++;
                    iter.path[iter.depth].mnode = mnode;
                    iter.path[iter.depth].cnode = nullptr;
                    iter.path[iter.depth].idx = slotIdx;
                    break;
                }
                case Slot::Type::EmptySlot: {
                    // If the slot is empty, return an iterator in end state
                    iter.is_valid = false;
                    return iter;
                }
                }
            } while (true);

            return iter;  // Return iterator in end state if not found
        }
    };
};  // namespace lits
