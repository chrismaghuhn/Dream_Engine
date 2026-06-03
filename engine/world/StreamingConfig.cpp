#include "engine/world/StreamingConfig.hpp"

#include "engine/core/EngineConfig.hpp"

#include <algorithm>
#include <cmath>

namespace engine {

namespace {

int clamp_int(int value, int lo, int hi) {
    return std::max(lo, std::min(value, hi));
}

} // namespace

StreamingConfig streaming_config_from_hardware(
    const MemoryBudget& mem,
    const WorldConfig& /*world*/,
    RenderPreset preset) {
    constexpr size_t kChunkVoxelBytesEst = 31 * 1024 * 8;
    StreamingConfig config{};
    const size_t voxel_ram = mem.chunk_voxel_ram > 0 ? mem.chunk_voxel_ram : (512ULL * 1024 * 1024);
    config.max_loaded_chunks =
        static_cast<int>(std::max<size_t>(1, voxel_ram / kChunkVoxelBytesEst));

    config.vertical_radius_chunks = 2;
    const int active_y_layers = config.vertical_radius_chunks * 2 + 1;
    const int chunks_per_layer_budget =
        std::max(1, config.max_loaded_chunks / std::max(1, active_y_layers));

    int horizontal = static_cast<int>(std::sqrt(static_cast<float>(chunks_per_layer_budget) / 3.14159f));
    horizontal = clamp_int(horizontal, 4, 10);

    if (preset == RenderPreset::High) {
        ++horizontal;
    } else if (preset == RenderPreset::Low) {
        --horizontal;
    }
    config.horizontal_radius_chunks = clamp_int(horizontal, 4, 10);
    config.max_loaded_chunks = clamp_int(config.max_loaded_chunks, 128, 768);

    return config;
}

} // namespace engine
