#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>

#include <litsRCU.hpp>

namespace litsRCU {

template <typename EntryIter> class Index;

template <typename EntryIter> class RebuildGuider {
  public:
    // cut_ = 0 => no rebuild in progress
    // cut_ > 0 => slots [0, cut_) have been migrated to the new index
    std::atomic<int64_t> Cut_{0};
    ModelNode<EntryIter> *newRoot_{nullptr};

    mutable std::mutex mlock_; // Protect needRebuild_ and building
    bool needRebuild_{false};  // Only modified with mlock_

  public:
    RebuildGuider() = default;

    bool RequestRebuild() {
        mlock_.lock();
        bool ret;
        if (needRebuild_) {
            ret = false;
        } else {
            std::cout << "Request Rebuild.\n";
            needRebuild_ = true;
            ret = true;
        }
        mlock_.unlock();
        return ret;
    }

    bool CheckRebuild() {
        mlock_.lock();
        bool ret;
        if (needRebuild_) {
            ret = true;
        } else {
            ret = false;
        }
        mlock_.unlock();
        return ret;
    }

    void start(ModelNode<EntryIter> *ptr) {
        newRoot_ = ptr;
        Cut_.store(0, std::memory_order_release);
        // 0 slot has been published
    }

    void end() {
        Cut_.store(0, std::memory_order_release);
        newRoot_ = NULL;
        mlock_.lock();
        needRebuild_ = false;
        mlock_.unlock();
    }
};

} // namespace litsRCU
