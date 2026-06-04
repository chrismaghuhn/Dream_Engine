#pragma once

#include "engine/core/EngineConfig.hpp"
#include "engine/render/GpuMeshPool.hpp"
#include "engine/render/StagingRing.hpp"
#include "engine/world/ChunkStore.hpp"
#include "engine/world/SectionIndexing.hpp"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <vector>

#include <volk.h>

namespace engine {

struct MeshUploadFlushMark {
    ChunkCoord coord{};
    std::uint8_t section_index = 0;
    std::uint32_t slot_id = 0;
    bool water = false;
    bool lod1 = false;
};

struct MeshUploadRequest {
    ChunkCoord coord{};
    std::uint8_t section_index = 0;
    std::uint32_t slot_id = 0;
    bool water = false;
    bool lod1 = false;
    std::vector<TerrainVertex> vertices;
    std::vector<std::uint32_t> indices;
};

class MeshUploadQueue {
public:
    MeshUploadQueue(std::uint32_t frames_in_flight, std::size_t chunk_mesh_cpu_ram);

    void init(VkDevice device, VkPhysicalDevice physical_device, const GpuCaps& caps);
    void shutdown();

    void enqueue(MeshUploadRequest request);
    void flush(VkCommandBuffer command_buffer, std::uint64_t frame_index, GpuMeshPool& mesh_pool);

    [[nodiscard]] const std::vector<MeshUploadFlushMark>& last_flushed_marks() const {
        return last_flushed_marks_;
    }
    [[nodiscard]] bool has_pending() const { return !pending_.empty(); }
    [[nodiscard]] std::size_t pending_count() const { return pending_.size(); }
    [[nodiscard]] const StagingRing& staging_ring() const { return staging_ring_; }

private:
    struct StagingGpuSlot {
        VkBuffer buffer = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        void* mapped = nullptr;
        VkDeviceSize capacity = 0;
        VkMemoryPropertyFlags memory_properties = 0;
    };

    struct PendingCopy {
        std::uint32_t slot_id = 0;
        std::size_t staging_offset = 0;
        std::size_t vertex_bytes = 0;
        std::size_t index_bytes = 0;
        std::size_t vertex_count = 0;
        std::size_t index_count = 0;
    };

    [[nodiscard]] StagingGpuSlot& staging_slot(std::uint64_t frame_index);
    void record_copy_barrier(VkCommandBuffer command_buffer,
                             VkBuffer dst_buffer,
                             VkDeviceSize dst_offset,
                             VkDeviceSize size) const;

    StagingRing staging_ring_;
    GpuCaps caps_{};
    std::uint32_t frames_in_flight_ = 0;
    VkDevice device_ = VK_NULL_HANDLE;
    std::vector<StagingGpuSlot> staging_gpu_;
    std::deque<MeshUploadRequest> pending_;
    std::deque<PendingCopy> ready_copies_;
    std::vector<MeshUploadFlushMark> last_flushed_marks_;
};

} // namespace engine
