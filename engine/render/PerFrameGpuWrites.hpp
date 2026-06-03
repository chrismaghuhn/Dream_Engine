#pragma once

#include "engine/core/EngineConfig.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

#include <volk.h>

namespace engine {

struct PerFrameGpuWrites {
    VkBuffer indirect_draw_buffer = VK_NULL_HANDLE;
    VkBuffer frame_ubo = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkDeviceSize memory_size = 0;
    VkDeviceSize ubo_size = 0;
    VkDeviceSize indirect_size = 0;
};

class PerFrameGpuWriteRing {
public:
    PerFrameGpuWriteRing(std::uint32_t frames_in_flight,
                         const GpuCaps& caps,
                         std::size_t max_indirect_draws,
                         std::size_t ubo_bytes);

    void init(VkDevice device, VkPhysicalDevice physical_device);
    void shutdown();

    [[nodiscard]] std::uint32_t ring_index(std::uint64_t frame_index) const;
    [[nodiscard]] PerFrameGpuWrites& slot(std::uint64_t frame_index);
    [[nodiscard]] const PerFrameGpuWrites& slot(std::uint64_t frame_index) const;
    [[nodiscard]] std::size_t aligned_dynamic_offset(std::size_t offset) const;
    [[nodiscard]] std::uint32_t frames_in_flight() const { return frames_in_flight_; }

private:
    [[nodiscard]] static std::size_t align_up(std::size_t value, std::size_t alignment);

    std::uint32_t frames_in_flight_ = 0;
    GpuCaps caps_{};
    std::size_t ubo_bytes_ = 0;
    std::size_t indirect_bytes_ = 0;
    std::size_t slot_bytes_ = 0;
    VkDevice device_ = VK_NULL_HANDLE;
    std::vector<PerFrameGpuWrites> per_frame_;
};

} // namespace engine
