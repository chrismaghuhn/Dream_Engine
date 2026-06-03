#include "engine/render/GpuMeshPool.hpp"

#include "engine/render/VkCheck.hpp"

#include <algorithm>

namespace engine {

namespace {

constexpr std::array<std::size_t, static_cast<std::size_t>(MeshBucket::Count)> kVertexCapacities{
    4 * 1024, 16 * 1024, 64 * 1024, 256 * 1024};
constexpr std::array<std::size_t, static_cast<std::size_t>(MeshBucket::Count)> kIndexCapacities{
    4 * 1024, 16 * 1024, 64 * 1024, 256 * 1024};

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

MeshBucket pick_mesh_bucket(const std::size_t vertex_bytes, const std::size_t index_bytes) {
    for (std::size_t bucket = 0; bucket < static_cast<std::size_t>(MeshBucket::Count); ++bucket) {
        if (vertex_bytes <= kVertexCapacities[bucket] && index_bytes <= kIndexCapacities[bucket]) {
            return static_cast<MeshBucket>(bucket);
        }
    }
    return MeshBucket::Count;
}

std::size_t mesh_bucket_vertex_capacity(const MeshBucket bucket) {
    return kVertexCapacities[static_cast<std::size_t>(bucket)];
}

std::size_t mesh_bucket_index_capacity(const MeshBucket bucket) {
    return kIndexCapacities[static_cast<std::size_t>(bucket)];
}

void GpuMeshPool::init(VkDevice device,
                       VkPhysicalDevice physical_device,
                       const std::size_t bytes_budget,
                       GpuDeferredFreeQueue* deferred_free) {
    device_ = device;
    physical_device_ = physical_device;
    bytes_budget_ = bytes_budget;
    deferred_free_ = deferred_free;
}

void GpuMeshPool::shutdown() {
    if (device_ == VK_NULL_HANDLE) {
        return;
    }

    for (GpuMeshSlot& slot : slots_) {
        if (!slot.live) {
            continue;
        }
        destroy_slot_resources(slot);
        slot.live = false;
    }

    slots_.clear();
    for (std::vector<std::uint32_t>& free_list : free_lists_) {
        free_list.clear();
    }

    bytes_used_ = 0;
    device_ = VK_NULL_HANDLE;
    physical_device_ = VK_NULL_HANDLE;
}

GpuMeshSlot* GpuMeshPool::find_slot(const std::uint32_t slot_id) {
    for (GpuMeshSlot& slot : slots_) {
        if (slot.live && slot.slot_id == slot_id) {
            return &slot;
        }
    }
    return nullptr;
}

const GpuMeshSlot* GpuMeshPool::find_slot(const std::uint32_t slot_id) const {
    for (const GpuMeshSlot& slot : slots_) {
        if (slot.live && slot.slot_id == slot_id) {
            return &slot;
        }
    }
    return nullptr;
}

void GpuMeshPool::destroy_slot_resources(GpuMeshSlot& slot) {
    if (device_ == VK_NULL_HANDLE) {
        return;
    }

    if (slot.vertex_buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device_, slot.vertex_buffer, nullptr);
        slot.vertex_buffer = VK_NULL_HANDLE;
    }
    if (slot.index_buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device_, slot.index_buffer, nullptr);
        slot.index_buffer = VK_NULL_HANDLE;
    }
    if (slot.memory != VK_NULL_HANDLE) {
        vkFreeMemory(device_, slot.memory, nullptr);
        slot.memory = VK_NULL_HANDLE;
    }

    if (slot.memory_size > 0) {
        bytes_used_ -= static_cast<std::size_t>(slot.memory_size);
        slot.memory_size = 0;
    }
}

void GpuMeshPool::return_slot_to_freelist(GpuMeshSlot& slot) {
    destroy_slot_resources(slot);
    free_lists_[static_cast<std::size_t>(slot.bucket)].push_back(slot.slot_id);
}

