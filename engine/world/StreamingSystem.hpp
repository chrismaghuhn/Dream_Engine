#pragma once

#include "engine/world/ChunkStore.hpp"
#include "engine/world/StreamingConfig.hpp"
#include "engine/world/WorldConfig.hpp"
#include "engine/world/WorldPosition.hpp"

#include <flecs.h>
#include <vector>

namespace engine {

[[nodiscard]] bool chunk_in_streaming_set(
    ChunkCoord coord,
    ChunkCoord player_chunk,
    const StreamingConfig& streaming,
    const WorldConfig& world);

/// Returns how many chunks were loaded this call (unload count is not included).
[[nodiscard]] int update_streaming(
    ChunkStore& store,
    flecs::world& world,
    const StreamingConfig& streaming,
    const WorldConfig& world_config,
    ChunkCoord player_chunk,
    std::vector<ChunkCoord>* out_loaded_coords = nullptr);

[[nodiscard]] inline int update_streaming(
    ChunkStore& store,
    flecs::world& ecs,
    const StreamingConfig& streaming,
    const WorldConfig& world_config,
    const WorldPosition& player,
    std::vector<ChunkCoord>* out_loaded_coords = nullptr) {
    return update_streaming(store, ecs, streaming, world_config, player.chunk, out_loaded_coords);
}

/// Loads the 3×3×3 chunk neighborhood required by `PlayerSpawnReadyGate` (no full streaming radius).
void load_spawn_neighborhood(
    ChunkStore& store,
    flecs::world& ecs,
    const WorldConfig& world_config,
    ChunkCoord spawn_chunk);

} // namespace engine
