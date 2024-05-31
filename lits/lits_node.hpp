#pragma once

#include "lits_base.hpp"
#include "lits_cnode.hpp"
#include "lits_entry.hpp"
#include "lits_hot.hpp"
#include "lits_iter.hpp"
#include "lits_model.hpp"
#include "lits_pmss.hpp"

#include <immintrin.h>
#include <stack>

namespace lits {

/**
 *
 * Item Array: (Node)
 * | Header | Item1 | ... | Itemk |
 *
 * Header:
 * | IAL(Item array length) (8B) |
 * | DataSize (8B) |
 * | Linear Model (16B) |
 * | Offset (4B) |
 * | Prefix Length (4B) |
 * | Prefix (patched to 8xB) |
 *
 *
 * Item Composition:
 *
 * ---------------------------------------------
 * |       3      |       13       |     48    |
 * ---------------------------------------------
 * | IType(max:7) |     unused     |  pointer  |
 * ---------------------------------------------
 *
 * FOR IType:
 *      IType = 0b000   =====>  Item Type is `Empty Slot`
 *      IType = 0b001   =====>  Item Type is `Single Pointer`
 *      IType = 0b010   =====>  Item Type is `Model-based Inner Node Pointer`
 *      IType = 0b011   =====>  Item Type is `Trie Node Pointer`
 *      IType = 0b100   =====>  Item Type is `Compact Leaf Node Pointer`
 *
 */

/**
 * Macros for bit handler
 */

// This macro is to transfer a pointer into uint64_t
#define P2U(p) ((uint64_t)(void *)(p))

// This macro is to transfer a pointer (ex. Item *) to an Item.
// The Item is {p;}
#define P2I(p) (*(Item *)&p)

// ************************************************************
//                      Pointer Types
// ************************************************************

class Item;
class InnerNode;
class litsIter;

inline int predictPos(InnerNode *node, str key, int &ccpl, const HPT *model);
void free_inner_node(InnerNode *node);
template <class records>
Item pmss_bulk(const records &kvs, const int l, const int r, const int ccpl,
               const HPT *model, const PMSS *pmss);
void extract_inner_node(InnerNode *node, KVS1 &kvs);

typedef enum : uint8_t {
    ITYP_Null = 0b000,
    ITYP_Sing = 0b001,
    ITYP_Mult = 0b010,
    ITYP_Trie = 0b011,
    ITYP_CNod = 0b100,
} ItemType;

class FlaggedPtr {
    static constexpr int ITYP_POS = 61;
    static constexpr uint64_t ITYP_MASK = 0x7UL;
    static constexpr uint64_t ITYP_UMASK = ~(ITYP_MASK << ITYP_POS);
    static constexpr uint64_t ITYP_QMASK = ~ITYP_UMASK;

  public:
    uint64_t main_body;

  public:
    FlaggedPtr(uint64_t _n = 0) : main_body(_n) {}

    // set
    inline void set_type(const ItemType t) {
        main_body = ((main_body & ITYP_UMASK) | ((uint64_t)t) << ITYP_POS);
    }
    inline void set_index(HOTIndex &index) {
        main_body = *reinterpret_cast<uint64_t *>(&index);
        set_type(ITYP_Trie);
    }
    inline void set_inner_node(InnerNode *n) {
        main_body = *reinterpret_cast<uint64_t *>(&n);
        set_type(ITYP_Mult);
    }
    inline void set_cnode(Cnode *n) {
        main_body = *reinterpret_cast<uint64_t *>(&n);
        set_type(ITYP_CNod);
    }
    inline void set_entry(kv *_kv) {
        main_body = *reinterpret_cast<uint64_t *>(&_kv);
        set_type(ITYP_Sing);
    }
    inline void set_null() { main_body = 0; }

    // get
    inline ItemType get_type() const {
        return (ItemType)((main_body >> ITYP_POS) & ITYP_MASK);
    }
    inline uint64_t get_raw64() const { return main_body; }
    inline uint64_t get_coded_index() const { return main_body & ITYP_UMASK; }
    inline void *raw() const { return (void *)(main_body & PTR_MASK); }
};

class InnerNode {
  public:
    typedef struct {
        uint64_t item_array_length;
        uint64_t num_of_keys;
        double k; // linear model's slope
        double b; // linear model's intercept
        uint32_t prefix_length;
        uint32_t header_offset;
    } header;

