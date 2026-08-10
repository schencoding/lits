#pragma once

#include <litsPlus_mnode.hpp>
#ifdef USE_CELL_ROOT
#include <litsPlus_cmnode.hpp>
#endif
#include <litsPlus_iter.hpp>

#include <algorithm>

namespace litsPlus
{

template <typename EntryIter> class Index
{
  public:
    Index() = default;

    void checkPMSSFilesExist(const std::string &litR, const std::string &litW, const std::string &hotR,
                             const std::string &hotW)
    {
        auto check = [](const std::string &path) {
            std::ifstream f(path);
            if (!f.is_open())
            {
                throw std::runtime_error("PMSS file not found or unreadable: " + path);
            }
        };

        check(litR);
        check(litW);
        check(hotR);
        check(hotW);
    }

#ifndef USE_PMSS
    void build(EntryIter begin, EntryIter end)
    {
#else
    void build(EntryIter begin, EntryIter end, const std::string &pmssDir, double ReadRatio = 1.0)
    {
        std::string litR = pmssDir + "/litR.csv";
        std::string litW = pmssDir + "/litW.csv";
        std::string hotR = pmssDir + "/hotR.csv";
        std::string hotW = pmssDir + "/hotW.csv";

        checkPMSSFilesExist(litR, litW, hotR, hotW);

        // Init the PMSS model
        pmss = new LatencyModel<EntryIter>(litR, litW, hotR, hotW, ReadRatio);
#endif
        // Train the HPT model
        model_ = new HPT;
        model_->train(begin, end);

#ifdef USE_CELL_ROOT
        // Bulk load the index
        void *mnode = static_cast<void *>(new CompactModelNode<EntryIter>(begin, end, model_, 0, pmss));
        root_ = Slot(mnode, Slot::Type::CMNodeSlot);
#else
        // Bulk load the index
        void *mnode = static_cast<void *>(new ModelNode<EntryIter>(begin, end, model_, 0, pmss));
        root_ = Slot(mnode, Slot::Type::MNodeSlot);
#endif
    }

    Entry *lookup(const char *key, const int keyLen) const
    {
        Slot _ = root_;
        int ccpl = 0;
        do
        {
            auto t = _.getType();
            auto p = _.getPointer();
            switch (t)
            {
            case Slot::Type::SingleSlot: {
                Entry *record = static_cast<Entry *>(p);
                return record->verify(key, keyLen, ccpl) ? record : NULL;
            }
            case Slot::Type::CLeafSlot: {
                CompactLeaf *cleaf = static_cast<CompactLeaf *>(p);
                return cleaf->lookup(key, keyLen, ccpl);
            }
            case Slot::Type::MNodeSlot: {
                ModelNode<EntryIter> *mnode = static_cast<ModelNode<EntryIter> *>(p);
                _ = mnode->locateSlot(key, ccpl);
                break;
            }
#ifdef USE_PMSS
            case Slot::Type::SubTreeSlot: {
                uint64_t subTrie = _.getData();
                return HOTLookup((HOTIndexInLIT &)(subTrie), key);
            }
#endif
#ifdef USE_CELL_ROOT
            case Slot::Type::CMNodeSlot: {
                CompactModelNode<EntryIter> *cmnode = static_cast<CompactModelNode<EntryIter> *>(_.getPointer());
                _ = cmnode->locateSlot(key, ccpl);
                break;
            }
#endif
            case Slot::Type::EmptySlot: {
                return NULL;
            }
            }
        } while (true);
    }

    bool append(const char *key, const int keyLen, const uint64_t value)
    {
        Slot *_ = &root_;
        int ccpl = 0;
        do
        {
            auto t = _->getType();
            switch (t)
            {
            case Slot::Type::EmptySlot: {
                _->setPointer(Entry::create(key, keyLen, value));
                _->setType(Slot::Type::SingleSlot);
                return true;
            }
            case Slot::Type::SingleSlot: {
                Entry *oldEntry = static_cast<Entry *>(_->getPointer());
                CompactLeaf *cleaf = new CompactLeaf(oldEntry, Entry::create(key, keyLen, value));
                _->setPointer((void *)cleaf);
                _->setType(Slot::Type::CLeafSlot);
                return true;
            }
            case Slot::Type::CLeafSlot: {
                Entry *newEntry = Entry::create(key, keyLen, value);
                CompactLeaf *cleaf = static_cast<CompactLeaf *>(_->getPointer());
                bool result = cleaf->append(newEntry);
                if (result == false)
                {
                    std::vector<Entry *> entries = cleaf->extract();
                    entries.push_back(newEntry);
                    delete cleaf;
                    std::sort(entries.begin(), entries.end(), [](const Entry *a, const Entry *b) noexcept {
                        return std::strcmp(a->key(), b->key()) < 0;
                    });
                    ModelNode<EntryIter> *mnode =
                        new ModelNode<EntryIter>(entries.begin(), entries.end(), model_, ccpl);
                    _->setPointer((void *)mnode);
                    _->setType(Slot::Type::MNodeSlot);
                }
                return true;
            }
            case Slot::Type::MNodeSlot: {
                ModelNode<EntryIter> *mnode = static_cast<ModelNode<EntryIter> *>(_->getPointer());
                _ = &mnode->locateSlot(key, ccpl);
                break;
            }
#ifdef USE_PMSS
            case Slot::Type::SubTreeSlot: {
                uint64_t subTrie = _->getData();
                HOTInsert((HOTIndexInLIT &)(subTrie), key, value);
                _->setData(subTrie);
            }
#endif
#ifdef USE_CELL_ROOT
            case Slot::Type::CMNodeSlot: {
                CompactModelNode<EntryIter> *cmnode = static_cast<CompactModelNode<EntryIter> *>(_->getPointer());
                _ = &cmnode->locateSlotAndInvalidCache(key, ccpl);
                break;
            }
#endif
            }
        } while (true);
    }

    /**
     * @return True if insertion, false if update
     */
    bool upsert(const char *key, const int keyLen, const uint64_t value)
    {
        Slot *_ = &root_;
        int ccpl = 0;
        do
        {
            auto t = _->getType();
            switch (t)
            {
            case Slot::Type::EmptySlot: {
                _->setPointer(Entry::create(key, keyLen, value));
                _->setType(Slot::Type::SingleSlot);
                return true;
            }
            case Slot::Type::SingleSlot: {
                Entry *oldEntry = static_cast<Entry *>(_->getPointer());
                if (oldEntry->verify(key, keyLen, ccpl))
                {
                    oldEntry->setValue(value);
                    return false;
                }
                CompactLeaf *cleaf = new CompactLeaf(oldEntry, Entry::create(key, keyLen, value));
                _->setPointer((void *)cleaf);
                _->setType(Slot::Type::CLeafSlot);
                return true;
            }
            case Slot::Type::CLeafSlot: {
                CompactLeaf *cleaf = static_cast<CompactLeaf *>(_->getPointer());
                bool exists = false;
                bool result = cleaf->upsert(key, keyLen, value, ccpl, exists);
                if (exists)
                {
                    return false;
                }
                if (result == false)
                {
                    Entry *newEntry = Entry::create(key, keyLen, value);
                    std::vector<Entry *> entries = cleaf->extract();
                    entries.push_back(newEntry);
                    delete cleaf;
                    std::sort(entries.begin(), entries.end(), [](const Entry *a, const Entry *b) noexcept {
                        return std::strcmp(a->key(), b->key()) < 0;
                    });
                    ModelNode<EntryIter> *mnode =
                        new ModelNode<EntryIter>(entries.begin(), entries.end(), model_, ccpl, pmss);
                    _->setPointer((void *)mnode);
                    _->setType(Slot::Type::MNodeSlot);
                }
                return true;
            }
            case Slot::Type::MNodeSlot: {
                ModelNode<EntryIter> *mnode = static_cast<ModelNode<EntryIter> *>(_->getPointer());
                _ = &mnode->locateSlot(key, ccpl);
                break;
            }
#ifdef USE_PMSS
            case Slot::Type::SubTreeSlot: {
                uint64_t subTrie = _->getData();
                auto kv = HOTUpsert((HOTIndexInLIT &)(subTrie), key, value);
                _->setData(subTrie);
                return kv == NULL ? true : false;
            }
#endif
#ifdef USE_CELL_ROOT
            case Slot::Type::CMNodeSlot: {
                CompactModelNode<EntryIter> *cmnode = static_cast<CompactModelNode<EntryIter> *>(_->getPointer());
                _ = &cmnode->locateSlotAndInvalidCache(key, ccpl);
                break;
            }
#endif
            }
        } while (true);
    }

#ifndef USE_CELL_ROOT
    litsPlusIter<EntryIter> find(const char *key, const int keyLen)
    {
        return litsPlusIter<EntryIter>::find(root_, key, keyLen);
    }
#else
    litsPlusIter<EntryIter> find(const char *key, const int keyLen)
    {
        assert(false && "For CNode, Scan is not implemented.");
        return litsPlusIter<EntryIter>::find(root_, key, keyLen);
    }
#endif

    size_t getSizeInBytes() const
    {
        Slot _ = root_;
        auto t = _.getType();
        switch (t)
        {
        case Slot::Type::CLeafSlot: {
            CompactLeaf *cleaf = static_cast<CompactLeaf *>(_.getPointer());
            return cleaf->getSizeInBytes();
        }
        case Slot::Type::MNodeSlot: {
            ModelNode<EntryIter> *mnode = static_cast<ModelNode<EntryIter> *>(_.getPointer());
            return mnode->getSizeInBytes();
        }
#ifdef USE_CELL_ROOT
        case Slot::Type::CMNodeSlot: {
            CompactModelNode<EntryIter> *cmnode = static_cast<CompactModelNode<EntryIter> *>(_.getPointer());
            return cmnode->getSizeInBytes();
        }
#endif
        }
        return 0;
    }

  private:
    HPT *model_;
    LatencyModel<EntryIter> *pmss;
    Slot root_;
};

}; // namespace litsPlus