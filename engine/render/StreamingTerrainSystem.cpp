#include "engine/render/StreamingTerrainSystem.hpp"

#include "engine/render/FrustumCull.hpp"
#include "engine/render/GpuDeferredFreeQueue.hpp"
#include "engine/world/BlockLight.hpp"
#include "engine/world/ChunkLifecycle.hpp"
#include "engine/world/GreedyMesher.hpp"
#include "engine/world/SectionIndexing.hpp"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <array>

namespace engine {

namespace {

[[nodiscard]] float section_center_distance(const DrawSection& section) {
    return glm::length(section.model_translation + glm::vec3(8.f));
}

[[nodiscard]] float chunk_distance_sq(ChunkCoord coord, const glm::vec3& focus_world) {
    const glm::vec3 chunk_center = glm::vec3(coord) * 32.f + glm::vec3(16.f);
    const glm::vec3 delta = chunk_center - focus_world;
    return glm::dot(delta, delta);
}

} // namespace

void StreamingTerrainSystem::mark_uploads_complete(const std::vector<MeshUploadFlushMark>& flushed_marks,
                                                 const GpuMeshPool& mesh_pool) {
    for (const MeshUploadFlushMark& mark : flushed_marks) {
        if (mark.slot_id == 0 || mark.section_index >= 8) {
            continue;
        }

        const auto chunk_it = chunk_meshes_.find(mark.coord);
        if (chunk_it == chunk_meshes_.end()) {
            continue;
        }

        SectionMeshState& section_state = chunk_it->second.sections[mark.section_index];
        if (!mark.water) {
            if (section_state.opaque_gpu_slot_id != mark.slot_id) {
                continue;
            }
            const GpuMeshSlot* slot = mesh_pool.slot(mark.slot_id);
            if (slot == nullptr || !mesh_pool.is_live(mark.slot_id)) {
                continue;
            }
            section_state.opaque_draw_index_count =
                clamp_index_count(slot, section_state.opaque_index_count);
            section_state.opaque_gpu_uploaded = section_state.opaque_draw_index_count > 0;
            // New mesh is confirmed on GPU; release the stale slot held for seamless rendering.
            if (section_state.stale_opaque_gpu_slot_id != 0) {
                pending_slot_frees_.push_back(section_state.stale_opaque_gpu_slot_id);
                section_state.stale_opaque_gpu_slot_id = 0;
                section_state.stale_opaque_draw_index_count = 0;
            }
            continue;
        }

        if (section_state.water_gpu_slot_id != mark.slot_id) {
            continue;
        }
        const GpuMeshSlot* slot = mesh_pool.slot(mark.slot_id);
        if (slot == nullptr || !mesh_pool.is_live(mark.slot_id)) {
            continue;
        }
        section_state.water_draw_index_count =
            clamp_index_count(slot, section_state.water_index_count);
        section_state.water_gpu_uploaded = section_state.water_draw_index_count > 0;
        if (section_state.stale_water_gpu_slot_id != 0) {
            pending_slot_frees_.push_back(section_state.stale_water_gpu_slot_id);
            section_state.stale_water_gpu_slot_id = 0;
            section_state.stale_water_draw_index_count = 0;
        }
    }
}

void StreamingTerrainSystem::release_section_gpu(SectionMeshState& section_state,
                                                 GpuDeferredFreeQueue& deferred_free,
                                                 const std::uint32_t submit_snapshot_slot) {
    if (section_state.stale_opaque_gpu_slot_id != 0) {
        deferred_free.enqueue_free(section_state.stale_opaque_gpu_slot_id, submit_snapshot_slot);
        section_state.stale_opaque_gpu_slot_id = 0;
        section_state.stale_opaque_draw_index_count = 0;
    }
    if (section_state.opaque_gpu_slot_id != 0) {
        deferred_free.enqueue_free(section_state.opaque_gpu_slot_id, submit_snapshot_slot);
        section_state.opaque_gpu_slot_id = 0;
        section_state.opaque_gpu_allocated = false;
        section_state.opaque_upload_queued = false;
        section_state.opaque_gpu_uploaded = false;
        section_state.opaque_draw_index_count = 0;
    }
    if (section_state.stale_water_gpu_slot_id != 0) {
        deferred_free.enqueue_free(section_state.stale_water_gpu_slot_id, submit_snapshot_slot);
        section_state.stale_water_gpu_slot_id = 0;
        section_state.stale_water_draw_index_count = 0;
    }
    if (section_state.water_gpu_slot_id != 0) {
        deferred_free.enqueue_free(section_state.water_gpu_slot_id, submit_snapshot_slot);
        section_state.water_gpu_slot_id = 0;
        section_state.water_gpu_allocated = false;
        section_state.water_upload_queued = false;
        section_state.water_gpu_uploaded = false;
        section_state.water_draw_index_count = 0;
    }
}

void StreamingTerrainSystem::release_far_gpu_meshes(GpuMeshPool& mesh_pool,
                                                    GpuDeferredFreeQueue& deferred_free,
                                                    const std::uint32_t submit_snapshot_slot) {
    (void)mesh_pool;
    for (auto& [coord, chunk_state] : chunk_meshes_) {
        if (chunk_within_mesh_radius(coord)) {
            continue;
        }
        for (SectionMeshState& section_state : chunk_state.sections) {
            release_section_gpu(section_state, deferred_free, submit_snapshot_slot);
        }
    }
}

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
    world.observer()
        .event<EvtChunkLoaded>()
        .with<ChunkCoord>()
        .run([this](flecs::iter& it) {
            while (it.next()) {
                const EvtChunkLoaded* evt = it.param<EvtChunkLoaded>();
                if (evt != nullptr) {
                    on_chunk_loaded(evt->coord);
                }
            }
        });

