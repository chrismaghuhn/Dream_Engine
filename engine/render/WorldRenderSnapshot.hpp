#pragma once

#include "engine/core/math.hpp"
#include "engine/world/TerrainLod.hpp"

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

namespace engine {

struct DrawSection {
    ChunkCoord coord{};
    std::uint8_t section_index = 0;
    glm::vec3 model_translation{0.f};
    std::uint32_t indirect_index = 0;
    std::uint32_t vertex_buffer_id = 0;
    std::uint32_t index_buffer_id = 0;
    std::uint32_t index_count = 0;
    glm::vec3 cull_min{0.f};
    glm::vec3 cull_max{0.f};
    TerrainLodLevel lod_level = TerrainLodLevel::Lod0;
    float vertex_scale = 1.f;
};

struct WorldRenderSnapshot {
    std::uint64_t frame_index = 0;
    std::uint64_t sim_tick = 0;
    glm::vec3 render_origin{0.f};
    glm::mat4 view{1.f};
    glm::mat4 proj{1.f};
    glm::vec4 sun_dir_intensity{0.f};
    glm::vec4 ambient_fog{0.f};
    std::vector<DrawSection> opaque_sections;
    std::vector<DrawSection> water_sections;
};

} // namespace engine
