#pragma once

#include "engine/core/JobSystem.hpp"
#include "engine/render/GpuMeshPool.hpp"
#include "engine/render/MeshUploadQueue.hpp"
#include "engine/render/WorldRenderSnapshot.hpp"
#include "engine/world/ChunkStore.hpp"
#include "engine/world/WorldConfig.hpp"
#include "engine/world/WorldEvents.hpp"

#include <array>
#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <vector>

#include <flecs.h>
#include <glm/glm.hpp>

namespace engine {

class StreamingTerrainSystem {
public:
    void init(flecs::world& world, ChunkStore& store, JobSystem& jobs, const WorldConfig& world_config);
    void register_observers(flecs::world& world);

    void on_frame(std::uint64_t frame_index,
                  GpuMeshPool& mesh_pool,
                  MeshUploadQueue& upload_queue,
                  GpuDeferredFreeQueue& deferred_free);
    void build_snapshot(WorldRenderSnapshot& snapshot, const glm::vec3& render_origin, ChunkStore& store);

private:
    struct SectionMeshState {
        std::uint8_t section_index = 0;
        std::uint32_t gpu_slot_id = 0;
        std::uint32_t index_count = 0;
        bool mesh_ready = false;
        bool gpu_allocated = false;
        bool upload_queued = false;
        bool mesh_job_pending = false;
        std::vector<TerrainVertex> vertices;
        std::vector<std::uint32_t> indices;
    };

    struct ChunkMeshState {
        ChunkCoord coord{};
        std::array<SectionMeshState, 8> sections{};
    };

    struct MeshCompletion {
        ChunkCoord coord{};
        std::uint8_t section_index = 0;
        std::vector<TerrainVertex> vertices;
        std::vector<std::uint32_t> indices;
    };

    void on_chunk_loaded(ChunkCoord coord);
    void on_chunk_unloaded(ChunkCoord coord);
    void schedule_chunk_mesh(ChunkCoord coord);
    void schedule_section_mesh(ChunkCoord coord, std::uint8_t section_index);
    void drain_mesh_completions();
    void ensure_gpu_slots(GpuMeshPool& mesh_pool, std::uint64_t frame_index);
    void queue_uploads(MeshUploadQueue& upload_queue);
    void sync_entity_mesh_slots(ChunkCoord coord, const ChunkMeshState& state);

    flecs::world* world_ = nullptr;
    ChunkStore* store_ = nullptr;
    JobSystem* jobs_ = nullptr;
    WorldConfig world_config_{};

    std::unordered_map<ChunkCoord, ChunkMeshState, ChunkCoordHash> chunk_meshes_;
    std::mutex completion_mutex_;
    std::vector<MeshCompletion> completions_;
    std::vector<std::uint32_t> pending_slot_frees_;
    std::vector<DrawSection> culled_sections_;
};

} // namespace engine
