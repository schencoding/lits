#pragma once
/*  Slot — 8‑byte packed pointer + type + spin‑lock
 *
 *  Layout (high → low bits)
 *   63 … 56   lock   (8 bits)  0 = unlocked, 1 = locked
 *   55 … 48   type   (8 bits)  user‑defined enum values
 *   47 …  0   ptr    (48 bits) canonical user‑space pointer
 *
 *  The whole 64‑bit word is manipulated atomically,
 *  so we keep the object size exactly 8 bytes.
 */

#include <atomic>
#include <cstdint>
#include <type_traits>

namespace litsRCU {

class Slot {
  public:
    /* User‑visible slot kinds (fits into 8 bits) */
    enum class Type : uint8_t {
        EmptySlot = 0,
        SingleSlot = 1,
        MNodeSlot = 2,
        CLeafSlot = 3,
        SubTreeSlot = 4,
        ForwardSlot = 5,
        ModelSlot = 6
    };

  public:
    /* Bit masks / shifts */
    static constexpr uint64_t PTR_MASK = (1ULL << 48) - 1; // bits 0‑47
    static constexpr uint64_t TYPE_MASK = 0xFFULL << 48;   // bits 48‑55
    static constexpr uint64_t LOCK_MASK = 0xFFULL << 56;   // bits 56‑63
    static constexpr int TYPE_SHIFT = 48;
    static constexpr int LOCK_SHIFT = 56;

    /* Packed data */
    volatile uint64_t data_{0};

    /* Helper: pack pointer + type + lock */
    static constexpr uint64_t pack(const void *p, Type t, uint8_t lock = 0) noexcept {
        return (static_cast<uint64_t>(lock) << LOCK_SHIFT) | (static_cast<uint64_t>(t) << TYPE_SHIFT) |
               (reinterpret_cast<uintptr_t>(p) & PTR_MASK);
    }

    struct SlotSnapshot {
        uint64_t bits;
        Slot::Type type() const {
            return static_cast<Slot::Type>((bits >> Slot::TYPE_SHIFT) & 0xFF);
        }
        void *ptr() const {
            return reinterpret_cast<void *>(bits & Slot::PTR_MASK);
        }
    };

  public:
    /* ── constructors ─────────────────────────────────────────── */
    Slot() = default;

    Slot(const void *p, Type t) noexcept : data_{pack(p, t)} {
    }

    Slot(Slot &&other) noexcept : data_(other.data_) {
    }

    Slot &operator=(Slot &&other) noexcept {
        data_ = other.data_;
        return *this;
    }

    /* ── basic accessors ────────────────────────── */
    inline SlotSnapshot load() const {
        return SlotSnapshot{data_};
    }

    void *getPointer() noexcept {
        return reinterpret_cast<void *>(data_ & PTR_MASK);
    }

    const void *getPointer() const noexcept {
        return reinterpret_cast<const void *>(data_ & PTR_MASK);
    }

    Type getType() const noexcept {
        return static_cast<Type>((data_ & TYPE_MASK) >> TYPE_SHIFT);
    }

    void setPointer(const void *p) noexcept {
        /* caller should hold the lock */
        uint64_t old = data_;
        uint64_t lock_bits = old & (TYPE_MASK | LOCK_MASK); // keep type + lock
        data_ = lock_bits | (reinterpret_cast<uintptr_t>(p) & PTR_MASK);
    }

    void setType(Type t) noexcept {
        /* caller should hold the lock */
        uint64_t old = data_;
        uint64_t new_val = (old & ~TYPE_MASK) | (static_cast<uint64_t>(t) << TYPE_SHIFT);
        data_ = new_val;
    }

    void update(const void *p, Type t) noexcept {
        uint64_t lock = data_ & LOCK_MASK;

        uint64_t new_val =
            lock | (reinterpret_cast<uintptr_t>(p) & PTR_MASK) | (static_cast<uint64_t>(t) << TYPE_SHIFT);

        data_ = new_val;
    }

    explicit operator bool() const noexcept {
        return (data_ & PTR_MASK) != 0;
    }

    /* ── tiny spin‑lock (8 bits) ──────────────────────────────── */
    void lock2() noexcept {
        uint64_t cur;
        for (;;) {
            /* read the slot */
            cur = data_;
            /* already locked → spin */
            if (cur & LOCK_MASK) {
                continue;
            }
            /* try to set lock bit */
            uint64_t desired = cur | (1ULL << LOCK_SHIFT);
            if (__atomic_compare_exchange_n(&data_, &cur, desired, true, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {
                break; // success
            }
        }
    }

    /* ── tiny spin‑lock (8 bits) ──────────────────────────────── */
    void lock() noexcept {
        while (true) {
            /* read the slot */
            uint64_t cur = data_;
            /* already locked → spin */
            if ((cur & LOCK_MASK) == 0) {
                uint64_t desired = cur | (1ULL << LOCK_SHIFT);
                /* try to set lock bit */
                if (__atomic_compare_exchange_n(&data_, &cur, desired, true, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {
                    return; // success
                }
            }
        }
    }

    bool try_lock() noexcept {
        uint64_t cur = data_;
        if (cur & LOCK_MASK)
            return false; // already locked
        uint64_t desired = cur | (1ULL << LOCK_SHIFT);
        return __atomic_compare_exchange_n(&data_, &cur, desired, true, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED);
    }

    void unlock() noexcept {
        /* clear lock bit */
        uint64_t cur = data_;
        data_ = cur & ~LOCK_MASK;
    }
};

/* Ensure we really stayed at 8 bytes */
static_assert(sizeof(Slot) == 8, "Slot must remain 8 bytes!");

class SlotInfo {
  public:
    Slot::Type t;
    void *pointer;

    SlotInfo() = default;
    SlotInfo(Slot::Type _t, void *_p) : t(_t), pointer(_p) {
    }
};

} // namespace litsRCU
