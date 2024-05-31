#pragma once

#include "lits_base.hpp"
#include "lits_kv.hpp"

namespace lits {

class KV {
  public:
    str k;
    val v;
};

// These two classes are reponsible for the KV iteration
class KVS1 {
  public:
    KVS1() = default;
    KVS1(const KVS1 &other) = delete;
    KV operator[](int index) const { return {d[index]->k, d[index]->v}; }
    kv *ret_kv(int index) const { return d[index]; }
    void push(kv *_kv) { d.push_back(_kv); }
    void self_delete() {
        for (int i = 0; i < d.size(); ++i) {
            free_kv(d[i]);
        }
    }
    int getSize() const { return d.size(); }

  private:
    std::vector<kv *> d;
};

class KVS2 {
  public:
    KVS2(const str *keys, const val *vals) : _keys(keys), _vals(vals) {}
    KV operator[](int index) const { return {_keys[index], _vals[index]}; }
    kv *ret_kv(int index) const { return new_kv(_keys[index], _vals[index]); }

  private:
    const str *_keys;
    const val *_vals;
};

}; // namespace lits