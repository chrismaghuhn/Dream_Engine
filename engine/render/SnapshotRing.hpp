#pragma once

#include <cassert>
#include <cstdint>
#include <vector>

#include <vulkan/vulkan.h>

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

    [[nodiscard]] std::uint32_t pick_write_slot() const {
        for (std::uint32_t slot = 0; slot < slot_count_; ++slot) {
            if (is_slot_signaled(slot)) {
                return slot;
            }
        }
        assert(false && "no signaled snapshot slot available");
        return 0;
    }

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
            return vkGetFenceStatus(device_, snapshot_fences_[slot]) == VK_SUCCESS;
        }
        return slot_signaled_[slot];
    }

    VkDevice device_ = VK_NULL_HANDLE;
    std::uint32_t slot_count_;
    std::vector<VkFence> snapshot_fences_;
    std::vector<bool> slot_signaled_;
};

} // namespace engine
