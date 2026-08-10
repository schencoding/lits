#pragma once

#include <litsRCU_ebr.hpp>
#include <litsRCU_mnode.hpp>
#include <litsRCU_rebuild.hpp>

#include <algorithm>
#include <chrono>
#include <queue>
#include <random>
#include <unordered_set>
#include <x86intrin.h>

namespace litsRCU {

template <typename EntryIter> class Index {
  public:
    Index() : ebr_(EpochBasedMemoryReclamationStrategy<EntryIter>::getInstance()) {
    }

    Index(HPT *model, ModelNode<EntryIter> *mroot)
        : model_(model), root_(mroot), ebr_(EpochBasedMemoryReclamationStrategy<EntryIter>::getInstance()) {
    }

    ~Index() {
        stop_background();
        delete model_;
        deleteMNode<EntryIter>((void *)root_);
    }

    void start_background() {
        stop_.store(false);
        bg_ = std::thread(&Index::background, this);
    }

    void stop_background() {
        stop_.store(true);
        if (bg_.joinable())
            bg_.join();
    }

    void background() {
        // std::this_thread::sleep_for(std::chrono::seconds(10));
        while (!stop_.load()) {
            std::this_thread::sleep_for(std::chrono::seconds(5));

            if (stop_.load())
                break;
            if (rebuildFlags_.CheckRebuild()) {
                std::cout << "Start Rebuild.\n";
                NonBlockRebuild();
                std::cout << "Rebuild Over\n";
            }

            // Every 5 sec try to advance
            ebr_->tryToAdvance();
        }
    }

    void initRebuildProgress(EntryIter begin, EntryIter end) {
        const size_t n = static_cast<size_t>(std::distance(begin, end));

        std::mt19937_64 rng(2025);
        std::uniform_int_distribution<size_t> dist(0, n - 1);

        const size_t W = windowSize;

        for (size_t i = 0; i < W; ++i) {
            size_t idx = dist(rng);
            Entry *e = *(begin + idx);
            const char *key = e->key();
            int keyLen = e->len(), ccpl = 0;

            size_t sid = root_->locateIdx(key, ccpl);

            slotIdWindow_[i].store(sid, std::memory_order_relaxed);
        }

        historyDistinct_.store(CountDistinct(), std::memory_order_relaxed);

        std::cout << "[History Distinct Slot Id Num]: " << historyDistinct_ << std::endl;

        // Start background rebuilder
        if (!disableRebuild)
            start_background();
    }

    void build(EntryIter begin, EntryIter end) {
        // Train the hash-enhanced prefix table
        model_ = new HPT;
        model_->train(begin, end);

        // Bulk load the index
        root_ = new ModelNode<EntryIter>(begin, end, model_, 0);

        // Init the depth window
        initRebuildProgress(begin, end);
    }

    void build(EntryIter begin, EntryIter end, unsigned threads) {
        const size_t n = std::distance(begin, end);
        if (threads <= 1 || n < 1'000) {
            build(begin, end);
            return;
        }

        model_ = new HPT;
        model_->train(begin, end);

        root_ = new ModelNode<EntryIter>(begin, end, model_, 0, /*isEmpty=*/true);
        int gcpl_ = root_->gcpl_;

        std::vector<EntryIter> pivots;
        pivots.push_back(begin);
        int ccpl;
        size_t curSid = root_->locateIdx((*begin)->key(), ccpl = 0 /*dummy*/);
        for (EntryIter it = std::next(begin); it != end; ++it) {
            size_t sid = root_->locateIdx((*it)->key(), ccpl = 0 /*dummy*/);
            if (sid != curSid) {
                pivots.push_back(it);
                curSid = sid;
            }
        }
        pivots.push_back(end);

        std::atomic<size_t> cursor{0};
        std::vector<std::thread> pool;
        pool.reserve(threads);

        auto worker = [&] {
            for (;;) {
                size_t idx = cursor.fetch_add(1, std::memory_order_relaxed);
                if (idx + 1 >= pivots.size())
                    break;

                EntryIter s = pivots[idx];
                EntryIter e = pivots[idx + 1];
                size_t sid = root_->locateIdx((*s)->key(), ccpl = 0 /*dummy*/);

                void *ptr;
                Slot::Type tp;
                size_t len = std::distance(s, e);
                if (len == 1) {
                    ptr = *s;
                    tp = Slot::Type::SingleSlot;
                } else if (len < MaxCompactLeafSize) {
                    ptr = new CompactLeaf(s, e);
                    tp = Slot::Type::CLeafSlot;
                } else {
                    ptr = new ModelNode<EntryIter>(s, e, model_, /*ccpl=*/gcpl_);
                    tp = Slot::Type::MNodeSlot;
                }

                Slot &dst = root_->slots_[sid];
                dst.update(ptr, tp);
            }
        };

        for (unsigned t = 0; t < threads; ++t)
            pool.emplace_back(worker);
        for (auto &th : pool)
            th.join();

        // Init the depth window
        initRebuildProgress(begin, end);
    }

    size_t CountDistinct(void) const {
        std::unordered_set<int64_t> distinct;
        distinct.reserve(windowSize * 2);

        for (size_t i = 0; i < windowSize; ++i) {
            int64_t val = slotIdWindow_[i].load(std::memory_order_relaxed);
            distinct.insert(val);
        }
        return distinct.size();
    }

    size_t distinctSlotIdNum() const {
        return CountDistinct();
    }

    inline void recordSlotId(size_t c, bool quiet = false) const {
        uint32_t pos = slotIdPos_.fetch_add(1, std::memory_order_relaxed);
        pos %= windowSize;
        size_t old = slotIdWindow_[pos].exchange(c, std::memory_order_relaxed);
        bool need_further_check = (pos == (windowSize - 1));
        if (need_further_check) {
            size_t distinctCnt = CountDistinct();
            bool too_small = (distinctCnt < (minRebuildThreshold * historyDistinct_));
            if (too_small) {
                rebuildFlags_.RequestRebuild();
                historyDistinct_ = distinctCnt;
            }
        }
    }

    const Entry *lookup(const char *key, const int keyLen) const {
        EBRGuard guard(ebr_);
        return _lookup(key, keyLen);
    }

    const Entry *_lookup(const char *key, const int keyLen) const {
        while (true) {
            int ccpl = 0;

            int64_t cut = rebuildFlags_.Cut_.load(std::memory_order_acquire);

            if (unlikely(cut > 0)) { // rebuild in progress
                size_t sid = rebuildFlags_.newRoot_->locateIdx(key, ccpl /*dummy*/);
                if (sid < static_cast<size_t>(cut)) {
                    auto result = rebuildFlags_.newRoot_->lookup(key, keyLen, ccpl = 0);
                    return result.type == mResult::Type::Found ? result.data : NULL;
                }
                /* else fall through to old index below */
            }

            /* normal (or sid ≥ cut) => old index */
            // Only when the rebuild is not in progress will measure cycles
            mResult result = root_->lookup(key, keyLen, ccpl);
            if (unlikely(result.type == mResult::Type::Retry)) {
                continue;
            } else {
                return result.type == mResult::Type::Found ? result.data : NULL;
            }
        }
    }

    bool insert(Entry *newEntry) {
        EBRGuard guard(ebr_);

        while (true) {
            int ccpl = 0;
            int64_t cut = rebuildFlags_.Cut_.load(std::memory_order_acquire);

            /* pick target root based on current Cut_ */
            ModelNode<EntryIter> *tgtRoot;
            if (cut > 0) {
                size_t sid = rebuildFlags_.newRoot_->locateIdx(newEntry->key(), ccpl /*dummy*/);
                if (sid < static_cast<size_t>(cut)) {
                    tgtRoot = rebuildFlags_.newRoot_; // write to new index
                    ccpl = 0;                         // reset prefix for new tree
                } else {
                    tgtRoot = root_; // still write old index
                }
            } else {
                tgtRoot = root_; // no rebuild
            }

            if (disableRebuild == false) {
                thread_local uint32_t probe_cnt = 0;
                bool measureSlotId = (++probe_cnt % measureGap) == 0 && (cut <= 0);
                if (unlikely(measureSlotId)) {
                    ccpl = 0;
                    size_t sid = tgtRoot->locateIdx(newEntry->key(), ccpl);
                    recordSlotId(sid, true);
                }
            }

            /* try append; on contention retry */
            if (tgtRoot->_RootAppend(newEntry, guard))
                return true; // success
            /* else: lock contention => loop and re-decide with fresh Cut_ */
        }
    }

    size_t getSizeInBytes() const {
        return root_->getSizeInBytes();
    }

    void NonBlockRebuild() {

        EBRGuard guard(ebr_);

        // 1. Extract current entries for rebuilding
        std::vector<Entry *> entries;
        entries = _extract();

        // 2. Train a new model on all entries
        HPT *newModel = new HPT;
        newModel->train(entries.begin(), entries.end());

        // 3. Create a new empty root node with the new model
        auto newRoot = new ModelNode<EntryIter>(entries.begin(), entries.end(), newModel, /*ccpl=*/0, /*isEmpty=*/true);
        entries.clear();
        rebuildFlags_.start(newRoot);

        // The tagged entry
        struct Tagged {
            Entry *rec;
            size_t sid;
        };

        struct LockedSlot {
            Slot *slot; // Pointer to slots on old index
            size_t sid; // Slot id on new index
        };

        std::queue<Tagged> Q; // FIFO buffer
        std::queue<LockedSlot> lockedQ;

        size_t i = 0; // current old-slot index
        const size_t N = root_->slots_.size();

        while (i < N || !Q.empty()) {
            // EBRGuard guard(ebr_);
            /* -------- collection phase --------
             * push old-slot entries until TWO different sid
             * values exist in the queue                                    */
            while (i < N && (Q.empty() || Q.front().sid == Q.back().sid)) // queue owns single sid
            {
                Slot &s = root_->slots_[i];
                s.lock();

                /* extract all entries in old slot i (lock-free sketch) */
                std::vector<Entry *> es;
                _extract(es, s); // old slot i will be empty
                std::sort(es.begin(), es.end(),
                          [](const Entry *a, const Entry *b) noexcept { return std::strcmp(a->key(), b->key()) < 0; });

                ++i;

                /* Record the first entry */
                if (!es.empty()) {
                    int _ccpl = 0;
                    size_t sidSlot = newRoot->locateIdx(es.back()->key(), _ccpl);
                    lockedQ.push({&s, sidSlot});
                } else {
                    if (lockedQ.empty()) {
                        lockedQ.push({&s, 0});
                    } else {
                        size_t sidSlot = lockedQ.back().sid;
                        lockedQ.push({&s, sidSlot});
                    }
                }

                /* tag and enqueue */
                for (Entry *e : es) {
                    int ccpl = 0;
                    size_t sid = newRoot->locateIdx(e->key(), ccpl);
                    Q.push({e, sid});
                }
            }

            /* -------- processing phase --------
             * queue now has at least one full batch (front.sid),
             * dequeue that whole batch, build subtree, attach to new root   */
            if (!Q.empty()) {
                size_t sid = Q.front().sid;
                std::vector<Entry *> bucket;
                while (!Q.empty() && Q.front().sid == sid) {
                    bucket.push_back(Q.front().rec);
                    Q.pop();
                }

                /* decide subtree type by bucket size */
                void *subtreePtr = nullptr;
                Slot::Type subType;

                if (bucket.size() == 1) {
                    /* one record → store pointer directly */
                    subtreePtr = bucket.front();
                    subType = Slot::Type::SingleSlot;
                } else if (bucket.size() < MaxCompactLeafSize) {
                    /* small batch → CompactLeaf */
                    subtreePtr = new CompactLeaf(bucket.begin(), bucket.end());
                    subType = Slot::Type::CLeafSlot;
                } else {
                    /* large batch → build a ModelNode */
                    subtreePtr =
                        new ModelNode<EntryIter>(bucket.begin(), bucket.end(), newModel, /*ccpl=*/newRoot->gcpl_);
                    subType = Slot::Type::MNodeSlot;
                }

                /* attach to new root */
                Slot &dst = newRoot->slots_[sid];
                dst.update(subtreePtr, subType);

                rebuildFlags_.Cut_.store(sid + 1, std::memory_order_release);

                while (!lockedQ.empty() && lockedQ.front().sid == sid) {
                    auto oldType = lockedQ.front().slot->getType();
                    auto oldPtr = lockedQ.front().slot->getPointer();

                    lockedQ.front().slot->update(NULL, Slot::Type::ForwardSlot);
                    lockedQ.front().slot->unlock();
                    // Now this slot can be delete, shedule for deletion
                    if (oldType == Slot::Type::MNodeSlot || oldType == Slot::Type::CLeafSlot) {
                        guard.scheduleForDeletion({oldType, oldPtr});
                    }
                    lockedQ.pop();
                }
            }
        }

        auto oldModel = this->model_;
        auto oldRoot = this->root_;
        this->model_ = newModel;
        this->root_ = newRoot;
        guard.scheduleForDeletion({Slot::Type::ModelSlot, oldModel});
        guard.scheduleForDeletion({Slot::Type::MNodeSlot, oldRoot});

        rebuildFlags_.end();

        // rebuildFlags_.Cut_.store(0, std::memory_order_release);
        // rebuildFlags_.newRoot_ = NULL;
    }

  private:
    std::vector<Entry *> _extract() const {
        std::vector<Entry *> result;
        root_->extract(result);
        return result;
    }

    static void _extract(std::vector<Entry *> &result, Slot &slotRef) {
        auto t = slotRef.getType();
        switch (t) {
        case Slot::Type::CLeafSlot: {
            // Cleaf itself doesn't need lock
            const CompactLeaf *cleaf = static_cast<const CompactLeaf *>(slotRef.getPointer());
            auto newEs = cleaf->extract();
            result.insert(result.end(), newEs.begin(), newEs.end());
            break;
        }
        case Slot::Type::MNodeSlot: {
            ModelNode<EntryIter> *mnode = static_cast<ModelNode<EntryIter> *>(slotRef.getPointer());
            for (auto it = mnode->slots_.begin(); it != mnode->slots_.end(); ++it) {
                Slot &sr = *it;
                _extract(result, sr);
            }
            break;
        }
        case Slot::Type::SingleSlot: {
            Entry *e = static_cast<Entry *>(slotRef.getPointer());
            result.push_back(e);
            break;
        }
        }
    }

  public:
    EpochBasedMemoryReclamationStrategy<EntryIter> *const ebr_;
    HPT *model_;
    ModelNode<EntryIter> *root_;
    mutable RebuildGuider<EntryIter> rebuildFlags_;

    // Background rebuilder
    std::thread bg_;
    std::atomic<bool> stop_{false};

  public:
#ifdef ENABALE_REBUILD
    static constexpr bool disableRebuild = false;
#else
    static constexpr bool disableRebuild = true;
#endif
    static constexpr double minRebuildThreshold = 0.33;
    static const int windowSize = 20000;
    static const int measureGap = 1ULL << 10;
    mutable std::array<std::atomic<int64_t>, windowSize> slotIdWindow_;
    mutable std::atomic<uint32_t> slotIdPos_{0};
    mutable std::atomic<size_t> historyDistinct_{0};
};
}; // namespace litsRCU