#pragma once

#include <litsPlus_base.hpp>
#include <litsPlus_cell.hpp>
#include <litsPlus_cnode.hpp>
#include <litsPlus_dsf.hpp>
#include <litsPlus_mnode.hpp>
#include <litsPlus_model.hpp>
#include <litsPlus_slot.hpp>

#include <algorithm>
#include <string>
#include <vector>

namespace litsPlus {

    template <typename EntryIter>
    class CompactModelNode {
    public:
        CompactModelNode() = delete;
        CompactModelNode(const CompactModelNode& other) = delete;
        CompactModelNode(CompactModelNode&&) = delete;
        CompactModelNode& operator=(const CompactModelNode&) = delete;
        CompactModelNode& operator=(CompactModelNode&&) = delete;

        CompactModelNode(EntryIter begin, EntryIter end, HPT* model, int ccpl, LatencyModel<EntryIter>* pmss) {
            // Decide the data size
            const int dataSize = std::distance(begin, end);

            // Malloc the space for slots
            double scaleFactor = DynamicScaleFactor<EntryIter>::DecideScaleFactor(begin, end, model);
            const int cellArrayLen = (scaleFactor * dataSize + Cell::BitsPerCell - 1) / Cell::BitsPerCell;
            slotArrayLen_ = cellArrayLen * Cell::BitsPerCell;
            cells_.resize(cellArrayLen);

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
            // slotArrayLen-1 = slope_ * maxCdf + intercept_
            // 0 = slope_ * minCdf + intercept_
            double minCdf = model->getCdf(frontKey, gcpl_);
            double maxCdf = model->getCdf(backKey, gcpl_);
            assert(maxCdf >= minCdf);
            uniformDistribution_ = false;
            if (maxCdf == minCdf) {
                uniformDistribution_ = true;
                minCdf = model->getCdfUniform(frontKey, gcpl_);
                maxCdf = model->getCdfUniform(backKey, gcpl_);
            }
            slope_ = static_cast<double>(slotArrayLen_ - 1) / (maxCdf - minCdf);
            intercept_ = 1. - slope_ * minCdf;

            // Stack for recursively build
            struct StackElment {
                EntryIter _begin;
                EntryIter _end;
            };
            std::vector<StackElment> stack;

            struct SlotInCell {
                long int _recordCnt;
                int _thisCellOfs;
                size_t _slotIdx;
            };
            std::vector<std::vector<SlotInCell>> cell_helpers(cellArrayLen);

            // Record the first element and its estimated position
            int lastIdx = predict(frontKey, gcpl_);
            EntryIter lastIter = begin;
            for (auto it = begin + 1; it != end; ++it) {
                // The key to place
                const char* key = (*it)->key();

                // The estimated position
                int idx = predict(key, gcpl_);

                // The position changes, prepare a sub-tree
                assert(idx >= lastIdx);
                if (idx != lastIdx) {
                    stack.push_back({lastIter, it});

                    // The estimated cell
                    int cellIdx = lastIdx / Cell::BitsPerCell;
                    int cellOfs = lastIdx % Cell::BitsPerCell;

                    // Record the slot position into cell_helpers
                    cells_[cellIdx].setBit(cellOfs);
                    cell_helpers[cellIdx].push_back({/*_recordCnt*/ std::distance(lastIter, it),
                                                     /*_thisCellOfs*/ cellOfs,
                                                     /*_slotIdx*/ stack.size() - 1});

                    lastIter = it;
                    lastIdx = idx;
                }
            }

            // Place the remain part into a sub-tree
            stack.push_back({lastIter, end});

            // The estimated cell
            int cellIdx = lastIdx / Cell::BitsPerCell;
            int cellOfs = lastIdx % Cell::BitsPerCell;

            // Record the slot position into cell_helpers
            cells_[cellIdx].setBit(cellOfs);
            cell_helpers[cellIdx].push_back({/*_recordCnt*/ std::distance(lastIter, end),
                                             /*_thisCellOfs*/ cellOfs,
                                             /*_slotIdx*/ stack.size() - 1});

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
                } else {
                    _subTree = reinterpret_cast<void*>(new ModelNode(_it->_begin, _it->_end, model, gcpl_, pmss));
                    _type = Slot::Type::MNodeSlot;
                }

