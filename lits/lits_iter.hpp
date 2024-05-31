#pragma once

#include "lits_hot.hpp"
#include "lits_node.hpp"

namespace lits {
class litsIter {
    typedef struct {
        Item *item_array;
        kv **kv_array;
        int array_len;
        int array_idx;

    } info;

  private:
    bool is_valid;          // Whether the iterator is valid?
    bool in_sub_trie;       // Whether the iterator is in a sub tree currently?
    bool in_cnode;          // Whether the iterator is in a cnode currently?
    bool is_end;            // Whether the iterator meets the end?
    HOTIter subtrie_iter;   // HOT iterator
    uint64_t coded_subtrie; // HOT index
    int depth;              // Current depth
    kv *data;               // Key-value pair
    info path[MAX_STACK];   // Recorded path

  public:
    /**
     * Constructor
     *
     * Initialize all the private fields
     */
    litsIter() {
        // Whether the iterator is valid?
        is_valid = true;
        // Whether the iterator is in a sub tree currently?
        in_sub_trie = false;
        // Whether the iterator is in a cnode currently?
        in_cnode = false;
        // Whether the iterator meets the end?
        is_end = false;
        // HOT iterator
        subtrie_iter = HOTIter();
        // HOT index
        coded_subtrie = 0;
        // Current depth
        depth = -1;
        // Key-value pair
        data = NULL;
        // Recorded path
        memset(path, 0, sizeof(info) * MAX_STACK);
    }

    /**
     * Sets the iterator to invalid state
     *
     * This function sets the iterator to invalid state. An invalid iterator
     * cannot be incremented or used for reading.
     */
    inline void set_invalid() { is_valid = false; }

    /**
     * Whether the iterator is not finished
     *
     * This function returns true if the iterator is not finished, i.e., the
     * iterator has not reached the end yet. Otherwise, it returns false.
     */
    inline bool not_finish() const {
        // Whether the iterator is not finished
        return is_end == false;
    }

    /**
     * Whether the iterator is valid
     *
     * This function returns true if the iterator is valid, i.e., the iterator
     * has been initialized properly and not set to invalid state by any
     * operation. Otherwise, it returns false.
     */
    inline bool valid() const { return is_valid; }

    /**
     * Get the kv pointer the iterator is pointing to
     *
     * This function returns the kv pointer the iterator is currently pointing
     * to. If the iterator is not in sub-trie, it returns the pointer to the
     * current kv. Otherwise, it returns the pointer to the kv the sub-trie
     * iterator is currently pointing to.
     */
    inline kv *getKV(void) const {
        // If the iterator is not in sub-trie
        if (in_sub_trie == false) {
            // Return the pointer to the current kv
            return data;
        }

        // Otherwise, return the pointer to the kv the sub-trie iterator is
        // currently pointing to
        return (*subtrie_iter).getKV();
    }

    /**
     * Get the value of the kv the iterator is pointing to
     *
     * This function returns the value of the kv the iterator is currently
     * pointing to.
     *
     * @return The value of the kv the iterator is pointing to
     */
    inline val read() const {
        // Return the value of the kv the iterator is currently pointing to
        return getKV()->read();
    }

    /**
     * Record the path of the iterator to the given InnerNode,
     * with the length of the item array and the index of the item
     * in the array
     *
     * This function records the path of the iterator to the given InnerNode,
     * with the length of the item array and the index of the item in the
     * array. The length and index are needed to iterate over the item
     * array.
     *
     * @param inode The InnerNode that the iterator is pointing to
     * @param _len The length of the item array of the InnerNode
     * @param _idx The index of the item in the item array of the InnerNode
     */
    inline void InnerNode_recordPath(InnerNode *inode, int _len, int _idx) {
        RT_ASSERT(depth < MAX_STACK - 1);
        depth++;
        path[depth] = {inode->get_items(), NULL, _len, _idx};
    }

    /**
     * Record the path of the iterator to the given Cnode,
     * with the length of the kv array and the index of the kv
     * in the array
     *
     * This function records the path of the iterator to the given Cnode,
     * with the length of the kv array and the index of the kv in the
     * array. The length and index are needed to iterate over the kv
     * array.
     *
     * @param cnode The Cnode that the iterator is pointing to
     * @param _len The length of the kv array of the Cnode
     * @param _idx The index of the kv in the kv array of the Cnode
     */
    inline void CNode_recordPath(Cnode *cnode, int _len, int _idx) {
        RT_ASSERT(depth < MAX_STACK - 1);
        in_cnode = true;
        depth++;
        path[depth] = {NULL, cnode->data, _len, _idx};
        data = RAW_KV(cnode->data[_idx]);
    }

