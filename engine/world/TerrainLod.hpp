#pragma once

#include "engine/core/math.hpp"
#include "engine/world/Chunk.hpp"
#include "engine/world/ChunkStore.hpp"
#include "engine/world/SectionIndexing.hpp"

#include <cmath>
#include <cstdint>

#include <glm/glm.hpp>

namespace engine {

enum class TerrainLodLevel : uint8_t { Lod0 = 0, Lod1 = 1, Impostor = 2 };

struct TerrainLodConfig {
    float lod0_far_blocks       = 96.f;
    float lod1_far_blocks       = 512.f;
    float lod_hysteresis_blocks = 16.f;
};

/// Squared horizontal distance (XZ) from chunk center to focus (world blocks).
inline float chunk_horizontal_distance_sq(ChunkCoord coord, glm::vec3 focus_world) {
    const glm::vec3 chunk_center = glm::vec3(coord) * 32.f + glm::vec3(16.f);
    const float     dx           = chunk_center.x - focus_world.x;
    const float     dz           = chunk_center.z - focus_world.z;
    return dx * dx + dz * dz;
}

inline TerrainLodLevel select_chunk_lod(
    float dist_sq, TerrainLodLevel prev_lod, TerrainLodConfig config) {
    const float dist     = std::sqrt(dist_sq);
    const float lod0_far = config.lod0_far_blocks;
    const float lod1_far = config.lod1_far_blocks;
    const float h        = config.lod_hysteresis_blocks;

    switch (prev_lod) {
    case TerrainLodLevel::Lod0:
        if (dist >= lod0_far + h) {
            if (dist >= lod1_far + h) {
                return TerrainLodLevel::Impostor;
            }
            return TerrainLodLevel::Lod1;
        }
        return TerrainLodLevel::Lod0;
    case TerrainLodLevel::Lod1:
        if (dist < lod0_far - h) {
            return TerrainLodLevel::Lod0;
        }
        if (dist >= lod1_far + h) {
            return TerrainLodLevel::Impostor;
        }
        return TerrainLodLevel::Lod1;
    case TerrainLodLevel::Impostor:
        if (dist < lod1_far - h) {
            if (dist < lod0_far - h) {
                return TerrainLodLevel::Lod0;
            }
            return TerrainLodLevel::Lod1;
        }
        return TerrainLodLevel::Impostor;
    }
    return TerrainLodLevel::Lod0;
}

void recompute_chunk_render_meta(Chunk& chunk);
bool chunk_force_lod0_water_border(const ChunkStore& store, ChunkCoord coord);
bool chunk_requires_lod0_streaming_edge(const ChunkStore& store, ChunkCoord coord);

} // namespace engine