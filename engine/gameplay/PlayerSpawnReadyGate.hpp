#pragma once

#include "engine/physics/VoxelCapsuleResolver.hpp"
#include "engine/world/ChunkStore.hpp"

#include <glm/glm.hpp>

namespace engine {

class PlayerSpawnReadyGate {
public:
    void reset() { ready_ = false; }

    [[nodiscard]] bool is_ready() const { return ready_; }

    bool update(ChunkStore& store, const Capsule& capsule, ChunkCoord spawn_chunk);

private:
    bool ready_ = false;
};

} // namespace engine
