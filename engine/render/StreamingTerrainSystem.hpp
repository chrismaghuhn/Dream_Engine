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

    void on_frame(const glm::vec3& focus_world,
                  std::uint32_t submit_snapshot_slot,
                  GpuMeshPool& mesh_pool,
                  MeshUploadQueue& upload_queue,
                  GpuDeferredFreeQueue& deferred_free);
    void mark_uploads_complete(const std::vector<MeshUploadFlushMark>& flushed_marks,
                               const GpuMeshPool& mesh_pool);
    void build_snapshot(WorldRenderSnapshot& snapshot,
                        const glm::vec3& render_origin,
                        ChunkStore& store,
                        const GpuMeshPool& mesh_pool);

    /// Refresh borders + schedule meshes for chunks already in the store (e.g. after save load).
    void bootstrap_existing_chunks(ChunkStore& store, const glm::vec3& focus_world);

    [[nodiscard]] std::size_t count_mesh_ready_sections() const;
    [[nodiscard]] std::size_t count_gpu_ready_sections() const;
    [[nodiscard]] int count_pending_mesh_jobs() const;

    /// Block until nearby section meshes are ready (startup / save load).
    void warmup_meshes_near_focus(JobSystem& jobs, const glm::vec3& focus_world, std::size_t min_sections);

    /// Drop cached meshes near focus so border data from newly loaded neighbors is picked up.
    void invalidate_meshes_near_focus(const glm::vec3& focus_world);

    /// Refresh 6-face border caches and remesh chunks touched by this frame's loads.
    void heal_seams_after_chunk_loads(const std::vector<ChunkCoord>& loaded_coords);

    /// Drop GPU slot references after device-lost recovery (CPU meshes kept).
    void reset_gpu_after_device_lost();

    /// Wait for mesh workers and apply completions (call after chunk load / streaming).
    void sync_mesh_workers();

private:
    struct SectionMeshState {
        std::uint8_t section_index = 0;
        std::uint32_t opaque_gpu_slot_id = 0;
        std::uint32_t water_gpu_slot_id = 0;
        std::uint32_t opaque_index_count = 0;
        std::uint32_t water_index_count = 0;
        std::uint32_t opaque_draw_index_count = 0;
        std::uint32_t water_draw_index_count = 0;
        bool mesh_ready = false;
        bool opaque_gpu_allocated = false;
        bool water_gpu_allocated = false;
        bool opaque_upload_queued = false;
        bool water_upload_queued = false;
        bool opaque_gpu_uploaded = false;
        bool water_gpu_uploaded = false;
        bool mesh_job_pending = false;
        // Set by soft_invalidate_chunk_mesh so that schedule_section_mesh
        // re-queues work without clearing the old GPU draw state.
        bool needs_remesh = false;
        std::uint32_t mesh_schedule_serial = 0;
        // Previous GPU slot kept live for rendering while the new mesh uploads.
        // Freed in mark_uploads_complete once the new slot is confirmed.
        std::uint32_t stale_opaque_gpu_slot_id = 0;
        std::uint32_t stale_opaque_draw_index_count = 0;
        std::uint32_t stale_water_gpu_slot_id = 0;
        std::uint32_t stale_water_draw_index_count = 0;
        std::vector<TerrainVertex> opaque_vertices;
        std::vector<std::uint32_t> opaque_indices;
        std::vector<TerrainVertex> water_vertices;
        std::vector<std::uint32_t> water_indices;
    };

    struct ChunkMeshState {
        ChunkCoord coord{};
        std::array<SectionMeshState, 8> sections{};
    };

    struct MeshCompletion {
        ChunkCoord coord{};
        std::uint8_t section_index = 0;
        std::uint32_t schedule_serial = 0;
        std::vector<TerrainVertex> opaque_vertices;
        std::vector<std::uint32_t> opaque_indices;
        std::vector<TerrainVertex> water_vertices;
        std::vector<std::uint32_t> water_indices;
    };

    void on_chunk_loaded(ChunkCoord coord);
    void on_chunk_unloaded(ChunkCoord coord);
    void schedule_chunk_mesh(ChunkCoord coord);
    void schedule_section_mesh(ChunkCoord coord, std::uint8_t section_index);
    void drain_mesh_completions();
    void ensure_gpu_slots(GpuMeshPool& mesh_pool, std::uint32_t submit_snapshot_slot);
    void invalidate_chunk_mesh(ChunkCoord coord);
    // Lighter alternative to invalidate_chunk_mesh: marks sections for remesh
    // but keeps the old GPU draw state intact so no visual gap occurs.
    void soft_invalidate_chunk_mesh(ChunkCoord coord);
    void reset_pending_upload_flags();
    void purge_stale_chunk_meshes();
    void queue_uploads(MeshUploadQueue& upload_queue, GpuMeshPool& mesh_pool);
    void sync_entity_mesh_slots(ChunkCoord coord, const ChunkMeshState& state);
    void process_mesh_backlog();
    void release_far_gpu_meshes(GpuMeshPool& mesh_pool,
                                GpuDeferredFreeQueue& deferred_free,
                                std::uint32_t submit_snapshot_slot);
    void release_section_gpu(SectionMeshState& section_state,
                             GpuDeferredFreeQueue& deferred_free,
                             std::uint32_t submit_snapshot_slot);

    static constexpr int kMaxPendingMeshJobs = 128;
    static constexpr int kMaxGpuAllocationsPerFrame = 24;
    static constexpr int kMaxUploadsPerFrame = 24;
    static constexpr int kMaxOpaqueDrawSections = 512;
    static constexpr int kMaxWaterDrawSections = 128;
    static constexpr int kMaxTotalIndirectDraws = 512;
    static constexpr int kMeshChunkRadius = 6;

    [[nodiscard]] bool chunk_within_mesh_radius(ChunkCoord coord) const;

    glm::vec3 focus_world_{0.f};
    flecs::world* world_ = nullptr;
    ChunkStore* store_ = nullptr;
    JobSystem* jobs_ = nullptr;
    WorldConfig world_config_{};

    std::unordered_map<ChunkCoord, ChunkMeshState, ChunkCoordHash> chunk_meshes_;
    std::mutex completion_mutex_;
    std::vector<MeshCompletion> completions_;
    std::vector<std::uint32_t> pending_slot_frees_;
    std::vector<DrawSection> culled_opaque_sections_;
    std::vector<DrawSection> culled_water_sections_;
};

} // namespace engine
