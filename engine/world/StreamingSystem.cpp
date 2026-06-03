#include "engine/world/StreamingSystem.hpp"

#include "engine/world/ChunkLifecycle.hpp"

#include <algorithm>
#include <cstdlib>
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
                    (void)load_chunk(ecs, store, coord, world_config);
                }
            }
        }
    }
}

int update_streaming(
    ChunkStore& store,
    flecs::world& ecs,
    const StreamingConfig& streaming,
    const WorldConfig& world_config,
    ChunkCoord player_chunk,
    std::vector<ChunkCoord>* out_loaded_coords) {
    if (out_loaded_coords != nullptr) {
        out_loaded_coords->clear();
    }
    const int r = streaming.horizontal_radius_chunks;
    const int cy_min =
        std::max(world_config.chunk_height_min, player_chunk.y - streaming.vertical_radius_chunks);
    const int cy_max =
        std::min(world_config.chunk_height_max, player_chunk.y + streaming.vertical_radius_chunks);

    const int load_budget = streaming.max_chunks_load_per_update;
    int loaded_this_update = 0;

    std::vector<ChunkCoord> load_candidates;
    for (int cy = cy_min; cy <= cy_max; ++cy) {
        for (int cx = player_chunk.x - r; cx <= player_chunk.x + r; ++cx) {
            for (int cz = player_chunk.z - r; cz <= player_chunk.z + r; ++cz) {
                const ChunkCoord coord{cx, cy, cz};
                if (!chunk_in_streaming_set(coord, player_chunk, streaming, world_config)) {
                    continue;
                }
                if (store.try_get(coord) == nullptr) {
                    load_candidates.push_back(coord);
                }
            }
        }
    }

    std::sort(load_candidates.begin(), load_candidates.end(), [&](const ChunkCoord& a, const ChunkCoord& b) {
        const int ady = std::abs(a.y - player_chunk.y);
        const int bdy = std::abs(b.y - player_chunk.y);
        if (ady != bdy) {
            return ady < bdy;
        }

        const int adx = a.x - player_chunk.x;
        const int adz = a.z - player_chunk.z;
        const int bdx = b.x - player_chunk.x;
        const int bdz = b.z - player_chunk.z;
        const int ad2 = adx * adx + adz * adz;
        const int bd2 = bdx * bdx + bdz * bdz;
        if (ad2 != bd2) {
            return ad2 < bd2;
        }
        if (a.x != b.x) {
            return a.x < b.x;
        }
        if (a.z != b.z) {
            return a.z < b.z;
        }
        return a.y < b.y;
    });

    for (const ChunkCoord coord : load_candidates) {
        if (load_budget > 0 && loaded_this_update >= load_budget) {
            break;
        }
        (void)load_chunk(ecs, store, coord, world_config);
        if (out_loaded_coords != nullptr) {
            out_loaded_coords->push_back(coord);
        }
        ++loaded_this_update;
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

    return loaded_this_update;
}

} // namespace engine
