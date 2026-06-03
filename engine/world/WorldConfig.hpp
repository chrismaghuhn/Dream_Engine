#pragma once

#include <cstdint>

namespace engine {

struct WorldConfig {
    int chunk_height_min = -4;
    int chunk_height_max = 8;
    bool finite_bounds = false;
    int sea_level = 64;
    uint64_t world_seed = 42;
    float rebase_radius = 512.f;
    float player_reach = 5.f;
};

} // namespace engine
