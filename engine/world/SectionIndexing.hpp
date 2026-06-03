#pragma once
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

constexpr uint32_t POS_BITS = 5;
constexpr uint32_t POS_MASK = (1u << POS_BITS) - 1;

enum class Face : uint32_t { PX = 0, NX = 1, PY = 2, NY = 3, PZ = 4, NZ = 5 };

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
