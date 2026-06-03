#pragma once

#include "engine/core/math.hpp"

#include <glm/glm.hpp>

namespace engine {

struct BlockPos {
    ChunkCoord chunk{};
    glm::ivec3 block_local{};

    [[nodiscard]] static BlockPos from_world_blocks(int wx, int wy, int wz) {
        return { block_to_chunk(wx, wy, wz), block_local_in_chunk(wx, wy, wz) };
    }

    [[nodiscard]] glm::ivec3 to_world_blocks() const {
        return glm::ivec3(chunk) * 32 + block_local;
    }

    [[nodiscard]] glm::ivec3 section_coord() const {
        return { block_local.x >> 4, block_local.y >> 4, block_local.z >> 4 };
    }

    [[nodiscard]] glm::ivec3 block_in_section() const {
        return { block_local.x & 15, block_local.y & 15, block_local.z & 15 };
    }
};

} // namespace engine
