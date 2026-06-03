#pragma once

#include "engine/world/ChunkStore.hpp"
#include "engine/world/WorldConfig.hpp"
#include "engine/world/WorldEvents.hpp"

#include <cstdint>
#include <functional>

#include <flecs.h>

namespace engine {

class GpuDeferredFreeQueue;

struct ChunkGpuServices {
    GpuDeferredFreeQueue* deferred_free = nullptr;
    std::function<std::uint32_t()> submit_snapshot_slot;
};

void set_chunk_gpu_services(ChunkGpuServices* services);
void register_chunk_lifecycle(flecs::world& world);

void refresh_chunk_section_borders(ChunkStore& store, ChunkCoord coord);
/// Refresh border caches for all loaded axial neighbors (fixes seams when a new chunk appears).
void refresh_loaded_chunk_neighbors(ChunkStore& store, ChunkCoord coord);

[[nodiscard]] flecs::entity load_chunk(
    flecs::world& world, ChunkStore& store, ChunkCoord coord, const WorldConfig& world_config);
void unload_chunk(flecs::world& world, ChunkStore& store, ChunkCoord coord);

} // namespace engine
