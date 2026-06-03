#pragma once

#include "engine/core/math.hpp"
#include "engine/gameplay/BlockState.hpp"

#include <cstdint>
#include <glm/glm.hpp>

namespace engine {

// Custom Flecs event payloads (design spec §6).
struct EvtOriginShift {
    glm::vec3 new_origin{};
};

struct EvtChunkLoaded {
    ChunkCoord coord{};
};

struct EvtChunkUnloaded {
    ChunkCoord coord{};
};

struct EvtChunkDirty {
    ChunkCoord coord{};
    uint8_t section = 0;
    uint8_t bits = 0;
};

struct EvtChunkMeshReady {
    ChunkCoord coord{};
    uint8_t section = 0;
};

struct EvtBlockBroken {
    ChunkCoord coord{};
    glm::ivec3 block_local{};
    BlockState old_state{};
};

struct EvtBlockPlaced {
    ChunkCoord coord{};
    glm::ivec3 block_local{};
    BlockState new_state{};
};

// Component tag — OnAdd observer enqueues remesh / collision / light (§6).
struct ChunkDirty {};

} // namespace engine

// Register event payload types with Flecs (events use the component registry).
#define ECS_EVENT_DECLARE(Type) struct Type
#define ECS_EVENT(world, Type) \
    do { (world).component<Type>(); } while (0)
