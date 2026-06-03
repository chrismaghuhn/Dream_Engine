#pragma once

#include "engine/core/math.hpp"

#include <glm/glm.hpp>

namespace engine {

struct WorldPosition {
    ChunkCoord chunk{};
    glm::vec3 local{0.f};

    [[nodiscard]] glm::ivec3 to_world_blocks() const {
        return glm::ivec3(chunk) * 32 + glm::ivec3(glm::floor(local));
    }

    static WorldPosition from_world_blocks(int wx, int wy, int wz) {
        const glm::ivec3 block_local = block_local_in_chunk(wx, wy, wz);
        return {
            block_to_chunk(wx, wy, wz),
            glm::vec3(block_local),
        };
    }
};

} // namespace engine
