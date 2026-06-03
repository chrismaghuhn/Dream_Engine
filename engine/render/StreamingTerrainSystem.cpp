#include "engine/render/StreamingTerrainSystem.hpp"

#include "engine/render/FrustumCull.hpp"
#include "engine/render/GpuDeferredFreeQueue.hpp"
#include "engine/world/GreedyMesher.hpp"
#include "engine/world/SectionIndexing.hpp"

#include <spdlog/spdlog.h>

#include <algorithm>

namespace engine {

namespace {

[[nodiscard]] float section_center_distance(const DrawSection& section) {
    return glm::length(section.model_translation + glm::vec3(8.f));
}

} // namespace

void StreamingTerrainSystem::init(
    flecs::world& world, ChunkStore& store, JobSystem& jobs, const WorldConfig& world_config) {
    world_ = &world;
    store_ = &store;
    jobs_ = &jobs;
    world_config_ = world_config;
    chunk_meshes_.clear();
    completions_.clear();
    culled_opaque_sections_.clear();
    culled_water_sections_.clear();
}

void StreamingTerrainSystem::register_observers(flecs::world& world) {
    world.observer<EvtChunkLoaded>()
        .event<EvtChunkLoaded>()
        .each([this](flecs::iter&, size_t, EvtChunkLoaded& evt) { on_chunk_loaded(evt.coord); });

    world.observer<EvtChunkUnloaded>()
        .event<EvtChunkUnloaded>()
        .each([this](flecs::iter&, size_t, EvtChunkUnloaded& evt) { on_chunk_unloaded(evt.coord); });

    world.observer<ChunkDirty>()
        .event(flecs::OnAdd)
        .each([this](flecs::entity entity, ChunkDirty) {
            const ChunkCoord* coord = entity.get<ChunkCoord>();
            if (coord == nullptr) {
                return;
            }
            schedule_chunk_mesh(*coord);
        });
}

void StreamingTerrainSystem::on_chunk_loaded(const ChunkCoord coord) {
    if (store_->is_pending_unload(coord)) {
        return;
    }
    schedule_chunk_mesh(coord);
}

void StreamingTerrainSystem::on_chunk_unloaded(const ChunkCoord coord) {
    chunk_meshes_.erase(coord);
}

void StreamingTerrainSystem::schedule_chunk_mesh(const ChunkCoord coord) {
    for (std::uint8_t section_index = 0; section_index < 8; ++section_index) {
        schedule_section_mesh(coord, section_index);
    }
}

int StreamingTerrainSystem::count_pending_mesh_jobs() const {
    int pending = 0;
    for (const auto& [coord, chunk_state] : chunk_meshes_) {
        (void)coord;
        for (const SectionMeshState& section_state : chunk_state.sections) {
            if (section_state.mesh_job_pending) {
                ++pending;
            }
        }
    }
    return pending;
}

void StreamingTerrainSystem::schedule_section_mesh(const ChunkCoord coord, const std::uint8_t section_index) {
    if (store_ == nullptr || jobs_ == nullptr) {
        return;
    }

    if (count_pending_mesh_jobs() >= kMaxPendingMeshJobs) {
        return;
    }

    const Chunk* chunk = store_->try_get(coord);
    if (chunk == nullptr || store_->is_pending_unload(coord)) {
        return;
    }

    ChunkMeshState& chunk_state = chunk_meshes_[coord];
    chunk_state.coord = coord;
    SectionMeshState& section_state = chunk_state.sections[section_index];
    if (section_state.mesh_job_pending) {
        return;
    }

    section_state.section_index = section_index;
    section_state.mesh_job_pending = true;
    const Section section_copy = chunk->sections[section_index];

    jobs_->run_meshing([this, coord, section_index, section_copy]() {
        MeshCompletion completion{
            .coord = coord,
            .section_index = section_index,
        };
        mesh_section(section_copy,
                     completion.opaque_vertices,
                     completion.opaque_indices,
                     completion.water_vertices,
                     completion.water_indices);

        std::lock_guard lock(completion_mutex_);
        completions_.push_back(std::move(completion));
    });
}

void StreamingTerrainSystem::drain_mesh_completions() {
    std::vector<MeshCompletion> local_completions;
    {
        std::lock_guard lock(completion_mutex_);
        local_completions.swap(completions_);
    }

    for (MeshCompletion& completion : local_completions) {
        if (store_->try_get(completion.coord) == nullptr || store_->is_pending_unload(completion.coord)) {
            continue;
        }

        ChunkMeshState& chunk_state = chunk_meshes_[completion.coord];
        chunk_state.coord = completion.coord;
        SectionMeshState& section_state = chunk_state.sections[completion.section_index];
        section_state.section_index = completion.section_index;
        section_state.opaque_vertices = std::move(completion.opaque_vertices);
        section_state.opaque_indices = std::move(completion.opaque_indices);
        section_state.water_vertices = std::move(completion.water_vertices);
        section_state.water_indices = std::move(completion.water_indices);
        section_state.opaque_index_count = static_cast<std::uint32_t>(section_state.opaque_indices.size());
        section_state.water_index_count = static_cast<std::uint32_t>(section_state.water_indices.size());
        section_state.mesh_ready = true;
        section_state.mesh_job_pending = false;
        section_state.opaque_gpu_allocated = false;
        section_state.water_gpu_allocated = false;
        section_state.opaque_upload_queued = false;
        section_state.water_upload_queued = false;
        if (section_state.opaque_gpu_slot_id != 0) {
            pending_slot_frees_.push_back(section_state.opaque_gpu_slot_id);
            section_state.opaque_gpu_slot_id = 0;
        }
        if (section_state.water_gpu_slot_id != 0) {
            pending_slot_frees_.push_back(section_state.water_gpu_slot_id);
            section_state.water_gpu_slot_id = 0;
        }
    }
}

void StreamingTerrainSystem::ensure_gpu_slots(GpuMeshPool& mesh_pool, const std::uint64_t frame_index) {
    (void)frame_index;
    for (auto& [coord, chunk_state] : chunk_meshes_) {
        if (store_->try_get(coord) == nullptr || store_->is_pending_unload(coord)) {
            continue;
        }

        bool slots_changed = false;
        for (SectionMeshState& section_state : chunk_state.sections) {
            if (!section_state.mesh_ready) {
                continue;
            }

            if (!section_state.opaque_gpu_allocated && !section_state.opaque_vertices.empty() &&
                !section_state.opaque_indices.empty()) {
                const std::size_t vertex_bytes = section_state.opaque_vertices.size() * sizeof(TerrainVertex);
                const std::size_t index_bytes = section_state.opaque_indices.size() * sizeof(std::uint32_t);
                const std::uint32_t slot_id = mesh_pool.allocate(vertex_bytes, index_bytes);
                if (slot_id != 0) {
                    section_state.opaque_gpu_slot_id = slot_id;
                    section_state.opaque_gpu_allocated = true;
                    slots_changed = true;
                }
            }

            if (!section_state.water_gpu_allocated && !section_state.water_vertices.empty() &&
                !section_state.water_indices.empty()) {
                const std::size_t vertex_bytes = section_state.water_vertices.size() * sizeof(TerrainVertex);
                const std::size_t index_bytes = section_state.water_indices.size() * sizeof(std::uint32_t);
                const std::uint32_t slot_id = mesh_pool.allocate(vertex_bytes, index_bytes);
                if (slot_id != 0) {
                    section_state.water_gpu_slot_id = slot_id;
                    section_state.water_gpu_allocated = true;
                    slots_changed = true;
                }
            }
        }

        if (slots_changed) {
            sync_entity_mesh_slots(coord, chunk_state);
        }
    }
}

void StreamingTerrainSystem::queue_uploads(MeshUploadQueue& upload_queue) {
    for (auto& [coord, chunk_state] : chunk_meshes_) {
        if (store_->try_get(coord) == nullptr || store_->is_pending_unload(coord)) {
            continue;
        }

        for (SectionMeshState& section_state : chunk_state.sections) {
            if (section_state.opaque_gpu_allocated && !section_state.opaque_upload_queued &&
                section_state.opaque_gpu_slot_id != 0 && !section_state.opaque_vertices.empty()) {
                upload_queue.enqueue(MeshUploadRequest{
                    .slot_id = section_state.opaque_gpu_slot_id,
                    .vertices = section_state.opaque_vertices,
                    .indices = section_state.opaque_indices,
                });
                section_state.opaque_upload_queued = true;
            }

            if (section_state.water_gpu_allocated && !section_state.water_upload_queued &&
                section_state.water_gpu_slot_id != 0 && !section_state.water_vertices.empty()) {
                upload_queue.enqueue(MeshUploadRequest{
                    .slot_id = section_state.water_gpu_slot_id,
                    .vertices = section_state.water_vertices,
                    .indices = section_state.water_indices,
                });
                section_state.water_upload_queued = true;
            }
        }
    }
}

void StreamingTerrainSystem::sync_entity_mesh_slots(const ChunkCoord coord, const ChunkMeshState& state) {
    if (world_ == nullptr || store_ == nullptr) {
        return;
    }

    const std::uint64_t entity_id = store_->entity_for(coord);
    if (entity_id == 0) {
        return;
    }

    flecs::entity entity(*world_, entity_id);
    if (!entity.is_alive()) {
        return;
    }

    ChunkMeshSlots slots{};
    for (const SectionMeshState& section_state : state.sections) {
        if (section_state.opaque_gpu_slot_id != 0) {
            slots.section_slot_ids[section_state.section_index] = section_state.opaque_gpu_slot_id;
        }
    }
    entity.set<ChunkMeshSlots>(slots);
}

void StreamingTerrainSystem::process_mesh_backlog() {
    if (store_ == nullptr || jobs_ == nullptr) {
        return;
    }

    store_->for_each_loaded([&](const ChunkCoord coord) {
        if (count_pending_mesh_jobs() >= kMaxPendingMeshJobs) {
            return;
        }
        if (store_->is_pending_unload(coord)) {
            return;
        }

        ChunkMeshState& chunk_state = chunk_meshes_[coord];
        chunk_state.coord = coord;
        for (std::uint8_t section_index = 0; section_index < 8; ++section_index) {
            if (count_pending_mesh_jobs() >= kMaxPendingMeshJobs) {
                return;
            }
            SectionMeshState& section_state = chunk_state.sections[section_index];
            if (!section_state.mesh_ready && !section_state.mesh_job_pending) {
                schedule_section_mesh(coord, section_index);
            }
        }
    });
}

void StreamingTerrainSystem::on_frame(const std::uint64_t frame_index,
                                      GpuMeshPool& mesh_pool,
                                      MeshUploadQueue& upload_queue,
                                      GpuDeferredFreeQueue& deferred_free) {
    drain_mesh_completions();
    process_mesh_backlog();
    for (const std::uint32_t slot_id : pending_slot_frees_) {
        deferred_free.enqueue_free(slot_id, frame_index);
    }
    pending_slot_frees_.clear();
    ensure_gpu_slots(mesh_pool, frame_index);
    queue_uploads(upload_queue);
}

void StreamingTerrainSystem::build_snapshot(
    WorldRenderSnapshot& snapshot, const glm::vec3& render_origin, ChunkStore& store) {
    snapshot.opaque_sections.clear();
    snapshot.water_sections.clear();
    culled_opaque_sections_.clear();
    culled_water_sections_.clear();

    const glm::mat4 view_proj = snapshot.proj * snapshot.view;
    const std::array<glm::vec4, 6> frustum_planes = frustum_planes_from_matrix(view_proj);

    std::uint32_t opaque_indirect_index = 0;
    std::uint32_t water_indirect_index = 0;

    store.for_each_loaded([&](const ChunkCoord coord) {
        if (store.is_pending_unload(coord)) {
            return;
        }

        const auto chunk_it = chunk_meshes_.find(coord);
        if (chunk_it == chunk_meshes_.end()) {
            return;
        }

        const ChunkMeshState& chunk_state = chunk_it->second;
        const glm::vec3 chunk_origin_world = glm::vec3(coord) * 32.f;

        for (const SectionMeshState& section_state : chunk_state.sections) {
            const glm::ivec3 section_coord = section_coord_from_index(section_state.section_index);
            const glm::vec3 section_offset = glm::vec3(section_coord) * 16.f;
            const glm::vec3 model_translation = chunk_origin_world + section_offset - render_origin;
            const glm::vec3 cull_min = model_translation;
            const glm::vec3 cull_max = model_translation + glm::vec3(16.f);

            if (!aabb_intersects_frustum(frustum_planes, cull_min, cull_max)) {
                continue;
            }

            if (section_state.mesh_ready && section_state.opaque_gpu_slot_id != 0 &&
                section_state.opaque_index_count > 0) {
                culled_opaque_sections_.push_back(DrawSection{
                    .coord = coord,
                    .section_index = section_state.section_index,
                    .model_translation = model_translation,
                    .indirect_index = opaque_indirect_index++,
                    .vertex_buffer_id = section_state.opaque_gpu_slot_id,
                    .index_buffer_id = section_state.opaque_gpu_slot_id,
                    .index_count = section_state.opaque_index_count,
                    .cull_min = cull_min,
                    .cull_max = cull_max,
                });
            }

            if (section_state.mesh_ready && section_state.water_gpu_slot_id != 0 &&
                section_state.water_index_count > 0) {
                culled_water_sections_.push_back(DrawSection{
                    .coord = coord,
                    .section_index = section_state.section_index,
                    .model_translation = model_translation,
                    .indirect_index = water_indirect_index++,
                    .vertex_buffer_id = section_state.water_gpu_slot_id,
                    .index_buffer_id = section_state.water_gpu_slot_id,
                    .index_count = section_state.water_index_count,
                    .cull_min = cull_min,
                    .cull_max = cull_max,
                });
            }
        }
    });

    std::sort(culled_water_sections_.begin(),
              culled_water_sections_.end(),
              [](const DrawSection& a, const DrawSection& b) {
                  return section_center_distance(a) > section_center_distance(b);
              });

    snapshot.opaque_sections.swap(culled_opaque_sections_);
    snapshot.water_sections.swap(culled_water_sections_);
}

} // namespace engine
