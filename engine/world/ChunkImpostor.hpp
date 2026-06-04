#pragma once

#include "engine/world/Chunk.hpp"

#include <glm/glm.hpp>

namespace engine {

struct ChunkImpostorSummary {
    bool valid = false;
    glm::vec3 color{0.45f};
    float min_y = 0.f; // chunk-local blocks 0..32
    float max_y = 0.f;
};

ChunkImpostorSummary compute_chunk_impostor(const Chunk& chunk);

} // namespace engine