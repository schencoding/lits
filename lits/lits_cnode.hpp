#pragma once

#include "lits_base.hpp"
#include "lits_entry.hpp"
#include "lits_gpkl.hpp"
#include "lits_kv.hpp"
#include "lits_utils.hpp"

namespace lits {

class Cnode {
  public:
    typedef struct {
        uint32_t ccpl; // Confirmed Common Prefix Length
        uint32_t key_cnt;
    } cheader;

  public:
    cheader h;
    kv *data[0];

    inline bool has_room() const { return h.key_cnt < CNODE_SIZE; }
    inline bool more_than_2() const { return h.key_cnt > 2; }

    /**
     * Return the size of the cnode.
     *
     * @return size of the cnode (Bytes)
     */
    inline uint64_t cnode_size() const {
        return h.key_cnt * sizeof(kv *) + sizeof(cheader);
    }
};

/**
 * Extracts data from the given Cnode and adds it to the provided KVS1.
 *
 * @param cnode pointer to the Cnode object to extract data from
 * @param kvs reference to the KVS1 object to add the extracted data to
 *
 * @return void
 *
 * @throws None
 */
inline void extract_cnode(Cnode *cnode, KVS1 &kvs) {
    for (int i = 0; i < cnode->h.key_cnt; ++i) {
        kvs.push((kv *)PTR_RAW(cnode->data[i]));
    }
    delete[] reinterpret_cast<uint8_t *>(cnode);
}

/**
 * Extracts and inserts key-value pairs in a sorted manner.
 *
 * This function extracts key-value pairs from a Cnode, inserts a new
 * key-value pair into the extracted KVS1, and deletes the original Cnode.
 * The KVS1 is sorted in ascending order of keys.
 *
 * @param cnode pointer to Cnode
 * @param kvs reference to KVS1
 * @param k constant string
 * @param v value
 *
 * @return true if the insertion is successful, false if the key already exists
 *
 * @throws None
 */
inline bool try_extract_keys_if_valid_insert(Cnode *cnode, KVS1 &kvs,
                                             const str k, const val v) {
    // Extract the common prefix length
    int ccpl = cnode->h.ccpl;
    // Find the position to insert the new key
    int cut_pos = cnode->h.key_cnt;
    for (int i = 0; i < cnode->h.key_cnt; ++i) {
        kv *cur_kv = (kv *)PTR_RAW(cnode->data[i]);
        // Compare the keys skipping the common prefix length
        int cmp_res = ustrcmp(cur_kv->k + ccpl, k + ccpl);
        if (cmp_res == 0) {
            // The key already exists, return false
            return false;
        } else if (cmp_res > 0) {
            cut_pos = i;
            break;
        }
    }
    // Extract the cnode data
    for (int j = 0; j < cut_pos; ++j) {
        kvs.push((kv *)PTR_RAW(cnode->data[j]));
    }
    // Insert the new key-value pair
    kvs.push(new_kv(k, v));
    for (int j = cut_pos; j < cnode->h.key_cnt; ++j) {
        kvs.push((kv *)PTR_RAW(cnode->data[j]));
    }
    // Delete the original Cnode
    delete[] reinterpret_cast<uint8_t *>(cnode);
    return true;
}

/**
 * Extracts and upserts key-value pairs in a sorted manner。
 *
 * @param cnode pointer to Cnode
 * @param kvs reference to KVS1
 * @param k constant string
 * @param v value
 *
 * @return value that was updated or 0 if no update occurred
 *
 * @throws None
 */
inline val try_extract_keys_if_valid_upsert(Cnode *cnode, KVS1 &kvs,
                                            const str k, const val v) {
    int ccpl = cnode->h.ccpl;       // Confirmed common prefix length
    int cut_pos = cnode->h.key_cnt; // Position to cut the Cnode
    for (int i = 0; i < cnode->h.key_cnt; ++i) {
        kv *cur_kv = (kv *)PTR_RAW(cnode->data[i]);
        int cmp_res = ustrcmp(cur_kv->k + ccpl, k + ccpl);
        if (cmp_res == 0) {
            // The key already exists, return old value
            val old_val = cur_kv->read();
            cur_kv->update(v);
            return old_val;
        } else if (cmp_res > 0) {
            cut_pos = i;
            break;
        }
    }
    // Extract the cnode data
    for (int j = 0; j < cut_pos; ++j) {
        kvs.push((kv *)PTR_RAW(cnode->data[j]));
    }
    kvs.push(new_kv(k, v));
    for (int j = cut_pos; j < cnode->h.key_cnt; ++j) {
        kvs.push((kv *)PTR_RAW(cnode->data[j]));
    }
    delete[] reinterpret_cast<uint8_t *>(cnode);
    return 0;
}

/**
 * @brief Builds a compact node (Cnode) for a given set of keys and values.
 *
 * The input keys and values must be sorted in ascending order, and the
 * function will build a compact node based on that ordering. The confirmed
 * common prefix of all the keys is also required as an input.
 *
 * The returned Cnode is a pointer to the newly built data structure, and it is
 * the responsibility of the caller to free the memory occupied by the Cnode
 * by calling the `delete_cnode` function.
 *
 * @param kvs The input keys and values, must be sorted and not duplicated.
 * @param l The starting index of the input keys and values.
 * @param r The ending index of the input keys and values (exclusive).
 * @param ccpl The confirmed common prefix length of all the input keys.
 *
 * @return a pointer which points to the new-built compact node.
 */
template <class records>
Cnode *new_cnode(const records &kvs, const int l, const int r, const int ccpl) {
    // Determine the size of the input records
    int size = (r - l);

    // The return cnode
    Cnode *ret;

    // Node size
    int node_size = size * sizeof(kv *) + sizeof(Cnode::cheader);

    // Malloc the node
    ret = (Cnode *)new uint8_t[node_size];
    memset(ret, 0, node_size);

    // Set the fields
    ret->h.ccpl = ccpl;
    ret->h.key_cnt = size;
    for (int i = 0; i < size; ++i) {
        // Store the KV, and use the front 16 bits to store a hash value.
        ret->data[i] = new_hash_kv(kvs.ret_kv(l + i));
    }

    return ret;
}

/**
 * Creates a new empty Cnode with the specified number of slots.
 *
 * @param number_of_slots the number of slots for the Cnode
 *
 * @return a pointer to the newly created Cnode
 *
 * @throws None
 */
Cnode *new_empty_cnode(const int number_of_slots) {
    int node_size = number_of_slots * sizeof(kv *) + sizeof(Cnode::cheader);
    Cnode *ret = (Cnode *)new uint8_t[node_size];
    memset(ret, 0, node_size);
    return ret;
}

/**
 * Search for a key in the given Cnode and return the corresponding kv if
 * found.
 *
 * The search algorithm is a linear search through all the keys stored in the
 * cnode. The search stops as soon as a key is found that matches the given
 * key.
 *
 * @param cnode pointer to the Cnode structure
 * @param ckey constant reference to the input string to be searched
 *
 * @return pointer to the corresponding kv-entry if found, otherwise NULL
 */
kv *_cnode_search(const Cnode *cnode, const str ckey) {
    // The input string's hash value
    uint16_t hv = hashStr(ckey);
    // Search one by one
    for (int i = 0; i < cnode->h.key_cnt; ++i) {
        if (hv != getHashVal(cnode->data[i])) {
            continue;
        }
        kv *raw_kv = RAW_KV(cnode->data[i]);

        // Verify the remain parts of the string
        bool match = raw_kv->verify(ckey, cnode->h.ccpl);
        if (match) {
            return raw_kv;
        }
    }

    return NULL;
}

/**
 * Insert a key-value pair into the Cnode while maintaining the sorted order.
 *
 * @param cnode The pointer to the Cnode.
 * @param ckey The key to be inserted.
 * @param cval The value corresponding to the key.
 *
 * @return true if the insertion is successful, false if the key already
 * exists.
 */
bool _cnode_withRoom_insert(Cnode *&cnode, const str ckey, const val cval) {
    // The current confirmed common prefix in this cnode
    int ccpl = cnode->h.ccpl, cut_pos = cnode->h.key_cnt;

    // The pointer to the old node and new node
    Cnode *old_node, *new_node;

    // Search one by one, util find a key which is larger than ckey
    for (int i = 0; i < cnode->h.key_cnt; ++i) {
        kv *raw_kv = RAW_KV(cnode->data[i]);
        str ikey = raw_kv->k;

        int cmp_res = ustrcmp(ikey + ccpl, ckey + ccpl);
        if (cmp_res == 0) {
            // The key already exists, return false
            return false;
        } else if (cmp_res > 0) {
            cut_pos = i;
            break;
        }
    }

    new_node = new_empty_cnode(cnode->h.key_cnt + 1);
    new_node->h.ccpl = cnode->h.ccpl;
    new_node->h.key_cnt = cnode->h.key_cnt + 1;

    // Copy the cnode data
    for (int j = 0; j < cut_pos; ++j) {
        new_node->data[j] = cnode->data[j];
    }
    new_node->data[cut_pos] = new_hash_kv(ckey, cval);
    for (int j = cut_pos; j < cnode->h.key_cnt; ++j) {
        new_node->data[j + 1] = cnode->data[j];
    }

    old_node = cnode;
    cnode = new_node;
    delete[] reinterpret_cast<uint8_t *>(old_node);

    return true;
}

/**
 * Upserts a new key-value pair in the given Cnode structure and returns the
 * old value if the key already exists.
 *
 * @param cnode A reference to a pointer to the Cnode structure
 * @param ckey The key to be upserted
 * @param cval The value to be upserted
 *
 * @return The old value if the key already exists, otherwise 0
 *
 * @throws None
 */

val _cnode_withRoom_upsert(Cnode *&cnode, const str ckey, const val cval) {
    // The current confirmed common prefix in this cnode
    int ccpl = cnode->h.ccpl, cut_pos = cnode->h.key_cnt;

    // The pointer to the old node and new node
    Cnode *old_node, *new_node;

    // The input string's hash value
    uint16_t hv = hashStr(ckey);

    // First try to find a repeat key
    for (int i = 0; i < cnode->h.key_cnt; ++i) {
        if (hv != getHashVal(cnode->data[i])) {
            continue;
        }
        kv *raw_kv = RAW_KV(cnode->data[i]);

        // Verify the remain parts of the string
        bool match = raw_kv->verify(ckey, cnode->h.ccpl);
        if (match) {
            val old_val = raw_kv->read();
            raw_kv->update(cval);
            return old_val;
        }
    }

    // Search one by one, util find a key which is larger than ckey
    for (int i = 0; i < cnode->h.key_cnt; ++i) {
        kv *raw_kv = RAW_KV(cnode->data[i]);
        str ikey = raw_kv->k;

        int cmp_res = ustrcmp(ikey + ccpl, ckey + ccpl);
        if (cmp_res > 0) {
            cut_pos = i;
            break;
        }
    }

    new_node = new_empty_cnode(cnode->h.key_cnt + 1);
    new_node->h.ccpl = cnode->h.ccpl;
    new_node->h.key_cnt = cnode->h.key_cnt + 1;

    // Copy the cnode data
    for (int j = 0; j < cut_pos; ++j) {
        new_node->data[j] = cnode->data[j];
    }
    new_node->data[cut_pos] = new_hash_kv(ckey, cval);
    for (int j = cut_pos; j < cnode->h.key_cnt; ++j) {
        new_node->data[j + 1] = cnode->data[j];
    }

    old_node = cnode;
    cnode = new_node;
    delete[] reinterpret_cast<uint8_t *>(old_node);
    return 0;
}

bool _cnode_withRoom_remove(Cnode *&cnode, const str ckey) {
    // The current confirmed common prefix in this cnode
    int ccpl = cnode->h.ccpl, delete_i = -1;

    // The pointer to the old node and new node
    Cnode *old_node, *new_node;

    // The input string's hash value
    uint16_t hv = hashStr(ckey);

    // Search one by one, util find a key which is larger than ckey
    for (int i = 0; i < cnode->h.key_cnt; ++i) {
        if (hv != getHashVal(cnode->data[i])) {
            continue;
        }
        kv *raw_kv = RAW_KV(cnode->data[i]);

        // Verify the remain parts of the string
        bool match = raw_kv->verify(ckey, cnode->h.ccpl);

        // If match, remove the target kv-entry
        if (match) {
            delete_i = i;
            free_kv(raw_kv);
            break;
        }
    }

    // Not found a valid entry
    if (delete_i == -1) {
        return false;
    }

    new_node = new_empty_cnode(cnode->h.key_cnt - 1);
    new_node->h.ccpl = cnode->h.ccpl;
    new_node->h.key_cnt = cnode->h.key_cnt - 1;

    // Copy the cnode data
    for (int j = 0; j < delete_i; ++j) {
        new_node->data[j] = cnode->data[j];
    }
    for (int j = delete_i; j < cnode->h.key_cnt - 1; ++j) {
        new_node->data[j] = cnode->data[j + 1];
    }

    old_node = cnode;
    cnode = new_node;
    delete[] reinterpret_cast<uint8_t *>(old_node);

    return true;
}

/**
 * This function is to extract the valid kv entry after deletion
 *
 * return NULL means no matched entry
 * return valid pointer means found a valid entry
 */
kv *_cnode_degrade(Cnode *cnode, const str ckey) {
    // This function will only be called when key count is 2
    RT_ASSERT(cnode->h.key_cnt == 2);

    // The deletion index
    int delete_i = -1;

    // The input string's hash value
    uint16_t hv = hashStr(ckey);

    // The return entry
    kv *ret_entry = NULL;

    // Search one by one, util find a key which is larger than ckey
    for (int i = 0; i < cnode->h.key_cnt; ++i) {
        if (hv != getHashVal(cnode->data[i])) {
            continue;
        }
        kv *raw_kv = RAW_KV(cnode->data[i]);

        // Verify the remain parts of the string
        bool match = raw_kv->verify(ckey, cnode->h.ccpl);

        // If match, remove the target kv-entry
        if (match) {
            delete_i = i;
            free_kv(raw_kv);
            break;
        }
    }

    // Not found a valid entry
    if (delete_i == -1) {
        return NULL;
    }

    ret_entry = RAW_KV(cnode->data[1 - delete_i]);
    delete[] reinterpret_cast<uint8_t *>(cnode);

    return ret_entry;
}

} // namespace lits