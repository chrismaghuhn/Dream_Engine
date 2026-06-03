#include "engine/render/ThinTerrainPreview.hpp"

#include <spdlog/spdlog.h>

#include <algorithm>

namespace engine {

namespace {

[[nodiscard]] float section_center_distance(const DrawSection& section) {
    return glm::length(section.model_translation + glm::vec3(8.f));
}

} // namespace

void ThinTerrainPreview::init(flecs::world& world, ChunkStore& store, const WorldConfig& world_config) {
    store_ = &store;
    if (store.try_get(coord_) == nullptr) {
        load_chunk(world, store, coord_, world_config);
    }
}

void ThinTerrainPreview::build_cpu_meshes() {
    if (meshes_built_ || store_ == nullptr) {
        return;
    }

    Chunk* chunk = store_->try_get(coord_);
    if (chunk == nullptr) {
        SPDLOG_ERROR("ThinTerrainPreview: chunk ({},{},{}) not loaded", coord_.x, coord_.y, coord_.z);
        return;
    }

    for (std::uint8_t section_idx = 0; section_idx < 8; ++section_idx) {
        SectionMesh& section_mesh = sections_[section_idx];
        section_mesh.section_index = section_idx;
        section_mesh.opaque_vertices.clear();
        section_mesh.opaque_indices.clear();
        section_mesh.water_vertices.clear();
        section_mesh.water_indices.clear();

        const MeshSectionResult result = mesh_section(chunk->sections[section_idx],
                                                      section_mesh.opaque_vertices,
                                                      section_mesh.opaque_indices,
                                                      section_mesh.water_vertices,
                                                      section_mesh.water_indices);
        section_mesh.opaque_index_count = static_cast<std::uint32_t>(result.opaque_index_count);
        section_mesh.water_index_count = static_cast<std::uint32_t>(result.water_index_count);
    }

    meshes_built_ = true;
    SPDLOG_INFO("ThinTerrainPreview: meshed chunk ({},{},{}) — {} sections",
                coord_.x,
                coord_.y,
                coord_.z,
                sections_.size());
}

void ThinTerrainPreview::ensure_gpu_slots(GpuMeshPool& mesh_pool, const std::uint64_t frame_index) {
    (void)frame_index;
    if (slots_allocated_ || !meshes_built_) {
        return;
    }

    for (SectionMesh& section_mesh : sections_) {
        if (!section_mesh.opaque_vertices.empty() && !section_mesh.opaque_indices.empty()) {
            const std::size_t vertex_bytes = section_mesh.opaque_vertices.size() * sizeof(TerrainVertex);
            const std::size_t index_bytes = section_mesh.opaque_indices.size() * sizeof(std::uint32_t);
            section_mesh.opaque_slot_id = mesh_pool.allocate(vertex_bytes, index_bytes);
            if (section_mesh.opaque_slot_id == 0) {
                SPDLOG_ERROR("ThinTerrainPreview: opaque GPU slot failed for section {}",
                             section_mesh.section_index);
            }
        }

        if (!section_mesh.water_vertices.empty() && !section_mesh.water_indices.empty()) {
            const std::size_t vertex_bytes = section_mesh.water_vertices.size() * sizeof(TerrainVertex);
            const std::size_t index_bytes = section_mesh.water_indices.size() * sizeof(std::uint32_t);
            section_mesh.water_slot_id = mesh_pool.allocate(vertex_bytes, index_bytes);
            if (section_mesh.water_slot_id == 0) {
                SPDLOG_ERROR("ThinTerrainPreview: water GPU slot failed for section {}",
                             section_mesh.section_index);
            }
        }
    }

    slots_allocated_ = true;
}

void ThinTerrainPreview::queue_uploads(MeshUploadQueue& upload_queue) {
    if (uploads_queued_ || !slots_allocated_) {
        return;
    }

    for (const SectionMesh& section_mesh : sections_) {
        if (section_mesh.opaque_slot_id != 0 && !section_mesh.opaque_vertices.empty()) {
            upload_queue.enqueue(MeshUploadRequest{
                .slot_id = section_mesh.opaque_slot_id,
                .vertices = section_mesh.opaque_vertices,
                .indices = section_mesh.opaque_indices,
            });
        }
        if (section_mesh.water_slot_id != 0 && !section_mesh.water_vertices.empty()) {
            upload_queue.enqueue(MeshUploadRequest{
                .slot_id = section_mesh.water_slot_id,
                .vertices = section_mesh.water_vertices,
                .indices = section_mesh.water_indices,
            });
        }
    }

    uploads_queued_ = true;
}

void ThinTerrainPreview::fill_snapshot(WorldRenderSnapshot& snapshot, const glm::vec3& render_origin) {
    snapshot.opaque_sections.clear();
    snapshot.water_sections.clear();

    if (!uploads_queued_) {
        ready_ = false;
        return;
    }

    const glm::vec3 chunk_origin_world = glm::vec3(coord_) * 32.f;

    for (const SectionMesh& section_mesh : sections_) {
        const glm::ivec3 section_coord = section_coord_from_index(section_mesh.section_index);
        const glm::vec3 section_offset = glm::vec3(section_coord) * 16.f;
        const glm::vec3 model_translation = chunk_origin_world + section_offset - render_origin;

        if (section_mesh.opaque_slot_id != 0 && section_mesh.opaque_index_count > 0) {
            snapshot.opaque_sections.push_back(DrawSection{
                .coord = coord_,
                .section_index = section_mesh.section_index,
                .model_translation = model_translation,
                .vertex_buffer_id = section_mesh.opaque_slot_id,
                .index_buffer_id = section_mesh.opaque_slot_id,
                .index_count = section_mesh.opaque_index_count,
            });
        }

        if (section_mesh.water_slot_id != 0 && section_mesh.water_index_count > 0) {
            snapshot.water_sections.push_back(DrawSection{
                .coord = coord_,
                .section_index = section_mesh.section_index,
                .model_translation = model_translation,
                .vertex_buffer_id = section_mesh.water_slot_id,
                .index_buffer_id = section_mesh.water_slot_id,
                .index_count = section_mesh.water_index_count,
            });
        }
    }

    std::sort(snapshot.water_sections.begin(),
              snapshot.water_sections.end(),
              [](const DrawSection& a, const DrawSection& b) {
                  return section_center_distance(a) > section_center_distance(b);
              });

    ready_ = !snapshot.opaque_sections.empty() || !snapshot.water_sections.empty();
}

} // namespace engine
