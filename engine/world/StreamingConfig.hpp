#pragma once

#include "engine/core/MemoryBudget.hpp"
#include "engine/world/WorldConfig.hpp"

namespace engine {

enum class RenderPreset;

struct StreamingConfig {
    int max_loaded_chunks = 0;
    int horizontal_radius_chunks = 4;
    int vertical_radius_chunks = 2;
};

StreamingConfig streaming_config_from_hardware(
    const MemoryBudget& mem,
    const WorldConfig& world,
    RenderPreset preset);

} // namespace engine