    /**
     * Initialize the subtrie iterator
     *
     * This function initializes the subtrie iterator, and sets the iterator
     * to the first valid item of the subtrie.
     *
     * @param hot_iter The HOT iterator of the subtrie
     * @param _coded_subtrie The HOT index of the subtrie
     */
    inline void Init_subtrieIter(HOTIter hot_iter, uint64_t _coded_subtrie) {
        in_sub_trie = true;
        // The HOT index of the subtrie
        coded_subtrie = _coded_subtrie;
        // The HOT iterator of the subtrie
        subtrie_iter = hot_iter;
    }

    /**
     * Set the current data pointer of the iterator.
     *
     * This function sets the current data pointer of the iterator to the given
     * pointer. This function is used when the iterator is pointing to a valid
     * item (either a kv or a Cnode).
     *
     * @param _data The new current data pointer of the iterator
     */
    inline void set_data(kv *_data) { data = _data; } /* set_data */

    /**
     * Incline the iterator.
     *
     * Incline the iterator to the next valid key-value pair. If there is no
     * such key-value pair, the iterator is set to be invalid.
     */
    void next() {
        // There are two cases for current iterator's status:
        // 1. the current iter is in a sub tree;
        // 2. the current iter is in LIT;

        // If the current iter is in sub_tree, do sub_tree iteration
        if (in_sub_trie) {
            // Incline the sub-trie iterator
            ++subtrie_iter;

            // If jump out the sub_tree, update the iter
            if (subtrie_iter == HOTIndex::END_ITERATOR) {
                // Clear the in_sub_trie flag
                in_sub_trie = false;

                // If depth < 0, meet the final entry
                while (depth >= 0) {
                    // Try to find the next valid item in the current level
                    if (ADVANCE())
                        return;

                    // Hit the end without meeting a valid item, roll back
                    // to the previous level
                    depth = depth - 1;
                }
            } else {
                return;
            }
            // Otherwise, do LIT iteration

        } else {
            while (depth >= 0) {
                if (ADVANCE())
                    return;
                depth = depth - 1;
            }
        }

        // No previous level, quit
        is_end = true;

        return;
    }

    /**
     * Find the first valid item in item array, record the path.
     *
     * A valid item must can be found.
     */
    void FIRST(const Item &father) {
        /*
         * If the father is a compact node, we simply record the information and
         * return.
         */
        if (father.get_itype() == ITYP_CNod) {
            in_cnode = true;
            Cnode *cnode = father.get_cnode();
            RT_ASSERT(depth < MAX_STACK - 1);
            path[++depth] = {NULL, cnode->data, (int)(cnode->h.key_cnt), 0};
            data = RAW_KV(cnode->data[0]);
            return;
        }

        /*
         * Otherwise, we start traversing the item array one by one.
         */
        InnerNode *inode = father.get_inner_node();
        Item *item_array = inode->get_items();
        int item_array_len = inode->get_item_array_len();
        int i = 0;

        for (int i = 0; i < item_array_len; ++i) {
            /*
             * Skip the empty items.
             */
            if (item_array[i].is_empty())
                continue;

            /*
             * The current valid item and its type.
             */
            Item *cur_item = &item_array[i];
            ItemType _t = cur_item->get_itype();

            /*
             * Record the information in the path.
             */
            RT_ASSERT(depth < MAX_STACK - 1);
            path[++depth] = {item_array, NULL, item_array_len, i};

            /*
             * Fill the data accordingly.
             */
            if (_t == ITYP_Sing) {
                data = cur_item->get_entry();
                return;
            } else if (_t == ITYP_Trie) {
                /*
                 * If the item is a sub-trie, we initialize an iterator on the
                 * sub-trie and return.
                 */
                in_sub_trie = true;
                uint64_t _coded_subtrie = cur_item->get_coded_index();
                coded_subtrie = _coded_subtrie;
                subtrie_iter = HOTBegin((HOTIndex &)coded_subtrie);
                return;
            } else {
                /*
                 * Otherwise, we recursively call FIRST on the item.
                 */
                FIRST(*cur_item);
                return;
            }
        }

        /*
         * If we reach here, no valid item is found.
         */
        RT_ASSERT(false);
        return;
    }

