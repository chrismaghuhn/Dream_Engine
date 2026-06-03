#pragma once

#include <cstdint>
#include <functional>
#include <vector>

#include <volk.h>

namespace engine {

struct PendingGpuFree {
    std::uint32_t slot_id = 0;
    std::uint32_t last_submit_snapshot_slot = 0;
};

class GpuDeferredFreeQueue {
public:
    using FreeCallback = std::function<void(std::uint32_t slot_id)>;

    explicit GpuDeferredFreeQueue(std::uint32_t fence_slot_count, FreeCallback on_free = {});

    void set_fences(VkDevice device, const VkFence* frame_submit_fences);
    void set_fence_checker(std::function<bool(std::uint32_t ring_index)> is_signaled);
    void set_free_callback(FreeCallback on_free);

    void enqueue_free(std::uint32_t slot_id, std::uint32_t last_submit_snapshot_slot);
    void process_completed();
    /// After vkDeviceWaitIdle — run pending frees immediately (e.g. device-lost recovery).
    void flush_pending_immediate();

    [[nodiscard]] const std::vector<PendingGpuFree>& pending() const { return pending_; }
    [[nodiscard]] std::size_t pending_count() const { return pending_.size(); }

private:
    [[nodiscard]] bool is_fence_signaled(std::uint32_t last_submit_snapshot_slot) const;

    std::uint32_t fence_slot_count_ = 0;
    FreeCallback on_free_;
    VkDevice device_ = VK_NULL_HANDLE;
    const VkFence* frame_submit_fences_ = nullptr;
    std::function<bool(std::uint32_t ring_index)> fence_checker_;
    std::vector<PendingGpuFree> pending_;
};

} // namespace engine
