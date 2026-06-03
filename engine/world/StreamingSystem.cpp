#include "engine/world/StreamingSystem.hpp"

#include "engine/world/ChunkLifecycle.hpp"

#include <vector>

namespace engine {

bool chunk_in_streaming_set(
    ChunkCoord coord,
    ChunkCoord player_chunk,
    const StreamingConfig& streaming,
    const WorldConfig& world) {
    if (coord.y < world.chunk_height_min || coord.y > world.chunk_height_max) {
        return false;
    }

    const int dx = coord.x - player_chunk.x;
    const int dz = coord.z - player_chunk.z;
    const int r = streaming.horizontal_radius_chunks;
    if (dx * dx + dz * dz > r * r) {
        return false;
    }

    const int dy = coord.y - player_chunk.y;
    return dy >= -streaming.vertical_radius_chunks && dy <= streaming.vertical_radius_chunks;
}

void load_spawn_neighborhood(
    ChunkStore& store,
    flecs::world& ecs,
    const WorldConfig& world_config,
    ChunkCoord spawn_chunk) {
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dz = -1; dz <= 1; ++dz) {
            for (int dx = -1; dx <= 1; ++dx) {
                const ChunkCoord coord{
                    spawn_chunk.x + dx,
                    spawn_chunk.y + dy,
                    spawn_chunk.z + dz,
                };
                if (coord.y < world_config.chunk_height_min ||
                    coord.y > world_config.chunk_height_max) {
                    continue;
                }
                if (store.try_get(coord) == nullptr) {
                    load_chunk(ecs, store, coord, world_config);
                }
            }
        }
    }
}

void update_streaming(
    ChunkStore& store,
    flecs::world& ecs,
    const StreamingConfig& streaming,
    const WorldConfig& world_config,
    ChunkCoord player_chunk) {
    const int r = streaming.horizontal_radius_chunks;
    const int cy_min =
        std::max(world_config.chunk_height_min, player_chunk.y - streaming.vertical_radius_chunks);
    const int cy_max =
        std::min(world_config.chunk_height_max, player_chunk.y + streaming.vertical_radius_chunks);

    const int load_budget = streaming.max_chunks_load_per_update;
    int loaded_this_update = 0;
    bool load_budget_exhausted = false;

    for (int cy = cy_min; cy <= cy_max && !load_budget_exhausted; ++cy) {
        for (int cx = player_chunk.x - r; cx <= player_chunk.x + r && !load_budget_exhausted; ++cx) {
            for (int cz = player_chunk.z - r; cz <= player_chunk.z + r; ++cz) {
                const ChunkCoord coord{cx, cy, cz};
                if (!chunk_in_streaming_set(coord, player_chunk, streaming, world_config)) {
                    continue;
                }
                if (store.try_get(coord) == nullptr) {
                    load_chunk(ecs, store, coord, world_config);
                    ++loaded_this_update;
                    if (load_budget > 0 && loaded_this_update >= load_budget) {
                        load_budget_exhausted = true;
                        break;
                    }
                }
            }
        }
    }

    std::vector<ChunkCoord> to_unload;
    store.for_each_loaded([&](ChunkCoord coord) {
        if (!chunk_in_streaming_set(coord, player_chunk, streaming, world_config)) {
            to_unload.push_back(coord);
        }
    });
    for (const ChunkCoord coord : to_unload) {
        unload_chunk(ecs, store, coord);
    }
}

void update_streaming(
    ChunkStore& store,
    flecs::world& ecs,
    const StreamingConfig& streaming,
    const WorldConfig& world_config,
    const WorldPosition& player) {
    update_streaming(store, ecs, streaming, world_config, player.chunk);
}

} // namespace engine
