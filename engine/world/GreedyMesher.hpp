#pragma once

#include "engine/world/Section.hpp"
#include "engine/world/SectionIndexing.hpp"

#include <cstdint>
#include <vector>

namespace engine {

struct MeshSectionResult {
    size_t opaque_vertex_count = 0;
    size_t opaque_index_count  = 0;
    size_t water_vertex_count  = 0;
    size_t water_index_count   = 0;
};

/// Greedy-mesh one section: opaque solids and water into separate buffers (§18).
MeshSectionResult mesh_section(
    const Section& section,
    std::vector<TerrainVertex>& opaque_vertices,
    std::vector<uint32_t>& opaque_indices,
    std::vector<TerrainVertex>& water_vertices,
    std::vector<uint32_t>& water_indices);

} // namespace engine
