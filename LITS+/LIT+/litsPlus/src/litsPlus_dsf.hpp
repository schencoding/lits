#pragma once

#include <litsPlus_model.hpp>

#include <cassert>
#include <vector>

namespace litsPlus {

    template <typename EntryIter>
    class DynamicScaleFactor {
    public:
        static double DecideScaleFactor(EntryIter begin, EntryIter end, HPT* model) {

            // Decide the datasize
            const size_t dataSize = std::distance(begin, end);

            // The init scale factor
            double scalefactor = DefaultScaleFactor;

            // Decide the global common prefix length
            const char* frontKey = (*begin)->key();
            const char* backKey = (*(end - 1))->key();
            int gcpl = ustrcpl(frontKey, backKey);

            // Calculate the local linear model
            // slotArrayLen-2 = slope * maxCdf + intercept
            // 1 = slope * minCdf + intercept
            double slope, intercept;
            double minCdf = model->getCdf(frontKey, gcpl);
            double maxCdf = model->getCdf(backKey, gcpl);
            assert(maxCdf >= minCdf);
            bool uniformdistribution = false;
            if (maxCdf == minCdf) {
                uniformdistribution = true;
                minCdf = model->getCdfUniform(frontKey, gcpl);
                maxCdf = model->getCdfUniform(backKey, gcpl);
            }

            do {
                // The valid slot count
                size_t validSlotCnt = 0;

                // Decide the init slot array length
                size_t slotArrayLen = dataSize * scalefactor;
                slotArrayLen = std::max<size_t>(slotArrayLen, 4);

                // The pre-place position array
                std::vector<bool> positions(slotArrayLen, false);

                // Decide the slope and intercept according to the slotArrayLen
                slope = static_cast<double>(slotArrayLen - 1) / (maxCdf - minCdf);
                intercept = 1. - slope * minCdf;

                // Go through all entries and record their position
                for (auto it = begin; it != end; ++it) {
                    const char* key = (*it)->key();
                    int pos = static_cast<int>(slope *
                                                   (uniformdistribution ? model->getCdfUniform(key, gcpl) : model->getCdf(key, gcpl)) +
                                               intercept);
                    pos = std::max<int>(0, std::min<int>(slotArrayLen - 1, pos));

                    if (positions[pos] == false) {
                        validSlotCnt++;
                        positions[pos] = true;
                    }
                }

                // Check if the valid slot ratio satisfy the limit
                double _validSlotRatio = static_cast<double>(validSlotCnt) / slotArrayLen;
                if (_validSlotRatio >= ValidRatioLimit) {
                    return scalefactor;
                } else {
                    double ac = _validSlotRatio / ValidRatioLimit;
                    scalefactor *= std::min<double>(ac, 0.8);
                }

            } while (true);
        }

    public:
        static constexpr double ValidRatioLimit = 0.05;
        static constexpr int DefaultScaleFactor = 2;
    };

};