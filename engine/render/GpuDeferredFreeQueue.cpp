#include "engine/render/GpuDeferredFreeQueue.hpp"

#include <volk.h>

#include <algorithm>

namespace engine {

GpuDeferredFreeQueue::GpuDeferredFreeQueue(std::uint32_t fence_slot_count, FreeCallback on_free)
    : fence_slot_count_(fence_slot_count > 0 ? fence_slot_count : 1)
    , on_free_(std::move(on_free)) {}

void GpuDeferredFreeQueue::set_fences(VkDevice device, const VkFence* frame_submit_fences) {
    device_ = device;
    frame_submit_fences_ = frame_submit_fences;
    fence_checker_ = {};
}

void GpuDeferredFreeQueue::set_fence_checker(std::function<bool(std::uint32_t ring_index)> is_signaled) {
    fence_checker_ = std::move(is_signaled);
    device_ = VK_NULL_HANDLE;
    frame_submit_fences_ = nullptr;
}

void GpuDeferredFreeQueue::set_free_callback(FreeCallback on_free) {
    on_free_ = std::move(on_free);
}

void GpuDeferredFreeQueue::enqueue_free(std::uint32_t slot_id,
                                        const std::uint32_t last_submit_snapshot_slot) {
    if (slot_id == 0) {
        return;
    }

    for (const PendingGpuFree& entry : pending_) {
        if (entry.slot_id == slot_id) {
            return;
        }
    }

    pending_.push_back(PendingGpuFree{slot_id, last_submit_snapshot_slot});
}

bool GpuDeferredFreeQueue::is_fence_signaled(const std::uint32_t last_submit_snapshot_slot) const {
    if (fence_slot_count_ == 0) {
        return false;
    }

    const std::uint32_t ring = last_submit_snapshot_slot % fence_slot_count_;

    if (fence_checker_) {
        return fence_checker_(ring);
    }

    if (device_ == VK_NULL_HANDLE || frame_submit_fences_ == nullptr) {
        return false;
    }

    return vkGetFenceStatus(device_, frame_submit_fences_[ring]) == VK_SUCCESS;
}

void GpuDeferredFreeQueue::process_completed() {
    pending_.erase(std::remove_if(pending_.begin(), pending_.end(), [&](const PendingGpuFree& entry) {
                       if (!is_fence_signaled(entry.last_submit_snapshot_slot)) {
                           return false;
                       }

                       if (on_free_) {
                           on_free_(entry.slot_id);
                       }
                       return true;
                   }),
                   pending_.end());
}

void GpuDeferredFreeQueue::flush_pending_immediate() {
    for (const PendingGpuFree& entry : pending_) {
        if (on_free_) {
            on_free_(entry.slot_id);
        }
    }
    pending_.clear();
}

} // namespace engine
