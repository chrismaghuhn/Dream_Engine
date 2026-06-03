#include "engine/render/SnapshotRing.hpp"

#include "engine/render/VkCheck.hpp"

#include <spdlog/spdlog.h>

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

bool SnapshotRing::consume_pick_device_lost() {
    const bool lost = pick_device_lost_;
    pick_device_lost_ = false;
    return lost;
}

void SnapshotRing::reset_fences_after_idle() {
    pick_device_lost_ = false;
    if (device_ == VK_NULL_HANDLE) {
        slot_signaled_.assign(slot_count_, true);
        return;
    }

    for (VkFence fence : snapshot_fences_) {
        if (fence != VK_NULL_HANDLE) {
            vkDestroyFence(device_, fence, nullptr);
        }
    }

    for (std::uint32_t slot = 0; slot < slot_count_; ++slot) {
        const VkFenceCreateInfo create_info{
            .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
            .flags = VK_FENCE_CREATE_SIGNALED_BIT,
        };
        const VkResult create_result =
            vkCreateFence(device_, &create_info, nullptr, &snapshot_fences_[slot]);
        if (create_result != VK_SUCCESS) {
            SPDLOG_CRITICAL(
                "Failed to recreate snapshot fence slot {}: {}",
                slot,
                vk_result_string(create_result));
            snapshot_fences_[slot] = VK_NULL_HANDLE;
        }
    }
}

std::uint32_t SnapshotRing::pick_write_slot() {
    for (std::uint32_t slot = 0; slot < slot_count_; ++slot) {
        if (is_slot_signaled(slot)) {
            if (device_ != VK_NULL_HANDLE) {
                const VkResult reset_result = vkResetFences(device_, 1, &snapshot_fences_[slot]);
                if (reset_result == VK_ERROR_DEVICE_LOST) {
                    pick_device_lost_ = true;
                    return slot;
                }
                if (reset_result != VK_SUCCESS) {
                    SPDLOG_CRITICAL(
                        "vkResetFences snapshot slot {}: {}",
                        slot,
                        vk_result_string(reset_result));
                    pick_device_lost_ = true;
                    return slot;
                }
            } else {
                slot_signaled_[slot] = false;
            }
            return slot;
        }
    }

    if (device_ != VK_NULL_HANDLE) {
        for (std::uint32_t slot = 0; slot < slot_count_; ++slot) {
            const VkResult wait_result =
                vkWaitForFences(device_, 1, &snapshot_fences_[slot], VK_TRUE, UINT64_MAX);
            if (wait_result == VK_ERROR_DEVICE_LOST) {
                SPDLOG_WARN("vkWaitForFences device lost on snapshot slot {}", slot);
                pick_device_lost_ = true;
                return slot;
            }
            if (wait_result != VK_SUCCESS) {
                SPDLOG_CRITICAL(
                    "vkWaitForFences snapshot slot {}: {}",
                    slot,
                    vk_result_string(wait_result));
                pick_device_lost_ = true;
                return slot;
            }
            const VkResult reset_result = vkResetFences(device_, 1, &snapshot_fences_[slot]);
            if (reset_result == VK_ERROR_DEVICE_LOST) {
                pick_device_lost_ = true;
                return slot;
            }
            if (reset_result != VK_SUCCESS) {
                SPDLOG_CRITICAL(
                    "vkResetFences snapshot slot {}: {}",
                    slot,
                    vk_result_string(reset_result));
                pick_device_lost_ = true;
                return slot;
            }
            return slot;
        }
    }

    SPDLOG_CRITICAL("no snapshot slot available");
    pick_device_lost_ = true;
    return 0;
}

} // namespace engine