std::uint32_t GpuMeshPool::allocate_bucket(const MeshBucket bucket) {
    auto& free_list = free_lists_[static_cast<std::size_t>(bucket)];
    if (!free_list.empty()) {
        const std::uint32_t slot_id = free_list.back();
        free_list.pop_back();
        if (GpuMeshSlot* slot = find_slot(slot_id)) {
            return slot_id;
        }
    }

    const std::size_t vertex_capacity = mesh_bucket_vertex_capacity(bucket);
    const std::size_t index_capacity = mesh_bucket_index_capacity(bucket);

    const VkBufferCreateInfo vertex_info{
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = static_cast<VkDeviceSize>(vertex_capacity),
        .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    const VkBufferCreateInfo index_info{
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = static_cast<VkDeviceSize>(index_capacity),
        .usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };

    GpuMeshSlot slot{};
    slot.slot_id = next_slot_id_++;
    slot.bucket = bucket;
    slot.vertex_capacity = vertex_capacity;
    slot.index_capacity = index_capacity;
    slot.live = true;

    VK_CHECK(vkCreateBuffer(device_, &vertex_info, nullptr, &slot.vertex_buffer));
    VK_CHECK(vkCreateBuffer(device_, &index_info, nullptr, &slot.index_buffer));

    VkMemoryRequirements vertex_requirements{};
    VkMemoryRequirements index_requirements{};
    vkGetBufferMemoryRequirements(device_, slot.vertex_buffer, &vertex_requirements);
    vkGetBufferMemoryRequirements(device_, slot.index_buffer, &index_requirements);

    const VkDeviceSize alignment =
        std::max(vertex_requirements.alignment, index_requirements.alignment);
    const VkDeviceSize vertex_offset = 0;
    const VkDeviceSize index_offset =
        (vertex_requirements.size + alignment - 1) & ~(alignment - 1);
    const VkDeviceSize total_size = index_offset + index_requirements.size;

    if (bytes_used_ + static_cast<std::size_t>(total_size) > bytes_budget_) {
        vkDestroyBuffer(device_, slot.vertex_buffer, nullptr);
        vkDestroyBuffer(device_, slot.index_buffer, nullptr);
        return 0;
    }

    const std::uint32_t memory_type = find_memory_type(
        physical_device_,
        vertex_requirements.memoryTypeBits | index_requirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (memory_type == UINT32_MAX) {
        vkDestroyBuffer(device_, slot.vertex_buffer, nullptr);
        vkDestroyBuffer(device_, slot.index_buffer, nullptr);
        return 0;
    }

    const VkMemoryAllocateInfo alloc_info{
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = total_size,
        .memoryTypeIndex = memory_type,
    };
    VK_CHECK(vkAllocateMemory(device_, &alloc_info, nullptr, &slot.memory));
    VK_CHECK(vkBindBufferMemory(device_, slot.vertex_buffer, slot.memory, vertex_offset));
    VK_CHECK(vkBindBufferMemory(device_, slot.index_buffer, slot.memory, index_offset));

    slot.memory_size = total_size;
    bytes_used_ += static_cast<std::size_t>(total_size);
    slots_.push_back(slot);
    return slot.slot_id;
}

std::uint32_t GpuMeshPool::allocate(const std::size_t vertex_bytes, const std::size_t index_bytes) {
    if (device_ == VK_NULL_HANDLE) {
        return 0;
    }

    const MeshBucket bucket = pick_mesh_bucket(vertex_bytes, index_bytes);
    if (bucket == MeshBucket::Count) {
        return 0;
    }

    return allocate_bucket(bucket);
}

std::uint32_t GpuMeshPool::regrow(const std::uint32_t old_slot_id,
                                  const std::size_t vertex_bytes,
                                  const std::size_t index_bytes,
                                  const std::uint64_t last_used_frame) {
    if (deferred_free_ != nullptr && old_slot_id != 0) {
        deferred_free_->enqueue_free(old_slot_id, last_used_frame);
    } else if (GpuMeshSlot* old_slot = find_slot(old_slot_id)) {
        return_slot_to_freelist(*old_slot);
    }

    return allocate(vertex_bytes, index_bytes);
}

void GpuMeshPool::release_immediate(const std::uint32_t slot_id) {
    if (GpuMeshSlot* slot = find_slot(slot_id)) {
        return_slot_to_freelist(*slot);
    }
}

const GpuMeshSlot* GpuMeshPool::slot(const std::uint32_t slot_id) const {
    return find_slot(slot_id);
}

} // namespace engine
