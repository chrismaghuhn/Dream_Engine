#pragma once

#include "engine/gameplay/BlockState.hpp"
#include "engine/world/Chunk.hpp"

#include <cstdint>

namespace engine {

class HeightmapWorldgen {
public:
    HeightmapWorldgen(uint64_t world_seed, int sea_level);

    [[nodiscard]] BlockState block_at_world(int wx, int wy, int wz) const;
    void fill_chunk(Chunk& chunk) const;

private:
    [[nodiscard]] int surface_height_at(int wx, int wz) const;

    uint64_t world_seed_;
    int sea_level_;
};

constexpr uint64_t kDefaultWorldSeed = 42;

} // namespace engine