    world.observer()
        .event<EvtChunkUnloaded>()
        .with<ChunkCoord>()
        .run([this](flecs::iter& it) {
            while (it.next()) {
                const EvtChunkUnloaded* evt = it.param<EvtChunkUnloaded>();
                if (evt != nullptr) {
                    on_chunk_unloaded(evt->coord);
                }
            }
        });

    world.observer<ChunkDirty>()
        .event(flecs::OnAdd)
        .each([this](flecs::entity entity, ChunkDirty) {
            const ChunkCoord* coord = entity.get<ChunkCoord>();
            if (coord == nullptr) {
                return;
            }
            // Mark sections for remesh without clearing GPU draw state, so the
            // old mesh stays visible until the new one is uploaded (no flash).
            soft_invalidate_chunk_mesh(*coord);
            schedule_chunk_mesh(*coord);
            // Remove the tag so subsequent block mutations in the same chunk
            // trigger OnAdd again and re-enter this path.
            entity.remove<ChunkDirty>();
        });
}

bool StreamingTerrainSystem::chunk_within_mesh_radius(const ChunkCoord coord) const {
    constexpr float kRadiusBlocks = static_cast<float>(kMeshChunkRadius * 32);
    return chunk_distance_sq(coord, focus_world_) <= kRadiusBlocks * kRadiusBlocks;
}

void StreamingTerrainSystem::invalidate_chunk_mesh(const ChunkCoord coord) {
    ChunkMeshState& chunk_state = chunk_meshes_[coord];
    chunk_state.coord = coord;

    for (SectionMeshState& section_state : chunk_state.sections) {
        ++section_state.mesh_schedule_serial;
        section_state.mesh_ready = false;
        section_state.mesh_job_pending = false;
        section_state.needs_remesh = false;
        section_state.opaque_gpu_allocated = false;
        section_state.water_gpu_allocated = false;
        section_state.opaque_upload_queued = false;
        section_state.water_upload_queued = false;
        section_state.opaque_gpu_uploaded = false;
        section_state.water_gpu_uploaded = false;
        section_state.opaque_draw_index_count = 0;
        section_state.water_draw_index_count = 0;

        if (section_state.stale_opaque_gpu_slot_id != 0) {
            pending_slot_frees_.push_back(section_state.stale_opaque_gpu_slot_id);
            section_state.stale_opaque_gpu_slot_id = 0;
            section_state.stale_opaque_draw_index_count = 0;
        }
        if (section_state.opaque_gpu_slot_id != 0) {
            pending_slot_frees_.push_back(section_state.opaque_gpu_slot_id);
            section_state.opaque_gpu_slot_id = 0;
        }
        if (section_state.stale_water_gpu_slot_id != 0) {
            pending_slot_frees_.push_back(section_state.stale_water_gpu_slot_id);
            section_state.stale_water_gpu_slot_id = 0;
            section_state.stale_water_draw_index_count = 0;
        }
        if (section_state.water_gpu_slot_id != 0) {
            pending_slot_frees_.push_back(section_state.water_gpu_slot_id);
            section_state.water_gpu_slot_id = 0;
        }
    }
}

void StreamingTerrainSystem::soft_invalidate_chunk_mesh(const ChunkCoord coord) {
    ChunkMeshState& chunk_state = chunk_meshes_[coord];
    chunk_state.coord = coord;

    for (SectionMeshState& section_state : chunk_state.sections) {
        ++section_state.mesh_schedule_serial;
        section_state.mesh_job_pending = false;
        section_state.needs_remesh = true;
        // Only supersede an existing stale slot when the active slot has already
        // been confirmed on the GPU (opaque_draw_index_count > 0).  If the active
        // slot's upload is still in flight, keeping the old confirmed stale is
        // safer than discarding it — discarding it would leave the section with no
        // fallback if the new mesh job also completes before the upload is confirmed.
        if (section_state.stale_opaque_gpu_slot_id != 0 &&
            section_state.opaque_draw_index_count > 0) {
            pending_slot_frees_.push_back(section_state.stale_opaque_gpu_slot_id);
            section_state.stale_opaque_gpu_slot_id = 0;
            section_state.stale_opaque_draw_index_count = 0;
        }
        if (section_state.stale_water_gpu_slot_id != 0 &&
            section_state.water_draw_index_count > 0) {
            pending_slot_frees_.push_back(section_state.stale_water_gpu_slot_id);
            section_state.stale_water_gpu_slot_id = 0;
            section_state.stale_water_draw_index_count = 0;
        }
        // Intentionally leave mesh_ready, opaque_gpu_*, opaque_draw_index_count,
        // and the active GPU slot ids intact so the old mesh keeps rendering until
        // drain_mesh_completions replaces it with the freshly built one.
    }
}

