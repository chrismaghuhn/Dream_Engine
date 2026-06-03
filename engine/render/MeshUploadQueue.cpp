#include "engine/render/MeshUploadQueue.hpp"

#include "engine/render/HostMemory.hpp"
#include "engine/render/VkCheck.hpp"

#include <algorithm>
#include <cstring>

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

[[nodiscard]] std::size_t align_up(std::size_t value, std::size_t alignment) {
    if (alignment == 0) {
        return value;
    }
    return (value + alignment - 1) & ~(alignment - 1);
}

} // namespace

MeshUploadQueue::MeshUploadQueue(const std::uint32_t frames_in_flight, const std::size_t chunk_mesh_cpu_ram)
    : staging_ring_(frames_in_flight, chunk_mesh_cpu_ram)
    , frames_in_flight_(frames_in_flight > 0 ? frames_in_flight : 1)
    , staging_gpu_(frames_in_flight_) {}

void MeshUploadQueue::init(VkDevice device, VkPhysicalDevice physical_device, const GpuCaps& caps) {
    if (device_ != VK_NULL_HANDLE || device == VK_NULL_HANDLE) {
        return;
    }

    device_ = device;
    caps_ = caps;

    const std::size_t slot_size = staging_ring_.slot_size();
    for (StagingGpuSlot& slot : staging_gpu_) {
        const VkBufferCreateInfo buffer_info{
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size = static_cast<VkDeviceSize>(slot_size),
            .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        };
        VK_CHECK(vkCreateBuffer(device_, &buffer_info, nullptr, &slot.buffer));

        VkMemoryRequirements requirements{};
        vkGetBufferMemoryRequirements(device_, slot.buffer, &requirements);

        VkMemoryPropertyFlags properties =
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        std::uint32_t memory_type =
            find_memory_type(physical_device, requirements.memoryTypeBits, properties);
        if (memory_type == UINT32_MAX) {
            properties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
            memory_type = find_memory_type(physical_device, requirements.memoryTypeBits, properties);
        }
        if (memory_type == UINT32_MAX) {
            VK_CHECK(VK_ERROR_FEATURE_NOT_PRESENT);
        }

        const VkMemoryAllocateInfo alloc_info{
            .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .allocationSize = requirements.size,
            .memoryTypeIndex = memory_type,
        };
        VK_CHECK(vkAllocateMemory(device_, &alloc_info, nullptr, &slot.memory));
        VK_CHECK(vkBindBufferMemory(device_, slot.buffer, slot.memory, 0));
        VK_CHECK(vkMapMemory(device_, slot.memory, 0, requirements.size, 0, &slot.mapped));

        slot.capacity = requirements.size;
        slot.memory_properties = properties;
    }
}

void MeshUploadQueue::shutdown() {
    if (device_ == VK_NULL_HANDLE) {
        return;
    }

    for (StagingGpuSlot& slot : staging_gpu_) {
        if (slot.mapped != nullptr) {
            vkUnmapMemory(device_, slot.memory);
            slot.mapped = nullptr;
        }
        if (slot.buffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(device_, slot.buffer, nullptr);
            slot.buffer = VK_NULL_HANDLE;
        }
        if (slot.memory != VK_NULL_HANDLE) {
            vkFreeMemory(device_, slot.memory, nullptr);
            slot.memory = VK_NULL_HANDLE;
        }
    }

    pending_.clear();
    ready_copies_.clear();
    device_ = VK_NULL_HANDLE;
}

void MeshUploadQueue::enqueue(MeshUploadRequest request) {
    if (request.slot_id == 0 || request.vertices.empty() || request.indices.empty()) {
        return;
    }

    for (MeshUploadRequest& pending : pending_) {
        if (pending.slot_id == request.slot_id) {
            pending = std::move(request);
            return;
        }
    }

    pending_.push_back(std::move(request));
}

MeshUploadQueue::StagingGpuSlot& MeshUploadQueue::staging_slot(const std::uint64_t frame_index) {
    return staging_gpu_[static_cast<std::size_t>(frame_index % frames_in_flight_)];
}

