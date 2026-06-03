#include "engine/render/ThinTerrainPreview.hpp"

#include <spdlog/spdlog.h>

namespace engine {

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
        section_mesh.vertices.clear();
        section_mesh.indices.clear();

        const MeshSectionResult result =
            mesh_section(chunk->sections[section_idx], section_mesh.vertices, section_mesh.indices);
        section_mesh.index_count = static_cast<std::uint32_t>(result.index_count);
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
        if (section_mesh.vertices.empty() || section_mesh.indices.empty()) {
            continue;
        }

        const std::size_t vertex_bytes = section_mesh.vertices.size() * sizeof(TerrainVertex);
        const std::size_t index_bytes = section_mesh.indices.size() * sizeof(std::uint32_t);

        if (section_mesh.slot_id != 0) {
            section_mesh.slot_id =
                mesh_pool.regrow(section_mesh.slot_id, vertex_bytes, index_bytes, frame_index);
        } else {
            section_mesh.slot_id = mesh_pool.allocate(vertex_bytes, index_bytes);
        }

        if (section_mesh.slot_id == 0) {
            SPDLOG_ERROR("ThinTerrainPreview: failed to allocate GPU slot for section {}", section_mesh.section_index);
        }
    }

    slots_allocated_ = true;
}

void ThinTerrainPreview::queue_uploads(MeshUploadQueue& upload_queue) {
    if (uploads_queued_ || !slots_allocated_) {
        return;
    }

    for (const SectionMesh& section_mesh : sections_) {
        if (section_mesh.slot_id == 0 || section_mesh.vertices.empty()) {
            continue;
        }

        upload_queue.enqueue(MeshUploadRequest{
            .slot_id = section_mesh.slot_id,
            .vertices = section_mesh.vertices,
            .indices = section_mesh.indices,
        });
    }

    uploads_queued_ = true;
}

void ThinTerrainPreview::fill_snapshot(WorldRenderSnapshot& snapshot, const glm::vec3& render_origin) {
    snapshot.opaque_sections.clear();

    if (!uploads_queued_) {
        ready_ = false;
        return;
    }

    const glm::vec3 chunk_origin_world = glm::vec3(coord_) * 32.f;

    for (std::size_t draw_index = 0; draw_index < sections_.size(); ++draw_index) {
        const SectionMesh& section_mesh = sections_[draw_index];
        if (section_mesh.slot_id == 0 || section_mesh.index_count == 0) {
            continue;
        }

        const glm::ivec3 section_coord = section_coord_from_index(section_mesh.section_index);
        const glm::vec3 section_offset = glm::vec3(section_coord) * 16.f;
        const glm::vec3 model_translation = chunk_origin_world + section_offset - render_origin;

        snapshot.opaque_sections.push_back(DrawSection{
            .coord = coord_,
            .section_index = section_mesh.section_index,
            .model_translation = model_translation,
            .indirect_index = static_cast<std::uint32_t>(draw_index),
            .vertex_buffer_id = section_mesh.slot_id,
            .index_buffer_id = section_mesh.slot_id,
            .index_count = section_mesh.index_count,
        });
    }

    ready_ = !snapshot.opaque_sections.empty();
}

} // namespace engine
