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
    std::function<std::uint64_t()> frame_index;
};

void set_chunk_gpu_services(ChunkGpuServices* services);
void register_chunk_lifecycle(flecs::world& world);

[[nodiscard]] flecs::entity load_chunk(
    flecs::world& world, ChunkStore& store, ChunkCoord coord, const WorldConfig& world_config);
void unload_chunk(flecs::world& world, ChunkStore& store, ChunkCoord coord);

} // namespace engine
