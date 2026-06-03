#pragma once

#include <cstdint>
#include <functional>
#include <vector>

#include <volk.h>

namespace engine {

struct PendingGpuFree {
    std::uint32_t slot_id = 0;
    std::uint64_t last_used_frame = 0;
};

class GpuDeferredFreeQueue {
public:
    using FreeCallback = std::function<void(std::uint32_t slot_id)>;

    explicit GpuDeferredFreeQueue(std::uint32_t frames_in_flight, FreeCallback on_free = {});

    void set_fences(VkDevice device, const VkFence* frame_submit_fences);
    void set_fence_checker(std::function<bool(std::uint32_t ring_index)> is_signaled);
    void set_free_callback(FreeCallback on_free);

    void enqueue_free(std::uint32_t slot_id, std::uint64_t last_used_frame);
    void process_completed();

    [[nodiscard]] const std::vector<PendingGpuFree>& pending() const { return pending_; }
    [[nodiscard]] std::size_t pending_count() const { return pending_.size(); }

private:
    [[nodiscard]] bool is_fence_signaled(std::uint64_t last_used_frame) const;

    std::uint32_t frames_in_flight_ = 0;
    FreeCallback on_free_;
    VkDevice device_ = VK_NULL_HANDLE;
    const VkFence* frame_submit_fences_ = nullptr;
    std::function<bool(std::uint32_t ring_index)> fence_checker_;
    std::vector<PendingGpuFree> pending_;
};

} // namespace engine
