#pragma once

#include "lits_cnode.hpp"
#include "lits_hot.hpp"
#include "lits_iter.hpp"
#include "lits_kv.hpp"
#include "lits_model.hpp"
#include "lits_node.hpp"

#include <cmath>
#include <stack>

namespace lits {

class LITS {
  private:
    // For bulk load, the index needs at least 1000 strings to train the model
    static const int min_bulk_load_size = 1000;

    // Whether the index has been bulk loaded
    bool hasBeenBuild = false;

    // The Global String Model: Hash-enhanced Prefix Table
    HPT *hpt;

    // The Structural Decision Tree
    PMSS *pmss;

    // The root node of the index.
    Item root;

  public:
    LITS() = default;
    ~LITS() = default;

    bool bulkload(const char **_keys, const uint64_t *_vals, const int _len,
                  HPT *_hpt = NULL) {
        RT_ASSERT(hasBeenBuild == false);
        return _bulkload((const str *)_keys, _vals, _len, _hpt);
    }

    void destroy() {
        RT_ASSERT(hasBeenBuild);
        return _destroy();
    }

    kv *lookup(const char *_key) {
        RT_ASSERT(hasBeenBuild);
        return _lookup((const str)_key);
    }

    bool insert(const char *_key, const uint64_t _val) {
        RT_ASSERT(hasBeenBuild);
        return _insert((const str)_key, (const val)_val);
    }

    /**
     * If update, return the kv_entry's old value
     * If insert, return 0
     */
    val upsert(const char *_key, const uint64_t _val) {
        RT_ASSERT(hasBeenBuild);
        return _upsert((const str)_key, (const val)_val);
    }

    bool remove(const char *_key) {
        RT_ASSERT(hasBeenBuild);
        return _remove((const str)_key);
    }

    litsIter find(const char *_key) const {
        RT_ASSERT(hasBeenBuild);
        return _find((const str)_key);
    }

    litsIter begin() const {
        RT_ASSERT(hasBeenBuild);
        return _begin();
    }

  private:
    bool _bulkload(const str *_keys, const uint64_t *_vals, const int _len,
                   HPT *_hpt = NULL) {
        // Check the input is sorted and unique
        if (_len < min_bulk_load_size) {
            std::cerr << "[Bulk Load]: For bulk load, the index needs at least "
                      << min_bulk_load_size << " strings!" << std::endl;
            return false;
        }

        for (int i = 1; i < _len; ++i) {
            if (ustrcmp(_keys[i], _keys[i - 1]) < 0) {
                std::cerr << "[Bulk Load]: The input strings are not sorted!"
                          << std::endl;
                return false;
            }
            if (ustrcmp(_keys[i], _keys[i - 1]) == 0) {
                std::cerr << "[Bulk Load]: The input strings are not unique!"
                          << std::endl;
                return false;
            }
        }

        // Train the Hash-enhanced Prefix Table
        if (_hpt) {
            hpt = _hpt;
        } else {
            hpt = new HPT();
            hpt->train(_keys, _len);
        }

        // Init the Performance Model for Structure Selection
        pmss = new PMSS();

        // Bulk load the root
        KVS2 kvs = {(const str *)_keys, (const val *)_vals};

        root = pmss_bulk(kvs, 0, _len, 0, hpt, pmss);

        hasBeenBuild = true;
        return true;
    }

    void _destroy() {
        delete hpt;
        delete pmss;

        KVS1 kvs;

        // Delete the main structure of the index
        root.recursive_extract(kvs);

        // Delete the key-value entry of the index
        kvs.self_delete();
    }

    kv *_lookup(const str _key) {
        int ccpl = 0;
        Item item = root;

        while (1) {
            switch (item.get_itype()) {
            case ITYP_Trie: {
                return trie_search(item, _key);
            };
            case ITYP_Sing: {
                return sing_search(item, _key, ccpl);
            };
            case ITYP_CNod: {
                return cnod_search(item, _key);
            }
            case ITYP_Null: {
                return NULL;
            };
            }

            // Recursively locate the position
            item = *item.locate(_key, ccpl, hpt);
        }

        return NULL;
    }

    bool _insert(const str _key, const val _val) {
        int ccpl = 0;
        Item *item = &root;
        PathStack stack(hpt, pmss);
        bool result;

        while (1) {
            switch (item->get_itype()) {
            case ITYP_Trie: {
                result = trie_insert(*item, _key, _val);
                goto RET;
            }
            case ITYP_Sing: {
                result = sing_insert(*item, _key, _val, ccpl);
                goto RET;
            }
            case ITYP_CNod: {
                result = cnod_insert(*item, _key, _val, hpt, pmss);
                goto RET;
            }
            case ITYP_Null: {
                item->set_entry(new_kv(_key, _val));
                result = true;
                goto RET;
            }
            }

            // Record the path
            stack.record_path(item, ccpl);

            // Recursively locate the position
            item = item->locate(_key, ccpl, hpt);
        }

    RET:

        if (result == true) {
            stack.change_num(1);
        }

        return result;
    }

    bool _remove(const str _key) {
        int ccpl = 0;
        Item *item = &root;
        PathStack stack(hpt, pmss);
        bool result;

        while (1) {
            switch (item->get_itype()) {
            case ITYP_Trie: {
                result = trie_remove(*item, _key);
                goto RET;
            }
            case ITYP_Sing: {
                result = sing_remove(*item, _key, ccpl);
                goto RET;
            }
            case ITYP_CNod: {
                result = cnod_remove(*item, _key, hpt, pmss);
                goto RET;
            }
            case ITYP_Null: {
                result = false;
                goto RET;
            }
            }

            // Record the path
            stack.record_path(item, ccpl);

            // Recursively locate the position
            item = item->locate(_key, ccpl, hpt);
        }

    RET:

        if (result == true) {
            stack.change_num(-1);
        }

        return result;
    }

    val _upsert(const str _key, const val _val) {
        int ccpl = 0;
        Item *item = &root;
        PathStack stack(hpt, pmss);
        val result;

        while (1) {
            switch (item->get_itype()) {
            case ITYP_Trie: {
                result = trie_upsert(*item, _key, _val);
                goto RET;
            }
            case ITYP_Sing: {
                result = sing_upsert(*item, _key, _val, ccpl);
                goto RET;
            }
            case ITYP_CNod: {
                result = cnod_upsert(*item, _key, _val, hpt, pmss);
                goto RET;
            }
            case ITYP_Null: {
                item->set_entry(new_kv(_key, _val));
                result = 0;
                goto RET;
            }
            }

            // Record the path
            stack.record_path(item, ccpl);

            // Recursively locate the position
            item = item->locate(_key, ccpl, hpt);
        }

    RET:

        if (result == 0) {
            stack.change_num(1);
        }

        return result;
    }

    litsIter _find(const str _key) const {
        int pos, ccpl = 0;
        Item item = root;

        litsIter iter;

        while (1) {
            switch (item.get_itype()) {
            case ITYP_Trie: {
                trie_find(item, _key, iter);
                return iter;
            };
            case ITYP_Sing: {
                sing_find(item, iter);
                return iter;
            };
            case ITYP_CNod: {
                cnod_find(item, _key, iter);
                return iter;
            }
            case ITYP_Null: {
                iter.set_invalid();
                return iter;
            };
            }

            // Recursively locate the position (and record the path)
            item = *recordPath_find(item, _key, ccpl, hpt, iter);
        }

        iter.set_invalid();
        return iter;
    }

    litsIter _begin() const {
        litsIter iter;
        iter.FIRST(root);
        return iter;
    }
};
}; // namespace lits
