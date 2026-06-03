#pragma once

#include "engine/core/EngineConfig.hpp"

#include <cstddef>
#include <cstdint>

#include <volk.h>

namespace engine {

[[nodiscard]] bool memory_is_host_coherent(VkMemoryPropertyFlags properties);

void align_mapped_range(VkMappedMemoryRange& range, size_t non_coherent_atom_size);

void host_write(VkDevice device,
                void* mapped,
                size_t offset,
                size_t size,
                const void* src,
                VkDeviceMemory memory,
                VkMemoryPropertyFlags properties,
                const GpuCaps& caps);

} // namespace engine
