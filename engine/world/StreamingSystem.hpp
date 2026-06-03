#pragma once

#include "engine/world/ChunkStore.hpp"
#include "engine/world/StreamingConfig.hpp"
#include "engine/world/WorldConfig.hpp"
#include "engine/world/WorldPosition.hpp"

#include <flecs.h>

namespace engine {

[[nodiscard]] bool chunk_in_streaming_set(
    ChunkCoord coord,
    ChunkCoord player_chunk,
    const StreamingConfig& streaming,
    const WorldConfig& world);

void update_streaming(
    ChunkStore& store,
    flecs::world& world,
    const StreamingConfig& streaming,
    const WorldConfig& world_config,
    ChunkCoord player_chunk);

void update_streaming(
    ChunkStore& store,
    flecs::world& ecs,
    const StreamingConfig& streaming,
    const WorldConfig& world_config,
    const WorldPosition& player);

} // namespace engine