    /**
     * Advance to the next valid item in the current level
     *
     * This function iterates the items in the current level and find the
     * first valid one. If a valid item is found, the iterator is updated
     * to point to the new item. Otherwise, the iterator is set to invalid.
     *
     * Return true if a valid item is found, or false if no valid item is
     * found in the current level.
     */
    bool ADVANCE() {
        // Extract the last level's information
        info &cli = path[depth];

        // If the iterator is in cnode currently
        if (in_cnode) {
            // Increment the index
            ++cli.array_idx;

            // Jump out of the cnode
            if (unlikely(cli.array_idx >= cli.array_len)) {
                in_cnode = false;
                return false;
            }

            // Otherwise, update the data
            data = RAW_KV(cli.kv_array[cli.array_idx]);
            return true;
        }

        // Iterate through the remain items
        for (int i = cli.array_idx + 1; i < cli.array_len; ++i) {
            if (cli.item_array[i].is_empty()) {
                continue;
            }

            // The current valid item and its type
            Item *cur_item = &cli.item_array[i];
            ItemType _t = cur_item->get_itype();

            // Record the new index
            cli.array_idx = i;

            // Check the type:
            // Return the single item
            // Return the first item in multi/cnode/trie item

            if (_t == ITYP_Mult || _t == ITYP_CNod) {
                // If hit the multi/Cnode-item, find the first valid item
                FIRST(*cur_item);
                return true;

            } else if (_t == ITYP_Sing) {
                // If hit the single-item, return it
                data = cur_item->get_entry();
                return true;

            } else if (_t == ITYP_Trie) {
                // If hit the trie-item, jump into it
                in_sub_trie = true;
                coded_subtrie = cur_item->get_coded_index();
                subtrie_iter = HOTBegin((HOTIndex &)coded_subtrie);
                return true;
            }
        }

        // Current level fail to find a valid item
        return false;
    }
};

/**
 * Find the Item corresponding to the given key in the given node, and record
 * the path of the iterator to the node.
 *
 * This function finds the Item corresponding to the given key in the given
 * node, and records the path of the iterator to the node. The path is needed
 * to iterate over the item array of the node.
 *
 * @param father The node to be searched
 * @param key The key to be searched
 * @param ccpl The common character prefix length of father and key
 * @param hpt The HPT used for predicting the position
 * @param iter The iterator used to record the path of the search
 *
 * @return A pointer to the found Item
 */
inline Item *recordPath_find(const Item &father, const str key, int &ccpl,
                             const HPT *hpt, litsIter &iter) {
    InnerNode *node = father.get_inner_node();
    int pos = predictPos(node, key, ccpl, hpt);
    iter.InnerNode_recordPath(node, node->h.item_array_length, pos);
    return &(node->get_items()[pos]);
}

/**
 * Find the item corresponding to the given key in the trie.
 *
 * This function finds the item corresponding to the given key in the trie. The
 * item is a node in the trie, which may be either an inner node or a leaf
 * node. If the key is not found, the iterator is set to invalid.
 *
 * @param item The current node in the search
 * @param _key The key to be searched
 * @param iter The iterator used to record the path of the search
 */
inline void trie_find(Item &item, const str _key, litsIter &iter) {
    uint64_t coded_subtrie = item.get_coded_index();
    HOTIter hot_iter = HOTFind((HOTIndex &)coded_subtrie, _key);
    if (hot_iter == HOTIndex::END_ITERATOR) {
        iter.set_invalid();
    } else {
        /*
         * Initialize the subtrie iterator.
         *
         * This function initializes the subtrie iterator, and sets the iterator
         * to the first valid item of the subtrie.
         *
         * @param hot_iter The HOT iterator of the subtrie
         * @param _coded_subtrie The HOT index of the subtrie
         */
        iter.Init_subtrieIter(hot_iter, coded_subtrie);
    }
}

/**
 * Find the single item in the iterator.
 *
 * This function finds the single item in the iterator, which must be a leaf
 * node. The item is set as the data pointer of the iterator.
 *
 * @param item The item to be searched
 * @param iter The iterator used to record the path of the search
 */
inline void sing_find(Item &item, litsIter &iter) {
    // Set the data pointer of the iterator to the found item
    iter.set_data(item.get_entry());
}

/**
 * Find the key in the iterator.
 *
 * This function searches for the given key in the iterator, which must be a
 * cnode. The key is searched by first using the hash value of the input key
 * to quickly find a key in the cnode with the same hash value. If there is
 * more than one key with the same hash value, the function will use the
 * partial key's verify function to verify if the input key matches the
 * partial key stored in the cnode. The iterator is updated to point to the
 * found key if a match is found. Otherwise, the iterator is set to
 * invalid.
 *
 * @param item The item to be searched
 * @param _key The key to be searched for
 * @param iter The iterator used to record the path of the search
 */
inline void cnod_find(Item &item, const str _key, litsIter &iter) {
    // The query cnode
    Cnode *cnode = item.get_cnode();

    // The input string's hash value
    uint16_t hv = hashStr(_key);

    // Search one by one
    for (int i = 0; i < cnode->h.key_cnt; ++i) {
        if (hv != getHashVal(cnode->data[i])) {
            continue;
        }
        kv *raw_kv = RAW_KV(cnode->data[i]);

        // Verify the remain parts of the string
        bool match = raw_kv->verify(_key, cnode->h.ccpl);
        if (match) {
            // Record the path of the search
            iter.CNode_recordPath(cnode, cnode->h.key_cnt, i);
            return;
        }
    }

    // If no match is found, set the iterator to invalid
    iter.set_invalid();
    return;
}

}; // namespace lits
