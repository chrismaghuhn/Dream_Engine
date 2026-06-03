#include "engine/render/HostMemory.hpp"

#include <volk.h>

#include <cstring>

namespace engine {

namespace {

[[nodiscard]] size_t align_down(size_t value, size_t alignment) {
    return value & ~(alignment - 1);
}

[[nodiscard]] size_t align_up(size_t value, size_t alignment) {
    return (value + alignment - 1) & ~(alignment - 1);
}

} // namespace

bool memory_is_host_coherent(VkMemoryPropertyFlags properties) {
    return (properties & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0;
}

void align_mapped_range(VkMappedMemoryRange& range, size_t non_coherent_atom_size) {
    if (non_coherent_atom_size == 0) {
        return;
    }

    const size_t start = range.offset;
    const size_t end = range.offset + range.size;
    range.offset = align_down(start, non_coherent_atom_size);
    range.size = align_up(end, non_coherent_atom_size) - range.offset;
}

void host_write(VkDevice device,
                void* mapped,
                size_t offset,
                size_t size,
                const void* src,
                VkDeviceMemory memory,
                VkMemoryPropertyFlags properties,
                const GpuCaps& caps) {
    std::memcpy(static_cast<char*>(mapped) + offset, src, size);

    if (memory_is_host_coherent(properties)) {
        return;
    }

    VkMappedMemoryRange range{
        .sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
        .memory = memory,
        .offset = offset,
        .size = size,
    };
    align_mapped_range(range, caps.non_coherent_atom_size);

    if (device != VK_NULL_HANDLE) {
        vkFlushMappedMemoryRanges(device, 1, &range);
    }
}

} // namespace engine