                // Set the target Slot
                slots_.push_back(Slot(_subTree, _type));
            }

            // Build the cell array
            for (int i = 0; i < cellArrayLen; ++i) {
                if (cell_helpers[i].empty())
                    continue;
                std::sort(cell_helpers[i].begin(), cell_helpers[i].end(),
                          [](const SlotInCell& a, const SlotInCell& b) {
                              return a._recordCnt > b._recordCnt;
                          });
                for (int j = 0; j < cell_helpers[i].size(); ++j) {
                    if (j < Cell::SlotPerCell) {
                        cells_[i].setCachedSlot(slots_[cell_helpers[i][j]._slotIdx],
                                                cell_helpers[i][j]._thisCellOfs, j);
                    }
                }
            }

            // Generate the rank table
            uint32_t bitSum = 0;
            for (size_t i = 0; i < cellArrayLen; ++i) {
                bitSum = this->cells_[i].getAndSetRank(bitSum);
            }
        }

        inline const int predict(const char* key, const int gcpl = 0) const {
            const int pos = static_cast<int>(slope_ *
                                                 (uniformDistribution_ ? model_->getCdfUniform(key, gcpl) : model_->getCdf(key, gcpl)) +
                                             intercept_);
            return std::max<int>(0, std::min<int>(pos, slotArrayLen_ - 1));
        }

        inline Slot locateSlot(const char* key, int& ccpl) {
            int cmpRes = memcmp(key + ccpl, prefix_.c_str(), gcpl_ - ccpl);
            if (cmpRes < 0)
                return minSlot_;
            else if (cmpRes > 0)
                return maxSlot_;

            ccpl = gcpl_;
            int bitIdx = predict(key, gcpl_);

            // Get the cell index and offset
            int cellIdx = bitIdx / Cell::BitsPerCell;
            int cellOfs = bitIdx % Cell::BitsPerCell;

            // Judge whether the slot is cached by the cell
            Slot _;
            bool isCached = true;
            _ = cells_[cellIdx].findCache(cellOfs, isCached);
            if (isCached == false) {
                int rankValue = cells_[cellIdx].rank(cellOfs);
                if (rankValue == -1) {
                    return minSlot_;
                }
                _ = slots_[rankValue];
            }
            return _;
        }

        inline Slot& locateSlotAndInvalidCache(const std::string& key, int& ccpl) {
            int cmpRes = memcmp(key.c_str() + ccpl, prefix_.c_str(), gcpl_ - ccpl);
            if (cmpRes < 0)
                return minSlot_;
            else if (cmpRes > 0)
                return maxSlot_;

            ccpl = gcpl_;
            int bitIdx = predict(key.c_str(), gcpl_);

            // Get the cell index and offset
            int cellIdx = bitIdx / Cell::BitsPerCell;
            int cellOfs = bitIdx % Cell::BitsPerCell;

            // Judge whether the slot is cached by the cell
            cells_[cellIdx].invalidCache(cellOfs);

            int rankValue = cells_[cellIdx].rank(cellOfs);
            if (rankValue == -1)
                return minSlot_;
            return slots_[rankValue];
        }

        size_t getSizeInBytes() const {
            size_t size = 0;
            size += sizeof(bool);
            size += sizeof(size_t);
            size += sizeof(std::string);
            size += sizeof(double) * 2;
            size += sizeof(HPT*);
            size += sizeof(Slot) * (slots_.size() + 2);
            size += sizeof(Cell) * cells_.size();

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

    private:
        bool uniformDistribution_;
        size_t gcpl_;
        std::string prefix_;
        double slope_;
        double intercept_;
        HPT* model_;
        Slot minSlot_, maxSlot_;
        size_t slotArrayLen_;
        std::vector<Cell> cells_;
        std::vector<Slot> slots_;
    };
};