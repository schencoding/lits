#pragma once

#include <litsPlus_base.hpp>
#include <litsPlus_cnode.hpp>
#include <litsPlus_dsf.hpp>
#include <litsPlus_model.hpp>
#include <litsPlus_slot.hpp>

#include <litsPlus_hot.hpp>
#include <litsPlus_pmss.hpp>

#include <string>
#include <vector>

namespace litsPlus {

    template <typename EntryIter>
    class ModelNode {
    public:
        ModelNode() = delete;
        ModelNode(const ModelNode& other) = delete;
        ModelNode(ModelNode&&) = delete;
        ModelNode& operator=(const ModelNode&) = delete;
        ModelNode& operator=(ModelNode&&) = delete;

        ModelNode(EntryIter begin, EntryIter end, HPT* model, int ccpl, LatencyModel<EntryIter>* pmss) {
            // Decide the data size
            const int dataSize = std::distance(begin, end);

            // Malloc the space for slots
            double scaleFactor = DynamicScaleFactor<EntryIter>::DecideScaleFactor(begin, end, model);
            const int slotArrayLen = scaleFactor * dataSize;
            slots_.resize(slotArrayLen);

            // Decide the global common prefix length
            const char* frontKey = (*begin)->key();
            const char* backKey = (*(end - 1))->key();
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
            assert(maxCdf >= minCdf);
            uniformDistribution_ = false;
            if (maxCdf == minCdf) {
            REBUILD_WITH_UNIFORM:
                uniformDistribution_ = true;
                minCdf = model->getCdfUniform(frontKey, gcpl_);
                maxCdf = model->getCdfUniform(backKey, gcpl_);
            }
            slope_ = static_cast<double>(slotArrayLen - 1) / (maxCdf - minCdf);
            intercept_ = 1. - slope_ * minCdf;

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
                const char* key = (*it)->key();

                // The estimated position
                int idx = predict(key, gcpl_);

                // The position changes, prepare a sub-tree
                if (idx < lastIdx) {
                    stack.clear();
                    goto REBUILD_WITH_UNIFORM;
                }
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
                void* _subTree;
                Slot::Type _type;

                if (_subTreeDataSize == 1) {
                    _subTree = *(_it->_begin);
                    _type = Slot::Type::SingleSlot;
                } else if (_subTreeDataSize < MaxCompactLeafSize) {
                    _subTree = reinterpret_cast<void*>(new CompactLeaf(_it->_begin, _it->_end));
                    _type = Slot::Type::CLeafSlot;
                }
#ifdef USE_PMSS
                else if (pmss->PMSS(_it->_begin, _it->_end) == Slot::Type::SubTreeSlot) {
                    HOTIndexInLIT* subTrieHOT = new HOTIndexInLIT;
                    HOTBulkload(*subTrieHOT, _it->_begin, _it->_end);
                    uint64_t code = *reinterpret_cast<uint64_t*>(&(*subTrieHOT));
                    _subTree = reinterpret_cast<void*>(code);
                    _type = Slot::Type::SubTreeSlot;
                }
#endif
                else {
                    _subTree = reinterpret_cast<void*>(new ModelNode(_it->_begin, _it->_end, model, gcpl_, pmss));
                    _type = Slot::Type::MNodeSlot;
                }

                // Set the target Slot
                slots_[_it->_slotIdx] = Slot(_subTree, _type);
            }
        }

        inline const int predict(const char* key, const int gcpl = 0) const {
            const int pos = static_cast<int>(slope_ *
                                                 (uniformDistribution_ ? model_->getCdfUniform(key, gcpl) : model_->getCdf(key, gcpl)) +
                                             intercept_);
            return std::max<int>(1, std::min<int>(pos, slots_.size() - 2));
        }

        inline Slot& locateSlot(const char* key, int& ccpl) {
            // int cmpRes = memcmp(key + ccpl, prefix_.c_str(), gcpl_ - ccpl);
            int cmpRes = std::strncmp(key + ccpl, prefix_.c_str(), gcpl_ - ccpl);
            if (cmpRes < 0)
                return slots_.front();
            else if (cmpRes > 0)
                return slots_.back();
            ccpl = gcpl_;
            return slots_[predict(key, gcpl_)];
        }

        inline Slot& locateSlotReturnIdx(const char* key, int& ccpl, int& slotIdx) {
            // int cmpRes = memcmp(key + ccpl, prefix_.c_str(), gcpl_ - ccpl);
            int cmpRes = std::strncmp(key + ccpl, prefix_.c_str(), gcpl_ - ccpl);
            if (cmpRes < 0) {
                slotIdx = 0;
                return slots_.front();
            } else if (cmpRes > 0) {
                slotIdx = slots_.size() - 1;
                return slots_.back();
            }
            ccpl = gcpl_;
            slotIdx = predict(key, gcpl_);
            return slots_[slotIdx];
        }

        size_t getSizeInBytes() const {
            size_t size = 0;
            size += sizeof(bool);
            size += sizeof(size_t);
            size += sizeof(std::string);
            size += sizeof(double) * 2;
            size += sizeof(HPT*);
            size += sizeof(Slot) * slots_.size();
            for (auto it = slots_.begin(); it != slots_.end(); ++it) {
                Slot _ = *it;
                auto t = _.getType();
                switch (t) {
                case Slot::Type::CLeafSlot: {
                    CompactLeaf* cleaf = static_cast<CompactLeaf*>(_.getPointer());
                    size += cleaf->getSizeInBytes();
                    break;
                }
                case Slot::Type::MNodeSlot: {
                    ModelNode<EntryIter>* mnode = static_cast<ModelNode<EntryIter>*>(_.getPointer());
                    size += mnode->getSizeInBytes();
                    break;
                }
                }
            }
            return size;
        }

    public:
        // For debug
        size_t slotArrayLen() const {
            return slots_.size();
        }

        Slot getSlot(const int idx) {
            return slots_[idx];
        }

    private:
        bool uniformDistribution_;
        size_t gcpl_;
        std::string prefix_;
        double slope_;
        double intercept_;
        HPT* model_;
        std::vector<Slot> slots_;
    };
};