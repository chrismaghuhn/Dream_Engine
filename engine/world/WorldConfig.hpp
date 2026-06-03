#pragma once

namespace engine {

struct WorldConfig {
    int chunk_height_min = -4;
    int chunk_height_max = 8;
    bool finite_bounds = false;
    int sea_level = 64;
    float rebase_radius = 512.f;
};

} // namespace engine
