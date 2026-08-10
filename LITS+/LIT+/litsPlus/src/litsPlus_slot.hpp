#pragma once

#include <cassert>
#include <cstdint>
#include <type_traits>

namespace litsPlus {

    class Slot {
    public:
        enum class Type : uint16_t { EmptySlot = 0,
                                     SingleSlot = 1,
                                     MNodeSlot = 2,
                                     CLeafSlot = 3,
                                     SubTreeSlot = 4,
                                     CMNodeSlot = 5 };

    private:
        static constexpr uint64_t PTR_MASK = (1ULL << 48) - 1;  // low 48 bits
        static constexpr uint64_t TYPE_MASK = ~PTR_MASK;        // high 16 bits

        uint64_t data_ = 0;

        static constexpr uint64_t pack(const void* p, Type t) noexcept {
            uint64_t ptr = reinterpret_cast<uintptr_t>(p);
            return (static_cast<uint64_t>(t) << 48) | ptr;
        }

        static constexpr uint64_t pack(const uint64_t p, Type t) noexcept {
            return (static_cast<uint64_t>(t) << 48) | p;
        }

    public:
        Slot() = default;
        Slot(const void* p, Type t) noexcept : data_{pack(p, t)} {}

        void* getPointer() noexcept { return reinterpret_cast<void*>(data_ & PTR_MASK); }

        uint64_t getData() noexcept { return reinterpret_cast<uint64_t>(data_ & PTR_MASK); }

        const void* getPointer() const noexcept { return reinterpret_cast<const void*>(data_ & PTR_MASK); }

        Type getType() const noexcept { return static_cast<Type>(data_ >> 48); }

        void setPointer(const void* p) noexcept { data_ = pack(p, getType()); }

        void setData(const uint64_t value) noexcept { data_ = pack(value, getType()); }

        void setType(Type t) noexcept { data_ = (data_ & PTR_MASK) | (static_cast<uint64_t>(t) << 48); }

        explicit operator bool() const noexcept { return (data_ & PTR_MASK) != 0; }
    };

};