  public:
    header h;
    unsigned char prefix_and_items[];

  public:
    inline uint64_t get_item_array_len() const { return h.item_array_length; }
    inline uint32_t get_prefix_length() const { return h.prefix_length; }
    inline unsigned char *get_prefix() { return prefix_and_items; }
    inline double get_K() const { return h.k; }
    inline double get_B() const { return h.b; }
    Item *get_items() { return (Item *)(get_prefix() + h.header_offset); }
};

class Item {
    // HOTIndex's root node must be 8 bytes.
    // Because we want to fit it into an item.
    ST_ASSERT(sizeof(HOTIndex) == 8);

  private:
    FlaggedPtr ptr;

  public:
    // init
    Item(uint64_t _ptr = 0) : ptr(_ptr) {}

    // set
    inline void set_coded_index(HOTIndex &index) { ptr.set_index(index); }
    inline void set_entry(kv *_kv) { ptr.set_entry(_kv); }
    inline void set_cnode(Cnode *cnode) { ptr.set_cnode(cnode); }
    inline void set_inner_node(InnerNode *inner_node) {
        ptr.set_inner_node(inner_node);
    }
    inline void set_null() { ptr.set_null(); }

    // get
    inline ItemType get_itype() const { return ptr.get_type(); }
    inline Cnode *get_cnode() const { return (Cnode *)ptr.raw(); }
    inline kv *get_entry() const { return (kv *)ptr.raw(); }
    inline InnerNode *get_inner_node() const { return (InnerNode *)ptr.raw(); }
    inline uint64_t get_coded_index() const { return ptr.get_coded_index(); }
    inline uint64_t get_raw64() const { return ptr.get_raw64(); }
    inline bool is_empty() const { return get_raw64() == 0; }

    // locate
    inline Item *locate(const str key, int &ccpl, const HPT *hpt) const {
        InnerNode *node = get_inner_node();
        int pos = predictPos(node, key, ccpl, hpt);
        return &(node->get_items()[pos]);
    }

    // recursive delete
    void recursive_extract(KVS1 &kvs) {
        switch (get_itype()) {
        case ITYP_Trie: {
            uint64_t coded_subtrie_in_rcs = get_coded_index();
            auto &hot = ((HOTIndex &)coded_subtrie_in_rcs);
            for (auto it = hot.begin(); it != HOTIndex::END_ITERATOR; ++it) {
                kvs.push((*it).getKV());
            }
            hot.~HOTSingleThreaded();
            return;
        }
        case ITYP_Sing: {
            kvs.push(get_entry());
            return;
        }
        case ITYP_CNod: {
            extract_cnode(get_cnode(), kvs);
            return;
        }
        case ITYP_Mult: {
            extract_inner_node(get_inner_node(), kvs);
            delete[] reinterpret_cast<uint8_t *>(get_inner_node());
            return;
        }
        }
    }
};

class PathStack {
  private:
    typedef struct {
        InnerNode *header;
        Item *father;
        int ccpl;
    } path;

  private:
    HPT *hpt;
    PMSS *pmss;
    int stack_op = 0;
    path p[MAX_STACK];

  public:
    PathStack() = delete;
    PathStack(HPT *_hpt, PMSS *_pmss) : hpt(_hpt), pmss(_pmss) {}

    inline void record_path(Item *item, int ccpl) {
        p[stack_op].header = item->get_inner_node();
        p[stack_op].father = item;
        p[stack_op].ccpl = ccpl;
        stack_op += 1;
    }

