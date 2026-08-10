#pragma once

#include <cstdint>
#include <cstring>
#include <memory>
#include <new>
#include <string>

class Entry {
    size_t keyLen_;
    uint64_t value_;
    char key_[];

    Entry() = delete;
    Entry(const Entry&) = delete;
    Entry& operator=(const Entry&) = delete;
    Entry(Entry&&) = delete;
    Entry& operator=(Entry&&) = delete;

public:
    static Entry* create(const std::string& k, uint64_t v) {
        const std::size_t bytes = sizeof(Entry) + k.size() + 1;
        void* mem = new uint8_t[bytes];
        Entry* p(static_cast<Entry*>(mem));
        p->keyLen_ = k.size();
        p->value_ = v;
        std::memcpy(p->key_, k.c_str(), k.size() + 1);
        return p;
    }

    static Entry* create(const char* k, const int len, uint64_t v) {
        const std::size_t bytes = sizeof(Entry) + len + 1;
        void* mem = new uint8_t[bytes];
        Entry* p(static_cast<Entry*>(mem));
        p->keyLen_ = len;
        p->value_ = v;
        std::memcpy(p->key_, k, len + 1);
        return p;
    }

    static void destroy(Entry* obj) noexcept {
        delete[] reinterpret_cast<uint8_t*>(obj);
    }

    void setValue(uint64_t v) {
        this->value_ = v;
    }

    bool verify(const char* k, int keyLen_, int ccpl) const noexcept {
        return std::memcmp(k + ccpl, key_ + ccpl, keyLen_ - ccpl + 1) == 0;
    }

    const char* key() const noexcept { return key_; }
    int len() const noexcept { return keyLen_; }
    uint64_t value() const noexcept { return value_; }
};
