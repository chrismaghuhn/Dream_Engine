#include <catch2/catch_test_macros.hpp>

#include "engine/gameplay/BlockRegistry.hpp"
#include "engine/gameplay/BlockState.hpp"
#include "engine/world/ChunkLodMesher.hpp"
#include "engine/world/GreedyMesher.hpp"
#include "engine/world/Section.hpp"
#include "engine/world/SectionIndexing.hpp"

#include <algorithm>

namespace {

void fill_section_stone(engine::Section& section) {
    const engine::BlockState stone = engine::make_block_state(engine::BLOCK_STONE, 0);
    for (int y = 0; y < engine::SECTION_DIM; ++y) {
        for (int z = 0; z < engine::SECTION_DIM; ++z) {
            for (int x = 0; x < engine::SECTION_DIM; ++x) {
                REQUIRE(section.write_block(x, y, z, stone));
            }
        }
    }
    section.sync_occupancy_from_blocks();
}

bool all_packed_coords_le_16(const std::vector<engine::TerrainVertex>& vertices) {
    for (const engine::TerrainVertex& vert : vertices) {
        const uint32_t packed = vert.packed_position_normal;
        if ((packed & engine::POS_MASK) > 16u) {
            return false;
        }
        if (((packed >> 5) & engine::POS_MASK) > 16u) {
            return false;
        }
        if (((packed >> 10) & engine::POS_MASK) > 16u) {
            return false;
        }
    }
    return true;
}

} // namespace

TEST_CASE("chunk lod mesher downsample 2 cubed fine solid is coarse solid") {
    engine::Chunk chunk{};
    fill_section_stone(chunk.section_at({0, 0, 0}));

    std::vector<engine::TerrainVertex> verts;
    std::vector<uint32_t>              indices;
    const engine::MeshChunkLodResult result =
        engine::mesh_chunk_lod1(chunk, verts, indices);

    REQUIRE(result.opaque_index_count > 0);
    REQUIRE(result.opaque_vertex_count >= 24);
}

TEST_CASE("chunk lod mesher single fine corner makes coarse solid") {
    engine::Chunk chunk{};
    const engine::BlockState stone = engine::make_block_state(engine::BLOCK_STONE, 0);
    REQUIRE(chunk.section_at({0, 0, 0}).write_block(0, 0, 0, stone));

    std::vector<engine::TerrainVertex> verts;
    std::vector<uint32_t>              indices;
    const engine::MeshChunkLodResult result =
        engine::mesh_chunk_lod1(chunk, verts, indices);

    REQUIRE(result.opaque_index_count == 36);
    REQUIRE(verts.size() == 24);
}

TEST_CASE("chunk lod mesher homogeneous stone fewer indices than eight sections") {
    engine::Chunk chunk{};
    for (int sy = 0; sy < 2; ++sy) {
        for (int sz = 0; sz < 2; ++sz) {
            for (int sx = 0; sx < 2; ++sx) {
                fill_section_stone(chunk.section_at({sx, sy, sz}));
            }
        }
    }

    std::vector<engine::TerrainVertex> lod1_verts;
    std::vector<uint32_t>              lod1_indices;
    const engine::MeshChunkLodResult lod1 =
        engine::mesh_chunk_lod1(chunk, lod1_verts, lod1_indices);

    size_t section_index_total = 0;
    for (int section_idx = 0; section_idx < 8; ++section_idx) {
        std::vector<engine::TerrainVertex> opaque_vertices;
        std::vector<uint32_t>              opaque_indices;
        std::vector<engine::TerrainVertex> water_vertices;
        std::vector<uint32_t>              water_indices;
        const engine::MeshSectionResult section_result = engine::mesh_section(
            chunk.sections[static_cast<size_t>(section_idx)],
            opaque_vertices,
            opaque_indices,
            water_vertices,
            water_indices);
        section_index_total += section_result.opaque_index_count;
    }

    REQUIRE(lod1.opaque_index_count > 0);
    REQUIRE(lod1.opaque_index_count < section_index_total);
}

TEST_CASE("chunk lod mesher exterior coarse solid emits chunk boundary face") {
    engine::Chunk chunk{};
    for (int sy = 0; sy < 2; ++sy) {
        for (int sz = 0; sz < 2; ++sz) {
            for (int sx = 0; sx < 2; ++sx) {
                fill_section_stone(chunk.section_at({sx, sy, sz}));
            }
        }
    }

    std::vector<engine::TerrainVertex> verts;
    std::vector<uint32_t>              indices;
    engine::mesh_chunk_lod1(chunk, verts, indices);

    bool has_px_face = false;
    for (const engine::TerrainVertex& vert : verts) {
        const uint32_t face = (vert.packed_position_normal >> 15) & 7u;
        const uint32_t x    = vert.packed_position_normal & engine::POS_MASK;
        if (face == static_cast<uint32_t>(engine::Face::PX) && x == 16u) {
            has_px_face = true;
        }
    }
    REQUIRE(has_px_face);
}

TEST_CASE("chunk lod mesher vertices use coarse coords 0 through 16 only") {
    engine::Chunk chunk{};
    fill_section_stone(chunk.section_at({0, 0, 0}));

    std::vector<engine::TerrainVertex> verts;
    std::vector<uint32_t>              indices;
    engine::mesh_chunk_lod1(chunk, verts, indices);

    REQUIRE(all_packed_coords_le_16(verts));
}