    /**
     * Only happens after a valid insertion.
     * Increase the #keys from root to leaf
     *
     * If detect a resize boundary, do resize
     */
    void change_num(int _cnt) {
        for (int i = 0; i < stack_op; ++i) {
            if (_cnt > 0)
                p[i].header->h.num_of_keys++;
            else
                p[i].header->h.num_of_keys--;

            // Possible resize
            if ((p[i].header->h.num_of_keys >=
                 2 * p[i].header->h.item_array_length) ||
                (4 * p[i].header->h.num_of_keys <=
                 p[i].header->h.item_array_length)) {
                KVS1 kvs;
                int cnt = p[i].header->h.num_of_keys;
                p[i].father->recursive_extract(kvs);
                Item new_item = pmss_bulk(kvs, 0, cnt, p[i].ccpl, hpt, pmss);

                *(p[i].father) = new_item;
                return;
            }
        }
    }
};

inline int predictPos(InnerNode *node, str key, int &ccpl, const HPT *model) {
    // The possible prefix
    str prefix = (str)(node->get_prefix());

    // The possible incremental common prefix length
    uint32_t icpl = node->get_prefix_length();

    // If there are cached prefix in this node
    if (icpl) {
        // Compare the key with the common prefix
        int cmp_res = ustrcmp(prefix, key + ccpl, icpl);

        // If the common prefix unmatch, return boundary
        if (unlikely(cmp_res) == -1) {
            return node->get_item_array_len() - 1;
        } else if (unlikely(cmp_res) == 1) {
            return 0;
        }
    }

    // The position predicted by Bigram
    int pos;
    if (ccpl + icpl) {
        pos = model->getPos(key, node->get_item_array_len() - 2, ccpl + icpl,
                            node->get_K(), node->get_B()) +
              1;
    } else {
        pos = model->getPos_woGCPL(key, node->get_item_array_len() - 2,
                                   node->get_K(), node->get_B()) +
              1;
    }

    // Increase the confirmed common prefix length
    ccpl += icpl;

    // Return a position protected by the bound
    return std::max<int>(std::min<int>(pos, node->get_item_array_len() - 2), 1);
}

void extract_inner_node(InnerNode *node, KVS1 &kvs) {
    Item *item_array = node->get_items();
    uint64_t item_array_length = node->get_item_array_len();
    for (int i = 0; i < item_array_length; ++i) {
        item_array[i].recursive_extract(kvs);
    }
}

// Build an inner node
template <class records>
InnerNode *_try_rebulk_as_model_node(const records &kvs, const int l,
                                     const int r, const int ccpl,
                                     const HPT *model, const PMSS *pmss) {
    // Variables
    int size;
    uint64_t item_array_length, space;
    uint32_t gcpl, icpl, space_for_pfx;
    InnerNode *new_node;
    Item *item_array;
    double min_cdf, max_cdf, k, b;
    int lastIdx, _r_begin, _r_len, tmp_ccpl1, tmp_ccpl2, first_key_idx,
        final_key_idx;
    bool invalid_branch;

    // The stack storing the bulk information
    typedef struct {
        int to_bulk_idx;
        int l_in_kvs;
        int r_in_kvs;
    } bulk_info;
    std::vector<bulk_info> bulk_stack;

    // The number of bulk load keys
    size = (r - l);

    // Determine the sparse item array length
    item_array_length = size * ScaleFactor;

    // Determine the global common prefix length
    gcpl = ucpl(kvs[l].k, kvs[r - 1].k);

    // Determine the incremental common prefix length
    icpl = gcpl - ccpl;

    // Record the space needed for prefix, padding the space to 8x
    space_for_pfx = (icpl + ((icpl % 8) ? (8 - (icpl % 8)) : 0));

    // The total space needed for this node (paralled with 8)
    space = sizeof(InnerNode::header)           // space for fix-sized header
            + space_for_pfx                     // space for prefix
            + item_array_length * sizeof(Item); // space for sparse item array

    // The new model-based inner node
    new_node = (InnerNode *)new uint8_t[space];
    memset(new_node, 0, space);

    // The new node's intercept and slope
    min_cdf = model->getCdf(kvs[l].k, gcpl);
    max_cdf = model->getCdf(kvs[r - 1].k, gcpl);
    if (max_cdf <= min_cdf)
        goto FAIL_TO_BULK;
    k = 1. / (max_cdf - min_cdf);
    b = min_cdf / (min_cdf - max_cdf);

    // Set the fields
    new_node->h.item_array_length = item_array_length;
    new_node->h.num_of_keys = size;
    new_node->h.k = k;
    new_node->h.b = b;
    new_node->h.prefix_length = icpl;
    new_node->h.header_offset = space_for_pfx;
    memcpy(new_node->get_prefix(), kvs[l].k + ccpl, icpl);

    // The begin address of the sparse item array
    item_array = new_node->get_items();

    // Variables in the iteration
    lastIdx = -1;
    invalid_branch = false;

    // Before distribution, we need to clarify the model can discriminate
    // at least two of the keys
    tmp_ccpl1 = ccpl;
    tmp_ccpl2 = ccpl;
    first_key_idx = predictPos(new_node, kvs[l].k, tmp_ccpl1, model);
    final_key_idx = predictPos(new_node, kvs[r - 1].k, tmp_ccpl2, model);

    // If the first key and last key cannot be discriminated, fail to build an
    // model-based inner node
    if (first_key_idx >= final_key_idx) {
        goto FAIL_TO_BULK;
    }

    /*---------------------------------------------*/
    /*---------------------------------------------*/
    /*             distribute the keys             */
    /*---------------------------------------------*/
    /*---------------------------------------------*/

    // Distribute the keys according to the CDFs
    for (int i = l; i < r; ++i) {
        // Avoid change to variable gcpl
        int tmp_ccpl = ccpl;

        // Predict the position according to StringModel
        int idx = predictPos(new_node, kvs[i].k, tmp_ccpl, model);

        // Make sure the indexes are monotonic and valid
        if (idx < lastIdx || idx < 0 || idx >= item_array_length) {
            invalid_branch = true;
            goto FAIL_TO_BULK;
        }

        // Start a new branch
        if (idx != lastIdx) {
            // Build the formar branch
            if (lastIdx >= 0) {
                int ggcpl = ucpl(kvs[_r_begin].k, kvs[_r_begin + _r_len - 1].k);

                // Push the key into bulk load stack
                bulk_stack.push_back({lastIdx, _r_begin, _r_begin + _r_len});
            }

            // Start a new branch
            _r_begin = i;
            _r_len = 1;
        } else {
            _r_len += 1;
        }

        // Update the lastIdx
        lastIdx = idx;
    }

    // CDF are reversed or over boundary due to model imprecision
    // This case rarely happenes in almost every datasets
    if (invalid_branch) {
        goto FAIL_TO_BULK;
    } else {
        // Handle the remain part
        bulk_stack.push_back({lastIdx, _r_begin, _r_begin + _r_len});
    }

    // Bulk load all groups in the stack
    for (int i = 0; i < bulk_stack.size(); ++i) {
        item_array[bulk_stack[i].to_bulk_idx] =
            pmss_bulk(kvs, bulk_stack[i].l_in_kvs, bulk_stack[i].r_in_kvs, gcpl,
                      model, pmss);
    }

    return new_node;

FAIL_TO_BULK:

    delete[] reinterpret_cast<uint8_t *>(new_node);
    return NULL;
}

template <class records>
Item pmss_bulk(const records &kvs, const int l, const int r, const int ccpl,
               const HPT *model, const PMSS *pmss) {
    // The return item
    Item item;

    // Determine the data size
    int size = r - l;

    // Case 1: bulk load as kv-entry
    if (size == 1) {
        item.set_entry(kvs.ret_kv(l));
        return item;
    }

    // Case 2: bulk load as compact leaf node
    else if (size <= CNODE_SIZE) {
        item.set_cnode(new_cnode(kvs, l, r, ccpl));
        return item;
    }

    // Case 3: bulk load as model-based node
    else if (pmss->decideSubType(size, getGPKL(kvs, l, r)) == STYP_Items) {
        auto child = _try_rebulk_as_model_node(kvs, l, r, ccpl, model, pmss);
        if (child) {
            item.set_inner_node(child);
            return item;
        }
    }

    // Case 4: bulk load as sub-trie node
    {
        HOTIndex *subtrie = new HOTIndex;
        HOTBulkload(*subtrie, kvs, l, r);
        item.set_coded_index(*subtrie);

        return item;
    }

    return item;
}

inline kv *trie_search(const Item &item, const str _key) {
    uint64_t subtrie = item.get_coded_index();
    return HOTLookup((HOTIndex &)subtrie, _key);
}

inline kv *sing_search(const Item &item, const str _key, const int ccpl) {
    kv *ret = item.get_entry();
    return ret->verify(_key, ccpl) ? ret : NULL;
}

inline kv *cnod_search(const Item &item, const str _key) {
    return _cnode_search(item.get_cnode(), _key);
}

inline bool trie_insert(Item &node, const str ckey, const val cval) {
    uint64_t coded_subtrie = node.get_coded_index();
    bool result = HOTInsert((HOTIndex &)coded_subtrie, ckey, cval);
    node.set_coded_index((HOTIndex &)coded_subtrie);
    return result;
}

inline bool sing_insert(Item &node, const str ckey, const val cval,
                        const int ccpl) {
    kv *old_entry = node.get_entry();
    int cmpRes = old_entry->keycmp(ckey, ccpl);
    if (cmpRes == 0) {
        return false;
    } else {
        Cnode *new_node = new_empty_cnode(2);
        new_node->h.ccpl = ccpl;
        new_node->h.key_cnt = 2;
        if (cmpRes > 0) {
            new_node->data[0] = new_hash_kv(old_entry);
            new_node->data[1] = new_hash_kv(ckey, cval);
        } else {
            new_node->data[0] = new_hash_kv(ckey, cval);
            new_node->data[1] = new_hash_kv(old_entry);
        }
        node.set_cnode(new_node);
        return true;
    }
}

inline bool cnod_insert(Item &node, const str ckey, const val cval,
                        const HPT *hpt, const PMSS *pmss) {
    Cnode *cnode = node.get_cnode();
    if (cnode->has_room()) {
        bool result = _cnode_withRoom_insert(cnode, ckey, cval);
        node.set_cnode(cnode);
        return result;
    } else {
        // Re bulk load the sub-trie
        int ccpl = cnode->h.ccpl;
        KVS1 kvs;
        if (try_extract_keys_if_valid_insert(cnode, kvs, ckey, cval)) {
            node = pmss_bulk(kvs, 0, CNODE_SIZE + 1, ccpl, hpt, pmss);
            return true;
        }

        return false;
    }
}

inline val trie_upsert(Item &node, const str ckey, const val cval) {
    uint64_t coded_subtrie = node.get_coded_index();
    auto result = HOTUpsert((HOTIndex &)coded_subtrie, ckey, cval);
    node.set_coded_index((HOTIndex &)coded_subtrie);
    if (result)
        return result->read();
    return 0;
}

inline val sing_upsert(Item &node, const str ckey, const val cval,
                       const int ccpl) {
    kv *old_entry = node.get_entry();
    int cmpRes = old_entry->keycmp(ckey, ccpl);
    if (cmpRes == 0) {
        val old_val = old_entry->read();
        old_entry->update(cval);
        return old_val;
    } else {
        Cnode *new_node = new_empty_cnode(2);
        new_node->h.ccpl = ccpl;
        new_node->h.key_cnt = 2;
        if (cmpRes > 0) {
            new_node->data[0] = new_hash_kv(old_entry);
            new_node->data[1] = new_hash_kv(ckey, cval);
        } else {
            new_node->data[0] = new_hash_kv(ckey, cval);
            new_node->data[1] = new_hash_kv(old_entry);
        }
        node.set_cnode(new_node);
        return 0;
    }
}

inline val cnod_upsert(Item &node, const str ckey, const val cval,
                       const HPT *hpt, const PMSS *pmss) {
    Cnode *cnode = node.get_cnode();
    if (cnode->has_room()) {
        val result = _cnode_withRoom_upsert(cnode, ckey, cval);
        node.set_cnode(cnode);
        return result;
    } else {
        // Re bulk load the sub-trie
        int ccpl = cnode->h.ccpl;
        KVS1 kvs;
        val res = try_extract_keys_if_valid_upsert(cnode, kvs, ckey, cval);
        if (res == 0) {
            node = pmss_bulk(kvs, 0, CNODE_SIZE + 1, ccpl, hpt, pmss);
            return 0;
        }
        return res;
    }
}

inline bool sing_remove(Item &node, const str ckey, const int ccpl) {
    kv *old_entry = node.get_entry();
    int cmpRes = old_entry->keycmp(ckey, ccpl);
    if (cmpRes == 0) {
        free_kv(old_entry);
        node.set_null();
        return true;
    }
    return false;
}

inline bool trie_remove(Item &node, const str ckey) {
    uint64_t coded_subtrie = node.get_coded_index();
    bool result = HOTRemove((HOTIndex &)coded_subtrie, ckey);
    node.set_coded_index((HOTIndex &)coded_subtrie);
    return result;
}

inline bool cnod_remove(Item &node, const str ckey, const HPT *hpt,
                        const PMSS *pmss) {
    Cnode *cnode = node.get_cnode();
    if (cnode->more_than_2()) {
        bool result = _cnode_withRoom_remove(cnode, ckey);
        node.set_cnode(cnode);
        return result;
    } else {
        // (possibly) Degrade the cnode into a single entry
        kv *entry = _cnode_degrade(cnode, ckey);
        if (entry == NULL) {
            return false;
        }
        node.set_entry(entry);
        return true;
    }
}

}; // namespace lits
