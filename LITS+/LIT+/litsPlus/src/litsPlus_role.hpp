#pragma once

#include <litsPlus.hpp>

#include <cstdint>

namespace litsPlus {

    enum class IndexRole : uint32_t { Normal = 0,
                                      RebuildSrc = 1,
                                      RebuildDst = 2 };

    template <typename EntryIter>
    class RebuildGuider {
    private:
        IndexRole role;
        int cut;
        Index<EntryIter>* opp;
    };
};
