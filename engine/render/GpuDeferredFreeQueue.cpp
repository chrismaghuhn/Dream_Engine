#include "engine/render/GpuDeferredFreeQueue.hpp"

#include <volk.h>

#include <algorithm>

namespace engine {

GpuDeferredFreeQueue::GpuDeferredFreeQueue(std::uint32_t frames_in_flight, FreeCallback on_free)
    : frames_in_flight_(frames_in_flight > 0 ? frames_in_flight : 1)
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

void GpuDeferredFreeQueue::enqueue_free(std::uint32_t slot_id, std::uint64_t last_used_frame) {
    pending_.push_back(PendingGpuFree{slot_id, last_used_frame});
}

bool GpuDeferredFreeQueue::is_fence_signaled(std::uint64_t last_used_frame) const {
    const std::uint32_t ring = static_cast<std::uint32_t>(last_used_frame % frames_in_flight_);

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
                       if (!is_fence_signaled(entry.last_used_frame)) {
                           return false;
                       }

                       if (on_free_) {
                           on_free_(entry.slot_id);
                       }
                       return true;
                   }),
                   pending_.end());
}

} // namespace engine
