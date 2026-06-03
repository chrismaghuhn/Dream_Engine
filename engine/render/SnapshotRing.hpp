#pragma once

#include "engine/render/WorldRenderSnapshot.hpp"

#include <cassert>
#include <cstdint>
#include <vector>

#include <volk.h>

namespace engine {

class SnapshotRing {
public:
    static constexpr std::uint32_t snapshot_count(std::uint32_t frames_in_flight) {
        return frames_in_flight + 1;
    }

    explicit SnapshotRing(std::uint32_t frames_in_flight);
    ~SnapshotRing();

    SnapshotRing(const SnapshotRing&) = delete;
    SnapshotRing& operator=(const SnapshotRing&) = delete;

    void init(VkDevice device);
    void shutdown();

    [[nodiscard]] std::uint32_t slot_count() const { return slot_count_; }
    [[nodiscard]] VkFence fence(std::uint32_t slot) const { return snapshot_fences_[slot]; }
    [[nodiscard]] WorldRenderSnapshot& snapshot(std::uint32_t slot) { return snapshots_[slot]; }
    [[nodiscard]] const WorldRenderSnapshot& snapshot(std::uint32_t slot) const {
        return snapshots_[slot];
    }

    /// Returns a snapshot slot whose GPU work has finished; resets its fence for the next submit.
    [[nodiscard]] std::uint32_t pick_write_slot();

    /// After vkDeviceWaitIdle / device-lost: recreate unsignaled fences (call after vkDeviceWaitIdle).
    void reset_fences_after_idle();

    /// True if the last pick_write_slot() hit VK_ERROR_DEVICE_LOST (cleared on read).
    [[nodiscard]] bool consume_pick_device_lost();

    void mark_submitted(std::uint32_t slot) {
        assert(slot < slot_count_);
        if (device_ == VK_NULL_HANDLE) {
            slot_signaled_[slot] = false;
        }
    }

    void mark_gpu_complete(std::uint32_t slot) {
        assert(slot < slot_count_);
        if (device_ == VK_NULL_HANDLE) {
            slot_signaled_[slot] = true;
        }
    }

private:
    [[nodiscard]] bool is_slot_signaled(std::uint32_t slot) const {
        if (device_ != VK_NULL_HANDLE) {
            const VkFence fence = snapshot_fences_[slot];
            if (fence == VK_NULL_HANDLE) {
                return true;
            }
            return vkGetFenceStatus(device_, fence) == VK_SUCCESS;
        }
        return slot_signaled_[slot];
    }

    VkDevice device_ = VK_NULL_HANDLE;
    std::uint32_t slot_count_;
    bool pick_device_lost_ = false;
    std::vector<VkFence> snapshot_fences_;
    std::vector<bool> slot_signaled_;
    std::vector<WorldRenderSnapshot> snapshots_;
};

} // namespace engine
