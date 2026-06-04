#include "engine/render/SectionVisibility.hpp"

#include "engine/world/BlockLight.hpp"
#include "engine/world/Chunk.hpp"

#include <deque>
#include <cmath>

namespace engine {

namespace {

[[nodiscard]] const Section* section_at_const(
    const ChunkStore& store,
    ChunkCoord chunk,
    glm::ivec3 section_coord) {
    const Chunk* chunk_ptr = store.try_get(chunk);
    if (chunk_ptr == nullptr) {
        return nullptr;
    }
    if (section_coord.x < 0 || section_coord.x >= CHUNK_SECTIONS_PER_AXIS
        || section_coord.y < 0 || section_coord.y >= CHUNK_SECTIONS_PER_AXIS
        || section_coord.z < 0 || section_coord.z >= CHUNK_SECTIONS_PER_AXIS) {
        return nullptr;
    }
    return &chunk_ptr->section_at(section_coord);
}

[[nodiscard]] bool chunk_within_mesh_radius(
    ChunkCoord coord,
    glm::vec3 focus_world,
    int mesh_chunk_radius_chunks) {
    const float radius_blocks = static_cast<float>(mesh_chunk_radius_chunks * 32);
    const glm::vec3 chunk_center = glm::vec3(coord) * 32.f + glm::vec3(16.f);
    const glm::vec3 delta = chunk_center - focus_world;
    return glm::dot(delta, delta) <= radius_blocks * radius_blocks;
}

} // namespace

bool sections_connected_portal(
    const SectionRenderMeta& meta_a,
    Face face_from_a,
    const Section* neighbor_b) {
    if (neighbor_b == nullptr) {
        return true;
    }
    const Face opposite = opposite_face(face_from_a);
    return section_face_has_portal(meta_a, face_from_a)
        || section_face_has_portal(neighbor_b->render_meta, opposite);
}

SectionVisibilityResult compute_section_visibility(
    const ChunkStore& store,
    const glm::vec3 focus_world,
    const int mesh_chunk_radius_chunks,
    TerrainOcclusionConfig config) {
    SectionVisibilityResult result{};

    if (!config.enabled) {
        return result;
    }

    const SectionVisKey seed = section_key_from_world(focus_world);
    if (store.try_get(seed.coord) == nullptr) {
        result.skipped_no_seed = true;
        return result;
    }

    result.ran_bfs = true;
    result.visible.insert(seed);

    std::deque<SectionVisKey> queue{};
    queue.push_back(seed);

    const int max_sections = std::max(config.max_bfs_sections, 1);

    while (!queue.empty()) {
        const SectionVisKey current = queue.front();
        queue.pop_front();
        ++result.visited_count;

        const glm::ivec3 section_coord = section_coord_from_index(current.section_index);
        const Section* current_section = section_at_const(store, current.coord, section_coord);
        if (current_section == nullptr) {
            continue;
        }

        for (int face_index = 0; face_index < 6; ++face_index) {
            const Face face = static_cast<Face>(face_index);

            ChunkCoord neighbor_chunk{};
            glm::ivec3 neighbor_section_coord{};
            neighbor_chunk_and_section(
                current.coord, section_coord, face, neighbor_chunk, neighbor_section_coord);

            if (!chunk_within_mesh_radius(neighbor_chunk, focus_world, mesh_chunk_radius_chunks)) {
                continue;
            }

            const Section* neighbor_section = section_at_const(store, neighbor_chunk, neighbor_section_coord);

            const bool portal_open = store.is_pending_unload(neighbor_chunk)
                || sections_connected_portal(current_section->render_meta, face, neighbor_section);
            if (!portal_open) {
                continue;
            }

            if (neighbor_section == nullptr) {
                continue;
            }

            const SectionVisKey neighbor_key{
                neighbor_chunk,
                static_cast<uint8_t>(section_index(
                    neighbor_section_coord.x,
                    neighbor_section_coord.y,
                    neighbor_section_coord.z)),
            };

            if (result.visible.contains(neighbor_key)) {
                continue;
            }

            if (static_cast<int>(result.visible.size()) >= max_sections) {
                result.truncated = true;
                return result;
            }

            result.visible.insert(neighbor_key);
            queue.push_back(neighbor_key);
        }
    }

    return result;
}

} // namespace engine