#pragma once

#include "engine/world/ChunkStore.hpp"
#include "engine/world/WorldEvents.hpp"

#include <flecs.h>

namespace engine {

void register_chunk_lifecycle(flecs::world& world);

[[nodiscard]] flecs::entity load_chunk(flecs::world& world, ChunkStore& store, ChunkCoord coord);
void unload_chunk(flecs::world& world, ChunkStore& store, ChunkCoord coord);

} // namespace engine
