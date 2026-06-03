#include "engine/render/PerFrameGpuWrites.hpp"

#include "engine/render/VkCheck.hpp"

#include <volk.h>

#include <algorithm>

namespace engine {

namespace {

[[nodiscard]] std::uint32_t find_memory_type(VkPhysicalDevice physical_device,
                                             std::uint32_t type_filter,
                                             VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memory_properties{};
    vkGetPhysicalDeviceMemoryProperties(physical_device, &memory_properties);

    for (std::uint32_t i = 0; i < memory_properties.memoryTypeCount; ++i) {
        if ((type_filter & (1u << i)) != 0 &&
            (memory_properties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }

    return UINT32_MAX;
}

} // namespace

PerFrameGpuWriteRing::PerFrameGpuWriteRing(std::uint32_t frames_in_flight,
                                           const GpuCaps& caps,
                                           std::size_t max_indirect_draws,
                                           std::size_t ubo_bytes)
    : frames_in_flight_(frames_in_flight > 0 ? frames_in_flight : 1)
    , caps_(caps)
    , ubo_bytes_(align_up(ubo_bytes, caps.min_uniform_buffer_offset_alignment > 0
                                       ? caps.min_uniform_buffer_offset_alignment
                                       : 1))
    , indirect_bytes_(align_up(max_indirect_draws * sizeof(VkDrawIndexedIndirectCommand),
                               caps.min_uniform_buffer_offset_alignment > 0
                                   ? caps.min_uniform_buffer_offset_alignment
                                   : 1))
    , slot_bytes_(ubo_bytes_ + indirect_bytes_)
    , per_frame_(frames_in_flight_) {
    for (PerFrameGpuWrites& slot : per_frame_) {
        slot.ubo_size = static_cast<VkDeviceSize>(ubo_bytes_);
        slot.indirect_size = static_cast<VkDeviceSize>(indirect_bytes_);
        slot.memory_size = static_cast<VkDeviceSize>(slot_bytes_);
    }
}

std::size_t PerFrameGpuWriteRing::align_up(std::size_t value, std::size_t alignment) {
    if (alignment == 0) {
        return value;
    }
    return (value + alignment - 1) & ~(alignment - 1);
}

void PerFrameGpuWriteRing::init(VkDevice device, VkPhysicalDevice physical_device) {
    if (device_ != VK_NULL_HANDLE || device == VK_NULL_HANDLE) {
        return;
    }

    device_ = device;

    for (PerFrameGpuWrites& slot : per_frame_) {
        const VkBufferCreateInfo buffer_info{
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size = static_cast<VkDeviceSize>(slot_bytes_),
            .usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        };
        VK_CHECK(vkCreateBuffer(device_, &buffer_info, nullptr, &slot.frame_ubo));

        VkMemoryRequirements requirements{};
        vkGetBufferMemoryRequirements(device_, slot.frame_ubo, &requirements);

        const std::uint32_t memory_type = find_memory_type(
            physical_device,
            requirements.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if (memory_type == UINT32_MAX) {
            VK_CHECK(VK_ERROR_FEATURE_NOT_PRESENT);
        }

        const VkMemoryAllocateInfo alloc_info{
            .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .allocationSize = requirements.size,
            .memoryTypeIndex = memory_type,
        };
        VK_CHECK(vkAllocateMemory(device_, &alloc_info, nullptr, &slot.memory));
        VK_CHECK(vkBindBufferMemory(device_, slot.frame_ubo, slot.memory, 0));

        slot.indirect_draw_buffer = slot.frame_ubo;
    }
}

void PerFrameGpuWriteRing::shutdown() {
    if (device_ == VK_NULL_HANDLE) {
        return;
    }

    for (PerFrameGpuWrites& slot : per_frame_) {
        if (slot.frame_ubo != VK_NULL_HANDLE) {
            vkDestroyBuffer(device_, slot.frame_ubo, nullptr);
            slot.frame_ubo = VK_NULL_HANDLE;
            slot.indirect_draw_buffer = VK_NULL_HANDLE;
        }
        if (slot.memory != VK_NULL_HANDLE) {
            vkFreeMemory(device_, slot.memory, nullptr);
            slot.memory = VK_NULL_HANDLE;
        }
    }

    device_ = VK_NULL_HANDLE;
}

std::uint32_t PerFrameGpuWriteRing::ring_index(std::uint64_t frame_index) const {
    return static_cast<std::uint32_t>(frame_index % frames_in_flight_);
}

PerFrameGpuWrites& PerFrameGpuWriteRing::slot(std::uint64_t frame_index) {
    return per_frame_[ring_index(frame_index)];
}

const PerFrameGpuWrites& PerFrameGpuWriteRing::slot(std::uint64_t frame_index) const {
    return per_frame_[ring_index(frame_index)];
}

std::size_t PerFrameGpuWriteRing::aligned_dynamic_offset(std::size_t offset) const {
    const std::size_t alignment = caps_.min_uniform_buffer_offset_alignment > 0
                                      ? caps_.min_uniform_buffer_offset_alignment
                                      : 1;
    return align_up(offset, alignment);
}

} // namespace engine
