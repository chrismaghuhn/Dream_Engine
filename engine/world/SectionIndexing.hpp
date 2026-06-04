#pragma once
#include "engine/core/math.hpp"

#include <cstdint>
#include <glm/glm.hpp>

namespace engine {

constexpr int SECTION_DIM = 16;
constexpr int CHUNK_SECTIONS_PER_AXIS = 2;

inline int block_index(int x, int y, int z) {
    return x + SECTION_DIM * z + SECTION_DIM * SECTION_DIM * y;
}

inline glm::ivec3 block_coord_from_index(int idx) {
    return {
        idx % SECTION_DIM,
        idx / (SECTION_DIM * SECTION_DIM),
        (idx / SECTION_DIM) % SECTION_DIM,
    };
}

inline int section_index(int sx, int sy, int sz) {
    return sx + 2 * sz + 4 * sy;
}

inline glm::ivec3 section_coord_from_index(int idx) {
    return { (idx) & 1, (idx >> 2) & 1, (idx >> 1) & 1 };
}

/// Squared distance from focus to section center (world blocks). Used for mesh/upload priority.
inline float section_mesh_distance_sq(ChunkCoord coord, int section_index, glm::vec3 focus_world) {
    const glm::ivec3 section_coord = section_coord_from_index(section_index);
    const glm::vec3 section_center_world =
        glm::vec3(coord) * 32.f + glm::vec3(section_coord) * 16.f + glm::vec3(8.f);
    const glm::vec3 delta = section_center_world - focus_world;
    return glm::dot(delta, delta);
}

constexpr uint32_t POS_BITS = 5;
constexpr uint32_t POS_MASK = (1u << POS_BITS) - 1;

enum class Face : uint32_t { PX = 0, NX = 1, PY = 2, NY = 3, PZ = 4, NZ = 5 };

struct SectionRenderMeta {
    bool    is_empty        = true;
    bool    is_opaque_full  = false;
    uint8_t face_solid_mask = 0;
};

inline bool face_solid(const SectionRenderMeta& m, Face f) {
    return (m.face_solid_mask >> static_cast<uint32_t>(f)) & 1u;
}

inline Face opposite_face(Face f) {
    return static_cast<Face>(static_cast<uint32_t>(f) ^ 1u);
}

inline uint32_t pack_vertex(uint32_t x, uint32_t y, uint32_t z, Face f) {
    return (x & POS_MASK)
         | ((y & POS_MASK) << 5)
         | ((z & POS_MASK) << 10)
         | ((uint32_t(f) & 7u) << 15);
}

struct TerrainVertex {
    uint32_t packed_position_normal;
    uint16_t material_id;
    uint8_t  ao;
    uint8_t  light;
};

static_assert(sizeof(TerrainVertex) == 8);

} // namespace engine