void StreamingTerrainSystem::on_chunk_loaded(const ChunkCoord coord) {
    if (store_->is_pending_unload(coord)) {
        return;
    }

    if (store_ != nullptr) {
        refresh_chunk_section_borders(*store_, coord);
        refresh_loaded_chunk_neighbors(*store_, coord);
    }

    static constexpr std::array<ChunkCoord, 6> kNeighborOffsets{
        ChunkCoord{1, 0, 0},
        ChunkCoord{-1, 0, 0},
        ChunkCoord{0, 1, 0},
        ChunkCoord{0, -1, 0},
        ChunkCoord{0, 0, 1},
        ChunkCoord{0, 0, -1},
    };

    if (chunk_within_mesh_radius(coord)) {
        schedule_chunk_mesh(coord);
    }

    for (const ChunkCoord& offset : kNeighborOffsets) {
        const ChunkCoord neighbor = coord + offset;
        if (store_->try_get(neighbor) == nullptr || store_->is_pending_unload(neighbor)) {
            continue;
        }
        if (!chunk_within_mesh_radius(neighbor)) {
            continue;
        }

        // Soft invalidate so the neighbor's current mesh stays visible while
        // the border-healed mesh is being built.
        soft_invalidate_chunk_mesh(neighbor);
        schedule_chunk_mesh(neighbor);
    }
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
    // mesh_ready stays true during a soft invalidation (needs_remesh) so the
    // old GPU geometry keeps rendering. Allow rescheduling in that case.
    if (section_state.mesh_ready && !section_state.needs_remesh) {
        return;
    }

    section_state.section_index = section_index;
    section_state.mesh_job_pending = true;
    section_state.needs_remesh = false;
    const std::uint32_t schedule_serial = section_state.mesh_schedule_serial;

    const glm::ivec3 section_coord = section_coord_from_index(section_index);
    refresh_section_border_cache(*store_, coord, section_coord);
    const Section section_snapshot = chunk->sections[section_index];

    // The worker operates ONLY on its captured `section_snapshot` copy (including
    // the border cache refreshed above) and the mutex-guarded completion queue.
    // It must NOT touch `store_`: ChunkStore is not thread-safe, and the main
    // thread mutates it in update_streaming without a meshing barrier. Stale
    // completions for chunks that were unloaded meanwhile are dropped on the
    // main thread in drain_mesh_completions().
    jobs_->run_meshing([this, coord, section_index, schedule_serial, section_snapshot]() {
        MeshCompletion completion{
            .coord = coord,
            .section_index = section_index,
            .schedule_serial = schedule_serial,
        };
        mesh_section(section_snapshot,
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
        if (section_state.mesh_schedule_serial != completion.schedule_serial) {
            section_state.mesh_job_pending = false;
            continue;
        }

        section_state.section_index = completion.section_index;
        section_state.opaque_vertices = std::move(completion.opaque_vertices);
        section_state.opaque_indices = std::move(completion.opaque_indices);
        section_state.water_vertices = std::move(completion.water_vertices);
        section_state.water_indices = std::move(completion.water_indices);
        section_state.opaque_index_count = static_cast<std::uint32_t>(section_state.opaque_indices.size());
        section_state.water_index_count = static_cast<std::uint32_t>(section_state.water_indices.size());
        section_state.mesh_ready = true;
        section_state.mesh_job_pending = false;
        section_state.needs_remesh = false;
        // Promote the active GPU slot to "stale" so build_snapshot can keep
        // drawing the previous geometry while the new upload is in flight.
        //
        // IMPORTANT: only promote the active slot when its upload has been
        // confirmed (opaque_draw_index_count > 0).  If the upload is still in
        // flight (draw_count == 0) the active slot contains no drawable data yet.
        // In that case, keep the existing stale slot (which is confirmed) and
        // discard the unconfirmed active slot instead.  This prevents a one-frame
        // gap when a block is broken while a seam-heal upload is still in flight.
        if (section_state.opaque_gpu_slot_id != 0) {
            const std::uint32_t draw_count = section_state.opaque_draw_index_count;
            if (draw_count > 0) {
                // Active slot has confirmed GPU data — use it as the new stale.
                if (section_state.stale_opaque_gpu_slot_id != 0) {
                    pending_slot_frees_.push_back(section_state.stale_opaque_gpu_slot_id);
                }
                section_state.stale_opaque_gpu_slot_id = section_state.opaque_gpu_slot_id;
                section_state.stale_opaque_draw_index_count = draw_count;
                section_state.opaque_gpu_slot_id = 0;
            } else if (section_state.stale_opaque_gpu_slot_id != 0) {
                // Active slot not yet confirmed but an old confirmed stale exists:
                // keep the stale, free the unconfirmed active slot.
                pending_slot_frees_.push_back(section_state.opaque_gpu_slot_id);
                section_state.opaque_gpu_slot_id = 0;
            } else {
                // No confirmed data anywhere — free the active slot and accept
                // that the section will be invisible until the new mesh uploads.
                pending_slot_frees_.push_back(section_state.opaque_gpu_slot_id);
                section_state.opaque_gpu_slot_id = 0;
            }
        } else {
            // No active replacement slot exists yet. Keep any confirmed stale
            // slot drawing until ensure_gpu_slots() allocates and uploads the
            // new mesh; freeing it here creates a visible one-frame gap.
        }
        if (section_state.water_gpu_slot_id != 0) {
            const std::uint32_t draw_count = section_state.water_draw_index_count;
            if (draw_count > 0) {
                if (section_state.stale_water_gpu_slot_id != 0) {
                    pending_slot_frees_.push_back(section_state.stale_water_gpu_slot_id);
                }
                section_state.stale_water_gpu_slot_id = section_state.water_gpu_slot_id;
                section_state.stale_water_draw_index_count = draw_count;
                section_state.water_gpu_slot_id = 0;
            } else if (section_state.stale_water_gpu_slot_id != 0) {
                pending_slot_frees_.push_back(section_state.water_gpu_slot_id);
                section_state.water_gpu_slot_id = 0;
            } else {
                pending_slot_frees_.push_back(section_state.water_gpu_slot_id);
                section_state.water_gpu_slot_id = 0;
            }
        } else {
            // Same fallback rule as opaque: keep confirmed stale water geometry
            // visible until the replacement upload is confirmed.
        }
        section_state.opaque_gpu_allocated = false;
        section_state.water_gpu_allocated = false;
        section_state.opaque_upload_queued = false;
        section_state.water_upload_queued = false;
        section_state.opaque_gpu_uploaded = false;
        section_state.water_gpu_uploaded = false;
        section_state.opaque_draw_index_count = 0;
        section_state.water_draw_index_count = 0;
    }
}

void StreamingTerrainSystem::ensure_gpu_slots(GpuMeshPool& mesh_pool,
                                              const std::uint32_t submit_snapshot_slot) {
    (void)submit_snapshot_slot;

    struct GpuAllocCandidate {
        ChunkCoord coord{};
        std::uint8_t section_index = 0;
        bool water = false;
        float distance_sq = 0.f;
    };

    std::vector<GpuAllocCandidate> candidates;
    for (auto& [coord, chunk_state] : chunk_meshes_) {
        if (store_->try_get(coord) == nullptr || store_->is_pending_unload(coord)) {
            continue;
        }

        if (!chunk_within_mesh_radius(coord)) {
            continue;
        }

        const float chunk_dist = chunk_distance_sq(coord, focus_world_);
        for (SectionMeshState& section_state : chunk_state.sections) {
            if (!section_state.mesh_ready) {
                continue;
            }

            if (!section_state.opaque_gpu_allocated && !section_state.opaque_vertices.empty() &&
                !section_state.opaque_indices.empty()) {
                candidates.push_back(
                    GpuAllocCandidate{coord, section_state.section_index, false, chunk_dist});
            }

            if (!section_state.water_gpu_allocated && !section_state.water_vertices.empty() &&
                !section_state.water_indices.empty()) {
                candidates.push_back(
                    GpuAllocCandidate{coord, section_state.section_index, true, chunk_dist});
            }
        }
    }

    std::sort(candidates.begin(), candidates.end(), [](const GpuAllocCandidate& a, const GpuAllocCandidate& b) {
        return a.distance_sq < b.distance_sq;
    });

    int allocated_this_frame = 0;
    for (const GpuAllocCandidate& candidate : candidates) {
        if (allocated_this_frame >= kMaxGpuAllocationsPerFrame) {
            break;
        }

        ChunkMeshState& chunk_state = chunk_meshes_[candidate.coord];
        SectionMeshState& section_state = chunk_state.sections[candidate.section_index];
        bool slots_changed = false;

        if (!candidate.water) {
            const std::size_t vertex_bytes = section_state.opaque_vertices.size() * sizeof(TerrainVertex);
            const std::size_t index_bytes = section_state.opaque_indices.size() * sizeof(std::uint32_t);
            const std::uint32_t slot_id = mesh_pool.allocate(vertex_bytes, index_bytes);
            if (slot_id != 0) {
                section_state.opaque_gpu_slot_id = slot_id;
                section_state.opaque_gpu_allocated = true;
                section_state.opaque_gpu_uploaded = false;
                section_state.opaque_draw_index_count = 0;
                slots_changed = true;
                ++allocated_this_frame;
            }
        } else {
            const std::size_t vertex_bytes = section_state.water_vertices.size() * sizeof(TerrainVertex);
            const std::size_t index_bytes = section_state.water_indices.size() * sizeof(std::uint32_t);
            const std::uint32_t slot_id = mesh_pool.allocate(vertex_bytes, index_bytes);
            if (slot_id != 0) {
                section_state.water_gpu_slot_id = slot_id;
                section_state.water_gpu_allocated = true;
                section_state.water_gpu_uploaded = false;
                section_state.water_draw_index_count = 0;
                slots_changed = true;
                ++allocated_this_frame;
            }
        }

        if (slots_changed) {
            sync_entity_mesh_slots(candidate.coord, chunk_state);
        }
    }
}

void StreamingTerrainSystem::reset_pending_upload_flags() {
    for (auto& [coord, chunk_state] : chunk_meshes_) {
        (void)coord;
        for (SectionMeshState& section_state : chunk_state.sections) {
            if (!section_state.opaque_gpu_uploaded) {
                section_state.opaque_upload_queued = false;
            }
            if (!section_state.water_gpu_uploaded) {
                section_state.water_upload_queued = false;
            }
        }
    }
}

void StreamingTerrainSystem::purge_stale_chunk_meshes() {
    if (store_ == nullptr) {
        return;
    }

    for (auto it = chunk_meshes_.begin(); it != chunk_meshes_.end();) {
        const ChunkCoord coord = it->first;
        if (store_->try_get(coord) == nullptr || store_->is_pending_unload(coord) ||
            !chunk_within_mesh_radius(coord)) {
            it = chunk_meshes_.erase(it);
        } else {
            ++it;
        }
    }
}

void StreamingTerrainSystem::queue_uploads(MeshUploadQueue& upload_queue, GpuMeshPool& mesh_pool) {
    struct UploadCandidate {
        ChunkCoord coord{};
        std::uint8_t section_index = 0;
        bool water = false;
        float distance_sq = 0.f;
    };

    std::vector<UploadCandidate> candidates;
    for (auto& [coord, chunk_state] : chunk_meshes_) {
        if (store_->try_get(coord) == nullptr || store_->is_pending_unload(coord)) {
            continue;
        }
        if (!chunk_within_mesh_radius(coord)) {
            continue;
        }

        const float chunk_dist = chunk_distance_sq(coord, focus_world_);
        for (SectionMeshState& section_state : chunk_state.sections) {
            if (section_state.opaque_gpu_allocated && !section_state.opaque_upload_queued &&
                !section_state.opaque_gpu_uploaded && section_state.opaque_gpu_slot_id != 0 &&
                !section_state.opaque_vertices.empty()) {
                candidates.push_back(
                    UploadCandidate{coord, section_state.section_index, false, chunk_dist});
            }

            if (section_state.water_gpu_allocated && !section_state.water_upload_queued &&
                !section_state.water_gpu_uploaded && section_state.water_gpu_slot_id != 0 &&
                !section_state.water_vertices.empty()) {
                candidates.push_back(
                    UploadCandidate{coord, section_state.section_index, true, chunk_dist});
            }
        }
    }

    std::sort(candidates.begin(), candidates.end(), [](const UploadCandidate& a, const UploadCandidate& b) {
        return a.distance_sq < b.distance_sq;
    });

    int queued_this_frame = 0;
    for (const UploadCandidate& candidate : candidates) {
        if (queued_this_frame >= kMaxUploadsPerFrame) {
            break;
        }

        ChunkMeshState& chunk_state = chunk_meshes_[candidate.coord];
        SectionMeshState& section_state = chunk_state.sections[candidate.section_index];

        if (!candidate.water) {
            const std::size_t vertex_bytes = section_state.opaque_vertices.size() * sizeof(TerrainVertex);
            const std::size_t index_bytes = section_state.opaque_indices.size() * sizeof(std::uint32_t);
            const GpuMeshSlot* slot = mesh_pool.slot(section_state.opaque_gpu_slot_id);
            if (slot == nullptr || vertex_bytes > slot->vertex_capacity ||
                index_bytes > slot->index_capacity) {
                if (section_state.opaque_gpu_slot_id != 0) {
                    pending_slot_frees_.push_back(section_state.opaque_gpu_slot_id);
                }
                section_state.opaque_gpu_slot_id = 0;
                section_state.opaque_gpu_allocated = false;
                section_state.opaque_upload_queued = false;
                section_state.opaque_gpu_uploaded = false;
                section_state.opaque_draw_index_count = 0;
                continue;
            }

            upload_queue.enqueue(MeshUploadRequest{
                .coord = candidate.coord,
                .section_index = candidate.section_index,
                .slot_id = section_state.opaque_gpu_slot_id,
                .water = false,
                .vertices = section_state.opaque_vertices,
                .indices = section_state.opaque_indices,
            });
            section_state.opaque_upload_queued = true;
            ++queued_this_frame;
            continue;
        }

        const std::size_t vertex_bytes = section_state.water_vertices.size() * sizeof(TerrainVertex);
        const std::size_t index_bytes = section_state.water_indices.size() * sizeof(std::uint32_t);
        const GpuMeshSlot* slot = mesh_pool.slot(section_state.water_gpu_slot_id);
        if (slot == nullptr || vertex_bytes > slot->vertex_capacity ||
            index_bytes > slot->index_capacity) {
            if (section_state.water_gpu_slot_id != 0) {
                pending_slot_frees_.push_back(section_state.water_gpu_slot_id);
            }
            section_state.water_gpu_slot_id = 0;
            section_state.water_gpu_allocated = false;
            section_state.water_upload_queued = false;
            section_state.water_gpu_uploaded = false;
            section_state.water_draw_index_count = 0;
            continue;
        }

        upload_queue.enqueue(MeshUploadRequest{
            .coord = candidate.coord,
            .section_index = candidate.section_index,
            .slot_id = section_state.water_gpu_slot_id,
            .water = true,
            .vertices = section_state.water_vertices,
            .indices = section_state.water_indices,
        });
        section_state.water_upload_queued = true;
        ++queued_this_frame;
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
        if (section_state.opaque_gpu_slot_id != 0 &&
            section_state.section_index < slots.section_slot_ids.size()) {
            slots.section_slot_ids[section_state.section_index] = section_state.opaque_gpu_slot_id;
        }
    }
    entity.set<ChunkMeshSlots>(slots);
}

std::size_t StreamingTerrainSystem::count_mesh_ready_sections() const {
    std::size_t count = 0;
    for (const auto& [coord, chunk_state] : chunk_meshes_) {
        if (store_ == nullptr || store_->try_get(coord) == nullptr ||
            store_->is_pending_unload(coord) || !chunk_within_mesh_radius(coord)) {
            continue;
        }
        for (const SectionMeshState& section_state : chunk_state.sections) {
            if (section_state.mesh_ready && section_state.opaque_index_count > 0) {
                ++count;
            }
        }
    }
    return count;
}

void StreamingTerrainSystem::bootstrap_existing_chunks(ChunkStore& store, const glm::vec3& focus_world) {
    store_ = &store;
    focus_world_ = focus_world;
    store.for_each_loaded([&](const ChunkCoord coord) {
        if (!store.is_pending_unload(coord)) {
            refresh_chunk_section_borders(store, coord);
        }
    });
    store.for_each_loaded([&](const ChunkCoord coord) {
        if (!store.is_pending_unload(coord)) {
            refresh_loaded_chunk_neighbors(store, coord);
        }
    });
    process_mesh_backlog();
}

void StreamingTerrainSystem::sync_mesh_workers() {
    if (jobs_ != nullptr) {
        jobs_->wait_meshing();
    }
    drain_mesh_completions();
}

void StreamingTerrainSystem::reset_gpu_after_device_lost() {
    pending_slot_frees_.clear();
    for (auto& [coord, chunk_state] : chunk_meshes_) {
        (void)coord;
        for (SectionMeshState& section_state : chunk_state.sections) {
            section_state.opaque_gpu_slot_id = 0;
            section_state.water_gpu_slot_id = 0;
            section_state.opaque_gpu_allocated = false;
            section_state.water_gpu_allocated = false;
            section_state.opaque_upload_queued = false;
            section_state.water_upload_queued = false;
            section_state.opaque_gpu_uploaded = false;
            section_state.water_gpu_uploaded = false;
            section_state.opaque_draw_index_count = 0;
            section_state.water_draw_index_count = 0;
        }
    }
}

void StreamingTerrainSystem::heal_seams_after_chunk_loads(const std::vector<ChunkCoord>& loaded_coords) {
    if (store_ == nullptr || loaded_coords.empty()) {
        return;
    }

    static constexpr std::array<ChunkCoord, 6> kNeighborOffsets{
        ChunkCoord{1, 0, 0},
        ChunkCoord{-1, 0, 0},
        ChunkCoord{0, 1, 0},
        ChunkCoord{0, -1, 0},
        ChunkCoord{0, 0, 1},
        ChunkCoord{0, 0, -1},
    };

    std::vector<ChunkCoord> affected;
    affected.reserve(loaded_coords.size() * 7);
    for (const ChunkCoord& loaded : loaded_coords) {
        if (store_->try_get(loaded) == nullptr || store_->is_pending_unload(loaded)) {
            continue;
        }
        affected.push_back(loaded);
        for (const ChunkCoord& offset : kNeighborOffsets) {
            const ChunkCoord neighbor = loaded + offset;
            if (store_->try_get(neighbor) != nullptr && !store_->is_pending_unload(neighbor)) {
                affected.push_back(neighbor);
            }
        }
    }

    std::sort(affected.begin(), affected.end(), [](const ChunkCoord& a, const ChunkCoord& b) {
        if (a.x != b.x) {
            return a.x < b.x;
        }
        if (a.y != b.y) {
            return a.y < b.y;
        }
        return a.z < b.z;
    });
    affected.erase(std::unique(affected.begin(), affected.end()), affected.end());

    for (const ChunkCoord& coord : affected) {
        if (!chunk_within_mesh_radius(coord)) {
            continue;
        }
        refresh_chunk_section_borders(*store_, coord);
    }

    for (const ChunkCoord& coord : affected) {
        if (!chunk_within_mesh_radius(coord)) {
            continue;
        }
        // Soft invalidate so existing meshes keep rendering during seam heal.
        soft_invalidate_chunk_mesh(coord);
        schedule_chunk_mesh(coord);
    }

    process_mesh_backlog();
}

void StreamingTerrainSystem::invalidate_meshes_near_focus(const glm::vec3& focus_world) {
    focus_world_ = focus_world;
    if (store_ == nullptr) {
        return;
    }

    store_->for_each_loaded([&](const ChunkCoord coord) {
        if (!store_->is_pending_unload(coord) && chunk_within_mesh_radius(coord)) {
            invalidate_chunk_mesh(coord);
        }
    });
    process_mesh_backlog();
}

void StreamingTerrainSystem::warmup_meshes_near_focus(
    JobSystem& jobs, const glm::vec3& focus_world, const std::size_t min_sections) {
    focus_world_ = focus_world;
    process_mesh_backlog();

    for (int attempt = 0; attempt < 80 && count_mesh_ready_sections() < min_sections; ++attempt) {
        jobs.wait_meshing();
        drain_mesh_completions();
        process_mesh_backlog();
    }
}

std::size_t StreamingTerrainSystem::count_gpu_ready_sections() const {
    std::size_t count = 0;
    for (const auto& [coord, chunk_state] : chunk_meshes_) {
        if (store_ == nullptr || store_->try_get(coord) == nullptr ||
            store_->is_pending_unload(coord) || !chunk_within_mesh_radius(coord)) {
            continue;
        }
        for (const SectionMeshState& section_state : chunk_state.sections) {
            if (section_state.mesh_ready && section_state.opaque_gpu_allocated &&
                section_state.opaque_gpu_uploaded && section_state.opaque_gpu_slot_id != 0 &&
                section_state.opaque_draw_index_count > 0) {
                ++count;
            }
        }
    }
    return count;
}

void StreamingTerrainSystem::process_mesh_backlog() {
    if (store_ == nullptr || jobs_ == nullptr) {
        return;
    }

    std::vector<ChunkCoord> coords;
    store_->for_each_loaded([&](const ChunkCoord coord) {
        if (!store_->is_pending_unload(coord)) {
            coords.push_back(coord);
        }
    });
    std::sort(coords.begin(), coords.end(), [this](const ChunkCoord& a, const ChunkCoord& b) {
        return chunk_distance_sq(a, focus_world_) < chunk_distance_sq(b, focus_world_);
    });

    for (const ChunkCoord coord : coords) {
        if (count_pending_mesh_jobs() >= kMaxPendingMeshJobs) {
            break;
        }
        if (!chunk_within_mesh_radius(coord)) {
            continue;
        }

        ChunkMeshState& chunk_state = chunk_meshes_[coord];
        chunk_state.coord = coord;
        for (std::uint8_t section_index = 0; section_index < 8; ++section_index) {
            if (count_pending_mesh_jobs() >= kMaxPendingMeshJobs) {
                break;
            }
            SectionMeshState& section_state = chunk_state.sections[section_index];
            if (!section_state.mesh_ready && !section_state.mesh_job_pending) {
                schedule_section_mesh(coord, section_index);
            }
        }
    }
}

void StreamingTerrainSystem::on_frame(const glm::vec3& focus_world,
                                      const std::uint32_t submit_snapshot_slot,
                                      GpuMeshPool& mesh_pool,
                                      MeshUploadQueue& upload_queue,
                                      GpuDeferredFreeQueue& deferred_free) {
    focus_world_ = focus_world;
    drain_mesh_completions();
    process_mesh_backlog();
    for (const std::uint32_t slot_id : pending_slot_frees_) {
        deferred_free.enqueue_free(slot_id, submit_snapshot_slot);
    }
    pending_slot_frees_.clear();
    purge_stale_chunk_meshes();
    release_far_gpu_meshes(mesh_pool, deferred_free, submit_snapshot_slot);
    ensure_gpu_slots(mesh_pool, submit_snapshot_slot);
    reset_pending_upload_flags();
    queue_uploads(upload_queue, mesh_pool);
}

void StreamingTerrainSystem::build_snapshot(WorldRenderSnapshot& snapshot,
                                            const glm::vec3& render_origin,
                                            ChunkStore& store,
                                            const GpuMeshPool& mesh_pool) {
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
        if (!chunk_within_mesh_radius(coord)) {
            return;
        }
        if (opaque_indirect_index >= kMaxOpaqueDrawSections) {
            return;
        }

        const auto chunk_it = chunk_meshes_.find(coord);
        if (chunk_it == chunk_meshes_.end()) {
            return;
        }

        const ChunkMeshState& chunk_state = chunk_it->second;
        const glm::vec3 chunk_origin_world = glm::vec3(coord) * 32.f;

        for (std::uint8_t section_index = 0; section_index < 8; ++section_index) {
            const SectionMeshState& section_state = chunk_state.sections[section_index];
            const glm::ivec3 section_coord = section_coord_from_index(section_index);
            const glm::vec3 section_offset = glm::vec3(section_coord) * 16.f;
            const glm::vec3 model_translation = chunk_origin_world + section_offset - render_origin;
            const glm::vec3 cull_min = model_translation;
            const glm::vec3 cull_max = model_translation + glm::vec3(16.f);

            const glm::vec3 section_center_world = chunk_origin_world + section_offset + glm::vec3(8.f);
            const glm::vec3 delta = section_center_world - focus_world_;
            constexpr float kMaxDrawDistance = 384.f;
            if (glm::dot(delta, delta) > kMaxDrawDistance * kMaxDrawDistance) {
                continue;
            }

            if (!aabb_intersects_frustum(frustum_planes, cull_min, cull_max)) {
                continue;
            }

            // Prefer the uploaded new mesh; fall back to the stale slot while the
            // new upload is in flight so the section never goes invisible.
            if (section_state.mesh_ready && section_state.opaque_gpu_allocated &&
                section_state.opaque_gpu_uploaded && section_state.opaque_draw_index_count > 0 &&
                mesh_pool.is_live(section_state.opaque_gpu_slot_id)) {
                culled_opaque_sections_.push_back(DrawSection{
                    .coord = coord,
                    .section_index = section_index,
                    .model_translation = model_translation,
                    .indirect_index = opaque_indirect_index++,
                    .vertex_buffer_id = section_state.opaque_gpu_slot_id,
                    .index_buffer_id = section_state.opaque_gpu_slot_id,
                    .index_count = section_state.opaque_draw_index_count,
                    .cull_min = cull_min,
                    .cull_max = cull_max,
                });
            } else if (section_state.stale_opaque_gpu_slot_id != 0 &&
                       section_state.stale_opaque_draw_index_count > 0 &&
                       mesh_pool.is_live(section_state.stale_opaque_gpu_slot_id)) {
                culled_opaque_sections_.push_back(DrawSection{
                    .coord = coord,
                    .section_index = section_index,
                    .model_translation = model_translation,
                    .indirect_index = opaque_indirect_index++,
                    .vertex_buffer_id = section_state.stale_opaque_gpu_slot_id,
                    .index_buffer_id = section_state.stale_opaque_gpu_slot_id,
                    .index_count = section_state.stale_opaque_draw_index_count,
                    .cull_min = cull_min,
                    .cull_max = cull_max,
                });
            }

            if (water_indirect_index >= kMaxWaterDrawSections ||
                opaque_indirect_index + water_indirect_index >= kMaxTotalIndirectDraws) {
                continue;
            }

            if (section_state.mesh_ready && section_state.water_gpu_allocated &&
                section_state.water_gpu_uploaded && section_state.water_draw_index_count > 0 &&
                mesh_pool.is_live(section_state.water_gpu_slot_id)) {
                culled_water_sections_.push_back(DrawSection{
                    .coord = coord,
                    .section_index = section_index,
                    .model_translation = model_translation,
                    .indirect_index = water_indirect_index++,
                    .vertex_buffer_id = section_state.water_gpu_slot_id,
                    .index_buffer_id = section_state.water_gpu_slot_id,
                    .index_count = section_state.water_draw_index_count,
                    .cull_min = cull_min,
                    .cull_max = cull_max,
                });
            } else if (section_state.stale_water_gpu_slot_id != 0 &&
                       section_state.stale_water_draw_index_count > 0 &&
                       mesh_pool.is_live(section_state.stale_water_gpu_slot_id)) {
                culled_water_sections_.push_back(DrawSection{
                    .coord = coord,
                    .section_index = section_index,
                    .model_translation = model_translation,
                    .indirect_index = water_indirect_index++,
                    .vertex_buffer_id = section_state.stale_water_gpu_slot_id,
                    .index_buffer_id = section_state.stale_water_gpu_slot_id,
                    .index_count = section_state.stale_water_draw_index_count,
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
