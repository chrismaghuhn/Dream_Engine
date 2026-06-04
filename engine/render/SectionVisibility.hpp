#pragma once

#include "engine/core/math.hpp"
#include "engine/world/BlockPos.hpp"
#include "engine/world/ChunkStore.hpp"
#include "engine/world/SectionIndexing.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <unordered_set>

namespace engine {

struct TerrainOcclusionConfig {
    bool enabled = true;
    int  max_bfs_sections = 8192;
};

struct SectionVisKey {
    ChunkCoord coord{};
    uint8_t    section_index = 0;

    [[nodiscard]] bool operator==(const SectionVisKey& other) const noexcept {
        return coord == other.coord && section_index == other.section_index;
    }
};

struct SectionVisKeyHash {
    size_t operator()(SectionVisKey key) const noexcept {
        size_t h = ChunkCoordHash{}(key.coord);
        h ^= std::hash<uint8_t>()(key.section_index) + 0x9e3779b9 + (h << 6) + (h >> 2);
        return h;
    }
};

[[nodiscard]] inline bool section_face_has_portal(const SectionRenderMeta& meta, Face face) {
    return !face_solid(meta, face);
}

[[nodiscard]] bool sections_connected_portal(
    const SectionRenderMeta& meta_a,
    Face face_from_a,
    const Section* neighbor_b);

[[nodiscard]] inline SectionVisKey section_key_from_world(glm::vec3 focus_world) {
    const glm::ivec3 block{
        static_cast<int>(glm::floor(focus_world.x)),
        static_cast<int>(glm::floor(focus_world.y)),
        static_cast<int>(glm::floor(focus_world.z)),
    };
    const BlockPos pos = BlockPos::from_world_blocks(block.x, block.y, block.z);
    const glm::ivec3 sec = pos.section_coord();
    return {pos.chunk, static_cast<uint8_t>(section_index(sec.x, sec.y, sec.z))};
}

struct SectionVisibilityResult {
    std::unordered_set<SectionVisKey, SectionVisKeyHash> visible{};
    int  visited_count   = 0;
    bool ran_bfs         = false;
    bool truncated       = false;
    bool skipped_no_seed = false;
};

[[nodiscard]] SectionVisibilityResult compute_section_visibility(
    const ChunkStore& store,
    glm::vec3 focus_world,
    int mesh_chunk_radius_chunks,
    TerrainOcclusionConfig config = {});

[[nodiscard]] inline bool connectivity_culling_active(const SectionVisibilityResult& visibility) {
    return visibility.ran_bfs && !visibility.skipped_no_seed && !visibility.truncated;
}

[[nodiscard]] inline bool connectivity_allows_draw(
    const SectionVisibilityResult& visibility,
    SectionVisKey key) {
    if (!connectivity_culling_active(visibility)) {
        return true;
    }
    return visibility.visible.contains(key);
}

} // namespace engine