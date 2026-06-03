#include "engine/render/SnapshotRing.hpp"

#include "engine/render/VkCheck.hpp"

namespace engine {

SnapshotRing::SnapshotRing(std::uint32_t frames_in_flight)
    : slot_count_(snapshot_count(frames_in_flight))
    , snapshot_fences_(slot_count_, VK_NULL_HANDLE)
    , slot_signaled_(slot_count_, true)
    , snapshots_(slot_count_) {}

SnapshotRing::~SnapshotRing() {
    shutdown();
}

void SnapshotRing::init(VkDevice device) {
    if (device_ != VK_NULL_HANDLE || device == VK_NULL_HANDLE) {
        return;
    }

    device_ = device;
    for (std::uint32_t slot = 0; slot < slot_count_; ++slot) {
        const VkFenceCreateInfo create_info{
            .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
            .flags = VK_FENCE_CREATE_SIGNALED_BIT,
        };
        VK_CHECK(vkCreateFence(device_, &create_info, nullptr, &snapshot_fences_[slot]));
    }
}

void SnapshotRing::shutdown() {
    if (device_ == VK_NULL_HANDLE) {
        return;
    }

    for (VkFence fence : snapshot_fences_) {
        if (fence != VK_NULL_HANDLE) {
            vkDestroyFence(device_, fence, nullptr);
        }
    }
    snapshot_fences_.assign(slot_count_, VK_NULL_HANDLE);
    device_ = VK_NULL_HANDLE;
}

} // namespace engine
