#pragma once

#include "engine/world/Section.hpp"
#include "engine/world/SectionIndexing.hpp"

#include <cstdint>
#include <vector>

namespace engine {

struct MeshSectionResult {
    size_t vertex_count = 0;
    size_t index_count  = 0;
};

/// Greedy-mesh one section using section voxels and SectionBorderCache ghost cells only.
MeshSectionResult mesh_section(
    const Section& section,
    std::vector<TerrainVertex>& vertices,
    std::vector<uint32_t>& indices);

} // namespace engine
