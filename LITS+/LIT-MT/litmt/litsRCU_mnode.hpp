#pragma once

#include <litsRCU_base.hpp>
#include <litsRCU_cnode.hpp>
#include <litsRCU_dsf.hpp>
#include <litsRCU_model.hpp>
#include <litsRCU_slot.hpp>

#include <string>
#include <vector>

namespace litsRCU {

template <typename EntryIter> class EBRGuard;

template <typename EntryIter> class ModelNode {
  public:
    ModelNode() = delete;
    ModelNode(const ModelNode &other) = delete;
    ModelNode(ModelNode &&) = delete;
    ModelNode &operator=(const ModelNode &) = delete;
    ModelNode &operator=(ModelNode &&) = delete;

    ModelNode(EntryIter begin, EntryIter end, HPT *model, int ccpl, const bool empty = false) {
        // Decide the data size
        const int dataSize = std::distance(begin, end);

        // Malloc the space for slots
        double scaleFactor = DynamicScaleFactor<EntryIter>::DecideScaleFactor(begin, end, model);
        const int slotArrayLen = scaleFactor * dataSize;
        slots_.resize(slotArrayLen);

        // Decide the global common prefix length
        const char *frontKey = (*begin)->key();
        const char *backKey = (*(end - 1))->key();
        gcpl_ = ustrcpl(frontKey, backKey);

        // Store the incremental prefix
        if (gcpl_ > ccpl)
            prefix_ = std::string(frontKey + ccpl, gcpl_ - ccpl);
        else
            prefix_ = std::string("");

        // Set the model
        model_ = model;

        // Calculate the local linear model
        // slotArrayLen-2 = slope_ * maxCdf + intercept_
        // 1 = slope_ * minCdf + intercept_
        double minCdf = model->getCdf(frontKey, gcpl_);
        double maxCdf = model->getCdf(backKey, gcpl_);
        uniformDistribution_ = false;
        if (maxCdf <= minCdf) {
        REBUILD_WITH_UNIFORM:
            uniformDistribution_ = true;
            minCdf = model->getCdfUniform(frontKey, gcpl_);
            maxCdf = model->getCdfUniform(backKey, gcpl_);
        }
        assert(maxCdf > minCdf);
        slope_ = static_cast<double>(slotArrayLen - 3) / (maxCdf - minCdf);
        intercept_ = 1. - slope_ * minCdf;

        // Recursively build the sub-tree when the emptyFlag is false
        if (empty)
            return;

        // Stack for recursively build
        struct StackElment {
            EntryIter _begin;
            EntryIter _end;
            int _slotIdx;
        };
        std::vector<StackElment> stack;

        // Record the first element and its estimated position
        int lastIdx = predict(frontKey, gcpl_);
        EntryIter lastIter = begin;
        for (auto it = begin + 1; it != end; ++it) {
            // The key to place
            const char *key = (*it)->key();

            // The estimated position
            int idx = predict(key, gcpl_);

            // The position changes, prepare a sub-tree
            if (idx < lastIdx) {
                // std::cout << "Data Size: " << dataSize << std::endl;
                // std::cout << "Frst Key: " << frontKey << std::endl;
                // std::cout << "Frst Idx: " << predict(frontKey, gcpl_) << std::endl;
                // std::cout << "Last Key: " << (*lastIter)->key() << std::endl;
                // std::cout << "Last Idx: " << lastIdx << std::endl;
                // std::cout << "Curr Key: " << (*it)->key() << std::endl;
                // std::cout << "Curr Idx: " << idx << std::endl;
                // std::cout << "Last Key: " << backKey << std::endl;
                // std::cout << "Last Idx: " << predict(backKey, gcpl_) << std::endl;
                assert(uniformDistribution_ == false);
                stack.clear();
                goto REBUILD_WITH_UNIFORM;
            }
            assert(idx >= lastIdx);
            if (idx != lastIdx) {
                stack.push_back({lastIter, it, lastIdx});
                lastIter = it;
                lastIdx = idx;
            }
        }

        // Place the remain part into a sub-tree
        stack.push_back({lastIter, end, lastIdx});

        // Build the sub-trees in the stack
        for (auto _it = stack.begin(); _it != stack.end(); ++_it) {
            // Decide the sub-tree type according to the dataSize
            const int _subTreeDataSize = std::distance(_it->_begin, _it->_end);
            void *_subTree;
            Slot::Type _type;

            if (_subTreeDataSize == 1) {
                _subTree = *(_it->_begin);
                _type = Slot::Type::SingleSlot;
            } else if (_subTreeDataSize < MaxCompactLeafSize) {
                _subTree = reinterpret_cast<void *>(new CompactLeaf(_it->_begin, _it->_end));
                _type = Slot::Type::CLeafSlot;
            } else {
                _subTree = reinterpret_cast<void *>(new ModelNode(_it->_begin, _it->_end, model, gcpl_));
                _type = Slot::Type::MNodeSlot;
            }

            // Set the target Slot
            slots_[_it->_slotIdx] = Slot(_subTree, _type);
        }
    }

    inline const int predict(const char *key, const int gcpl = 0) const {
        const int pos = static_cast<int>(
            slope_ * (uniformDistribution_ ? model_->getCdfUniform(key, gcpl) : model_->getCdf(key, gcpl)) +
            intercept_);
        return std::max<int>(1, std::min<int>(pos, slots_.size() - 2));
    }

    inline size_t locateIdx(const char *key, int &ccpl) const {
        int cmpRes = memcmp(key + ccpl, prefix_.c_str(), gcpl_ - ccpl);
        if (cmpRes < 0)
            return 0;
        else if (cmpRes > 0)
            return slots_.size() - 1;
        ccpl = gcpl_;
        return predict(key, gcpl_);
    }

    inline const Slot *locateSlotConst(const size_t idx) const {
        return &slots_[idx];
    }

    inline const Slot *locateSlotConst(const char *key, int &ccpl) const {
        int cmpRes = std::strncmp(key + ccpl, prefix_.c_str(), gcpl_ - ccpl);
        if (cmpRes < 0)
            return &slots_.front();
        else if (cmpRes > 0)
            return &slots_.back();
        ccpl = gcpl_;
        return &slots_[predict(key, gcpl_)];
    }

    inline Slot &locateSlot(const int idx) {
        return slots_[idx];
    }

    inline Slot &locateSlot(const char *key, int &ccpl) {
        int cmpRes = std::strncmp(key + ccpl, prefix_.c_str(), gcpl_ - ccpl);
        if (cmpRes < 0)
            return slots_.front();
        else if (cmpRes > 0)
            return slots_.back();
        ccpl = gcpl_;
        return slots_[predict(key, gcpl_)];
    }

    mResult lookup(const char *key, const int keyLen, int &ccpl) const {
        // Locate the target slot using HPT
        const Slot *_ = this->locateSlotConst(key, ccpl);

        
        // Traverse down until a leaf or empty slot
        do {

            Slot::SlotSnapshot ss = _->load();
            switch (ss.type()) {
            case Slot::Type::SingleSlot: {
                const Entry *rec = static_cast<const Entry *>(ss.ptr());
                // verify and return or nullptr
                auto type = rec->verify(key, keyLen, ccpl) ? mResult::Type::Found : mResult::Type::NotFound;
                return {type, rec};
            }
            case Slot::Type::CLeafSlot: {
                const CompactLeaf *cleaf = static_cast<const CompactLeaf *>(ss.ptr());
                
                // delegate to leaf lookup
                const Entry *rec = cleaf->lookup(key, keyLen, ccpl);
                auto type = rec ? mResult::Type::Found : mResult::Type::NotFound;
                return {type, rec};
            }
            case Slot::Type::MNodeSlot: {
                const ModelNode<EntryIter> *mnode = static_cast<const ModelNode<EntryIter> *>(ss.ptr());
                // descend one level
                _ = mnode->locateSlotConst(key, ccpl);
                break;
            }
            case Slot::Type::EmptySlot: {
                // not found
                return {mResult::Type::NotFound, NULL};
            }
            case Slot::Type::ForwardSlot: {
                // The data has been moved to the new index
                // Retry
                return {mResult::Type::Retry, NULL};
            }
            }
        } while (true);
    }

    /**
     * This API can only be called when this mnode is root.
     */
    bool _RootAppend(Entry *newEntry, EBRGuard<EntryIter> &guard) {
        int ccpl = 0;
        Slot *cur = &locateSlot(newEntry->key(), ccpl);
        Slot *possibleSlotOnRoot = cur;

        if (possibleSlotOnRoot->try_lock() == false) {
            return false;
        }

        // Perform CAS-style append under lock
        while (true) {
            switch (cur->getType()) {
            case Slot::Type::EmptySlot: {
                // empty => insert single entry
                cur->update(newEntry, Slot::Type::SingleSlot);
                goto SUCCESS;
            }

            case Slot::Type::SingleSlot: {
                // one entry exists => upgrade to CLeaf
                Entry *old = static_cast<Entry *>(cur->getPointer());
                if (old->verify(newEntry->key(), newEntry->len(), ccpl)) {
                    goto FAIL;
                }
                auto *leaf = new CompactLeaf(old, newEntry);
                cur->update(leaf, Slot::Type::CLeafSlot);
                goto SUCCESS;
            }

            case Slot::Type::CLeafSlot: {
                // compact leaf => try in-place append
                auto oldLeaf = static_cast<CompactLeaf *>(cur->getPointer());
                auto newLeaf = new CompactLeaf(*oldLeaf);

                bool exists = false;
                bool result = newLeaf->append(newEntry, ccpl, exists);

                // Entry already exist
                if (exists) {
                    guard.scheduleForDeletion({Slot::Type::CLeafSlot, newLeaf});
                    goto FAIL;
                } else {
                    guard.scheduleForDeletion({Slot::Type::CLeafSlot, oldLeaf});
                }

                // Append successfully
                if (result) {
                    cur->update(newLeaf, Slot::Type::CLeafSlot);
                    goto SUCCESS;
                }

                // leaf full => split to model node
                std::vector<Entry *> es = newLeaf->extract();
                es.push_back(newEntry);
                std::sort(es.begin(), es.end(),
                          [](const Entry *a, const Entry *b) noexcept { return std::strcmp(a->key(), b->key()) < 0; });

                auto *mn = new ModelNode<EntryIter>(es.begin(), es.end(), model_, ccpl);
                cur->update(mn, Slot::Type::MNodeSlot);
                guard.scheduleForDeletion({Slot::Type::CLeafSlot, newLeaf});
                goto SUCCESS;
            }

            case Slot::Type::MNodeSlot: {
                // internal node => descend via lock coupling
                auto *mn = static_cast<ModelNode<EntryIter> *>(cur->getPointer());
                Slot *nxt = &mn->locateSlot(newEntry->key(), ccpl);
                cur = nxt;
                continue;
            }
            case Slot::Type::ForwardSlot: {
                goto FAIL;
            }
            }
        }

    SUCCESS:
        possibleSlotOnRoot->unlock();
        return true;

    FAIL:
        possibleSlotOnRoot->unlock();
        return false;
    }

    size_t getSizeInBytes() const {
        size_t size = 0;
        size += sizeof(bool);
        size += sizeof(size_t);
        size += sizeof(std::string);
        size += sizeof(double) * 2;
        size += sizeof(HPT *);
        size += sizeof(Slot) * slots_.size();
        for (auto it = slots_.begin(); it != slots_.end(); ++it) {
            const Slot *_ = &(*it);
            auto t = _->getType();
            switch (t) {
            case Slot::Type::CLeafSlot: {
                const CompactLeaf *cleaf = static_cast<const CompactLeaf *>(_->getPointer());
                size += cleaf->getSizeInBytes();
                break;
            }
            case Slot::Type::MNodeSlot: {
                const ModelNode<EntryIter> *mnode = static_cast<const ModelNode<EntryIter> *>(_->getPointer());
                size += mnode->getSizeInBytes();
                break;
            }
            }
        }
        return size;
    }

    void extract(std::vector<Entry *> &result) const {
        for (auto it = slots_.begin(); it != slots_.end(); ++it) {
            Slot::SlotSnapshot ss = (*it).load();
            auto t = ss.type();
            switch (t) {
            case Slot::Type::CLeafSlot: {
                const CompactLeaf *cleaf = static_cast<const CompactLeaf *>(ss.ptr());
                auto newEs = cleaf->extract();
                std::sort(newEs.begin(), newEs.end(), [](const litsRCU::Entry *a, const litsRCU::Entry *b) noexcept {
                    return std::strcmp(a->key(), b->key()) < 0;
                });
                result.insert(result.end(), newEs.begin(), newEs.end());
                break;
            }
            case Slot::Type::MNodeSlot: {
                const ModelNode<EntryIter> *mnode = static_cast<const ModelNode<EntryIter> *>(ss.ptr());
                mnode->extract(result);
                break;
            }
            case Slot::Type::SingleSlot: {
                Entry *e = static_cast<Entry *>(ss.ptr());
                result.push_back(e);
                break;
            }
            }
        }
    }

  public:

    void showMyself() const {
        using std::cout;
        using std::endl;

        cout << "========== ModelNode ==========" << endl;
        cout << "Is uniform?                 : " << (uniformDistribution_ ? "true" : "false") << endl;
        cout << "Global Partial Key Length   : " << gcpl_ << endl;
        cout << "Prefix                      : " << prefix_ << endl;
        cout << "Slope                       : " << slope_ << endl;
        cout << "Intercept                   : " << intercept_ << endl;
        cout << "HPT model ptr               : " << (model_ ? "Found" : "null") << endl;
        cout << "Number of Slots             : " << slots_.size() << endl;
        cout << "===============================" << endl;
    }

  public:
    bool uniformDistribution_;
    size_t gcpl_;
    std::string prefix_;
    double slope_;
    double intercept_;
    HPT *model_;

  public:
    std::vector<Slot> slots_;
};

template <typename EntryIter> void deleteMNode(void *pointer, bool recursive = true) {
    if (!pointer)
        return;

    auto *mnode = static_cast<ModelNode<EntryIter> *>(pointer);

    if (recursive) {
        for (auto it = mnode->slots_.begin(); it != mnode->slots_.end(); ++it) {
            Slot::SlotSnapshot ss = (*it).load();
            switch (ss.type()) {
            case Slot::Type::MNodeSlot: {
                auto *child = static_cast<ModelNode<EntryIter> *>(ss.ptr());
                deleteMNode<EntryIter>(child, true);
                break;
            }
            case Slot::Type::CLeafSlot: {
                auto *cleaf = static_cast<CompactLeaf *>(ss.ptr());
                delete cleaf;
                break;
            }
            case Slot::Type::SingleSlot: {
                break;
            }
            default:
                break;
            }
        }
    }

    delete mnode;
}

}; // namespace litsRCU