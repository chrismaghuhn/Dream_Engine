#pragma once

#include "engine/world/Chunk.hpp"
#include "engine/world/GreedyMesher.hpp"
#include "engine/world/SectionIndexing.hpp"

#include <cstdint>
#include <vector>

namespace engine {

using MeshChunkLodResult = MeshSectionResult;

/// Downsampled 16³ greedy opaque mesh for one chunk (LOD1). Coarse vertices 0..16; no water quads.
MeshChunkLodResult mesh_chunk_lod1(
    const Chunk& chunk,
    std::vector<TerrainVertex>& opaque_vertices,
    std::vector<uint32_t>& opaque_indices);

} // namespace engine