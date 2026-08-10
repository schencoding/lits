#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <litsRCU_cnode.hpp>
#include <litsRCU_mnode.hpp>
#include <litsRCU_slot.hpp>
#include <tbb/enumerable_thread_specific.h>
#include <thread>
#include <vector>

namespace litsRCU {

constexpr uint32_t NUM_EPOCHS = 3;
constexpr uint32_t NOT_IN_EPOCH = NUM_EPOCHS;

template <typename EntryIter> struct alignas(64) LocalEBRInfo {
    std::array<std::vector<SlotInfo>, NUM_EPOCHS> freeList_;
    std::atomic<uint8_t> localEpoch_{NOT_IN_EPOCH};
    uint8_t previousEpoch_{NOT_IN_EPOCH};
    bool wantsToAdvance_{false};

    ~LocalEBRInfo() {
        for (uint32_t i = 0; i < NUM_EPOCHS; ++i)
            freeForEpoch(i);
    }

    inline void scheduleForDeletion(SlotInfo si) {
        auto e = localEpoch_.load(std::memory_order_relaxed);
        assert(e != NOT_IN_EPOCH);
        auto &vec = freeList_[e];
        vec.emplace_back(si);
        wantsToAdvance_ = ((vec.size() & 63u) == 0);
    }

    inline uint32_t getLocalEpoch() const {
        return localEpoch_.load(std::memory_order_relaxed);
    }

    inline void enter(uint32_t newEpoch) {
        if (previousEpoch_ != newEpoch) {
            freeForEpoch(newEpoch);
            wantsToAdvance_ = false;
            previousEpoch_ = static_cast<uint8_t>(newEpoch);
        }
        localEpoch_.store(static_cast<uint8_t>(newEpoch), std::memory_order_release);
    }

    inline void leave() {
        localEpoch_.store(static_cast<uint8_t>(NOT_IN_EPOCH), std::memory_order_release);
    }

    inline void wantToAdvance() {
        wantsToAdvance_ = true;
    }

    inline bool doesThreadWantToAdvanceEpoch() const {
        return wantsToAdvance_;
    }

  private:
    inline void freeForEpoch(uint32_t epoch) {
        auto &lst = freeList_[epoch];
        for (SlotInfo &si : lst) {
            if (si.t == Slot::Type::CLeafSlot) {
                delete static_cast<CompactLeaf *>(si.pointer);
            } else if (si.t == Slot::Type::MNodeSlot) {
                deleteMNode<EntryIter>(si.pointer);
            } else if (si.t == Slot::Type::ModelSlot) {
                delete static_cast<HPT *>(si.pointer);
            } else {
                assert(false);
            }
        }
        lst.clear();
    }
};

template <typename EntryIter> class EpochBasedMemoryReclamationStrategy {

    std::atomic<uint32_t> currentEpoch_{0};

    using LocalEBRInfo_ = LocalEBRInfo<EntryIter>;

    tbb::enumerable_thread_specific<LocalEBRInfo_, tbb::cache_aligned_allocator<LocalEBRInfo_>,
                                    tbb::ets_key_per_instance>
        threadInfos_;

  public:
    static EpochBasedMemoryReclamationStrategy *getInstance() {
        static EpochBasedMemoryReclamationStrategy inst;
        return &inst;
    }

    inline void enterCriticalSection() {
        auto &info = threadInfos_.local();
        uint32_t cur = currentEpoch_.load(std::memory_order_relaxed);
        info.enter(cur);
        if (info.doesThreadWantToAdvanceEpoch()) {
            if (canAdvance(cur)) {
                uint32_t expected = cur;
                (void)currentEpoch_.compare_exchange_strong(expected, NEXT_EPOCH[cur], std::memory_order_relaxed,
                                                            std::memory_order_relaxed);
            }
        }
    }

    inline void tryToAdvance() {
        auto &info = threadInfos_.local();
        info.wantToAdvance();
        enterCriticalSection();
        leaveCriticialSection();
    }

    inline void leaveCriticialSection() {
        auto &info = threadInfos_.local();
        info.leave();
    }

    inline void scheduleForDeletion(SlotInfo si) {
        auto &info = threadInfos_.local();
        info.scheduleForDeletion(si);
    }

    bool canAdvance(uint32_t cur) {
        uint32_t prev = PREVIOUS_EPOCH[cur];
        return !std::any_of(threadInfos_.begin(), threadInfos_.end(),
                            [prev](LocalEBRInfo_ const &t) { return t.getLocalEpoch() == prev; });
    }

    uint32_t NEXT_EPOCH[3] = {1, 2, 0};
    uint32_t PREVIOUS_EPOCH[3] = {2, 0, 1};
};

template <typename EntryIter> class EBRGuard {
    EpochBasedMemoryReclamationStrategy<EntryIter> *ebr_;

  public:
    explicit EBRGuard(EpochBasedMemoryReclamationStrategy<EntryIter> *ebr) noexcept : ebr_(ebr) {
        ebr_->enterCriticalSection();
    }
    ~EBRGuard() {
        if (ebr_)
            ebr_->leaveCriticialSection();
    }
    EBRGuard(const EBRGuard &) = delete;
    EBRGuard &operator=(const EBRGuard &) = delete;
    EBRGuard(EBRGuard &&o) noexcept : ebr_(o.ebr_) {
        o.ebr_ = nullptr;
    }

    inline void wantToAdvance() noexcept {
        ebr_->wantToAdvance();
    }

    inline void scheduleForDeletion(SlotInfo si) const noexcept {
        ebr_->scheduleForDeletion(si);
    }
};

} // namespace litsRCU
