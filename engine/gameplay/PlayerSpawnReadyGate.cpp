#include "engine/gameplay/PlayerSpawnReadyGate.hpp"

#include "engine/core/math.hpp"

namespace engine {

namespace {

bool neighborhood_chunks_loaded(ChunkStore& store, ChunkCoord spawn_chunk) {
    for (int dz = -1; dz <= 1; ++dz) {
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                const ChunkCoord neighbor{
                    spawn_chunk.x + dx,
                    spawn_chunk.y + dy,
                    spawn_chunk.z + dz,
                };
                if (store.try_get(neighbor) == nullptr) {
                    return false;
                }
            }
        }
    }
    return true;
}

bool occupancy_samples_ok(ChunkStore& store, const Capsule& capsule) {
    const glm::ivec3 feet = glm::ivec3(glm::floor(capsule.center - glm::vec3{0.f, capsule.half_height + capsule.radius, 0.f}));
    const glm::ivec3 head = glm::ivec3(glm::floor(capsule.center + glm::vec3{0.f, capsule.half_height + capsule.radius, 0.f}));

    const auto chunk_loaded_at = [&](int wx, int wy, int wz) {
        (void)occupancy_at(wx, wy, wz, store, OccupancyPolicy::AirIfChunkMissing);
        return store.try_get(block_to_chunk(wx, wy, wz)) != nullptr;
    };

    return chunk_loaded_at(feet.x, feet.y, feet.z) && chunk_loaded_at(head.x, head.y, head.z) &&
           chunk_loaded_at(feet.x, feet.y + 1, feet.z);
}

} // namespace

bool PlayerSpawnReadyGate::update(ChunkStore& store, const Capsule& capsule, ChunkCoord spawn_chunk) {
    if (ready_) {
        return true;
    }

    if (store.try_get(spawn_chunk) == nullptr) {
        return false;
    }
    if (!neighborhood_chunks_loaded(store, spawn_chunk)) {
        return false;
    }
    if (!occupancy_samples_ok(store, capsule)) {
        return false;
    }

    ready_ = true;
    return true;
}

} // namespace engine
