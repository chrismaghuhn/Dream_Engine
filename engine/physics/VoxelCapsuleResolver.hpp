#pragma once

#include "engine/gameplay/VoxelMovementConfig.hpp"
#include "engine/world/ChunkStore.hpp"

#include <glm/glm.hpp>

namespace engine {

struct Capsule {
    glm::vec3 center{0.f};
    float radius = 0.3f;
    float half_height = 0.6f;
};

struct CapsuleMoveResult {
    glm::vec3 center{};
    glm::vec3 velocity{};
    bool on_ground = false;
    bool landed_this_tick = false;
};

[[nodiscard]] bool capsule_overlaps_solid(
    const Capsule& capsule,
    ChunkStore& store,
    float skin_width = 0.03f,
    OccupancyPolicy policy = OccupancyPolicy::SolidIfChunkMissing);

[[nodiscard]] bool capsule_on_ground(
    const Capsule& capsule,
    ChunkStore& store,
    float ground_probe = 0.05f,
    OccupancyPolicy policy = OccupancyPolicy::SolidIfChunkMissing);

CapsuleMoveResult move_and_slide(
    const Capsule& capsule,
    glm::vec3 velocity,
    float delta_time,
    ChunkStore& store,
    const VoxelMovementConfig& movement,
    float ground_snap = 0.1f,
    OccupancyPolicy policy = OccupancyPolicy::SolidIfChunkMissing);

[[nodiscard]] glm::vec3 capsule_half_extents(const Capsule& capsule, float skin_width);

} // namespace engine