void MeshUploadQueue::record_copy_barrier(VkCommandBuffer command_buffer,
                                          VkBuffer dst_buffer,
                                          const VkDeviceSize dst_offset,
                                          const VkDeviceSize size) const {
    const VkBufferMemoryBarrier barrier{
        .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_INDEX_READ_BIT,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .buffer = dst_buffer,
        .offset = dst_offset,
        .size = size,
    };

    vkCmdPipelineBarrier(command_buffer,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
                         0,
                         0,
                         nullptr,
                         1,
                         &barrier,
                         0,
                         nullptr);
}

void MeshUploadQueue::flush(VkCommandBuffer command_buffer,
                            const std::uint64_t frame_index,
                            GpuMeshPool& mesh_pool) {
    if (device_ == VK_NULL_HANDLE) {
        return;
    }

    last_flushed_marks_.clear();
    ready_copies_.clear();

    const std::uint32_t ring_index =
        static_cast<std::uint32_t>(frame_index % staging_ring_.frames_in_flight());
    staging_ring_.reset_slot(ring_index);
    StagingGpuSlot& staging = staging_slot(frame_index);
    std::size_t staging_cursor = 0;

    while (!pending_.empty()) {
        MeshUploadRequest request = std::move(pending_.front());
        pending_.pop_front();

        const std::size_t vertex_bytes = request.vertices.size() * sizeof(TerrainVertex);
        const std::size_t index_bytes = request.indices.size() * sizeof(std::uint32_t);
        const std::size_t upload_bytes = vertex_bytes + index_bytes;

        const std::size_t reserved =
            staging_ring_.try_enqueue(frame_index, StagingUpload{.id = request.slot_id, .size = upload_bytes});
        if (reserved == 0) {
            pending_.push_front(std::move(request));
            break;
        }

        const std::size_t staging_offset = staging_cursor;
        staging_cursor = align_up(staging_cursor + upload_bytes, 16);
        if (staging_cursor > staging.capacity) {
            pending_.push_front(std::move(request));
            staging_ring_.reset_slot(ring_index);
            break;
        }

        host_write(device_,
                   staging.mapped,
                   staging_offset,
                   vertex_bytes,
                   request.vertices.data(),
                   staging.memory,
                   staging.memory_properties,
                   caps_);
        host_write(device_,
                   staging.mapped,
                   staging_offset + vertex_bytes,
                   index_bytes,
                   request.indices.data(),
                   staging.memory,
                   staging.memory_properties,
                   caps_);

        const GpuMeshSlot* slot = mesh_pool.slot(request.slot_id);
        if (slot == nullptr) {
            continue;
        }

        if (vertex_bytes > slot->vertex_capacity || index_bytes > slot->index_capacity) {
            continue;
        }

        const VkDeviceSize vtx_copy = static_cast<VkDeviceSize>(vertex_bytes);
        const VkDeviceSize idx_copy = static_cast<VkDeviceSize>(index_bytes);

        const VkBufferCopy vertex_copy{
            .srcOffset = static_cast<VkDeviceSize>(staging_offset),
            .dstOffset = 0,
            .size = vtx_copy,
        };
        vkCmdCopyBuffer(command_buffer, staging.buffer, slot->vertex_buffer, 1, &vertex_copy);

        const VkBufferCopy index_copy{
            .srcOffset = static_cast<VkDeviceSize>(staging_offset + vertex_bytes),
            .dstOffset = 0,
            .size = idx_copy,
        };
        vkCmdCopyBuffer(command_buffer, staging.buffer, slot->index_buffer, 1, &index_copy);

        record_copy_barrier(command_buffer, slot->vertex_buffer, 0, vtx_copy);
        record_copy_barrier(command_buffer, slot->index_buffer, 0, idx_copy);

        last_flushed_marks_.push_back(MeshUploadFlushMark{
            .coord = request.coord,
            .section_index = request.section_index,
            .slot_id = request.slot_id,
            .water = request.water,
        });
        ready_copies_.push_back(PendingCopy{
            .slot_id = request.slot_id,
            .staging_offset = staging_offset,
            .vertex_bytes = static_cast<std::size_t>(vtx_copy),
            .index_bytes = static_cast<std::size_t>(idx_copy),
            .vertex_count = request.vertices.size(),
            .index_count = request.indices.size(),
        });
    }
}

} // namespace engine
