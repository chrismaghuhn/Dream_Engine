#include <catch2/catch_test_macros.hpp>

#include "engine/gameplay/BlockRegistry.hpp"
#include "engine/gameplay/BlockState.hpp"
#include "engine/world/GreedyMesher.hpp"
#include "engine/world/Section.hpp"
#include "engine/world/SectionIndexing.hpp"

namespace {

size_t quad_count(const std::vector<uint32_t>& indices) {
    return indices.size() / 6;
}

size_t face_count(const std::vector<engine::TerrainVertex>& vertices) {
    return vertices.size() / 4;
}

void fill_solid(engine::Section& section, int x, int y, int z) {
    const engine::BlockState stone = engine::make_block_state(engine::BLOCK_STONE, 0);
    REQUIRE(section.write_block(x, y, z, stone));
    section.occupancy.set(x, y, z, true);
}

void fill_water(engine::Section& section, int x, int y, int z) {
    const engine::BlockState water = engine::make_block_state(engine::BLOCK_WATER, 0);
    REQUIRE(section.write_block(x, y, z, water));
}

engine::MeshSectionResult mesh_both(engine::Section& section,
                                    std::vector<engine::TerrainVertex>& opaque_vertices,
                                    std::vector<uint32_t>& opaque_indices,
                                    std::vector<engine::TerrainVertex>& water_vertices,
                                    std::vector<uint32_t>& water_indices) {
    return engine::mesh_section(
        section, opaque_vertices, opaque_indices, water_vertices, water_indices);
}

} // namespace

TEST_CASE("greedy mesher empty section returns no geometry") {
    engine::Section section;
    std::vector<engine::TerrainVertex> opaque_vertices;
    std::vector<uint32_t> opaque_indices;
    std::vector<engine::TerrainVertex> water_vertices;
    std::vector<uint32_t> water_indices;
    const engine::MeshSectionResult result =
        mesh_both(section, opaque_vertices, opaque_indices, water_vertices, water_indices);
    REQUIRE(result.opaque_vertex_count == 0);
    REQUIRE(result.opaque_index_count == 0);
    REQUIRE(result.water_vertex_count == 0);
    REQUIRE(result.water_index_count == 0);
}

TEST_CASE("greedy mesher 1 cubed solid has 6 faces") {
    engine::Section section;
    fill_solid(section, 0, 0, 0);

    std::vector<engine::TerrainVertex> opaque_vertices;
    std::vector<uint32_t> opaque_indices;
    std::vector<engine::TerrainVertex> water_vertices;
    std::vector<uint32_t> water_indices;
    const engine::MeshSectionResult result =
        mesh_both(section, opaque_vertices, opaque_indices, water_vertices, water_indices);

    REQUIRE(result.opaque_vertex_count == 24);
    REQUIRE(result.opaque_index_count == 36);
    REQUIRE(face_count(opaque_vertices) == 6);
    REQUIRE(quad_count(opaque_indices) == 6);
    REQUIRE(water_vertices.empty());
}

TEST_CASE("greedy mesher adjacent solids cull shared face") {
    engine::Section section;
    fill_solid(section, 0, 0, 0);
    fill_solid(section, 1, 0, 0);

    std::vector<engine::TerrainVertex> opaque_vertices;
    std::vector<uint32_t> opaque_indices;
    std::vector<engine::TerrainVertex> water_vertices;
    std::vector<uint32_t> water_indices;
    mesh_both(section, opaque_vertices, opaque_indices, water_vertices, water_indices);

    REQUIRE(face_count(opaque_vertices) == 6);
    REQUIRE(quad_count(opaque_indices) == 6);
}

TEST_CASE("greedy mesher adjacent water culls shared face") {
    engine::Section section;
    fill_water(section, 0, 0, 0);
    fill_water(section, 1, 0, 0);

    std::vector<engine::TerrainVertex> opaque_vertices;
    std::vector<uint32_t> opaque_indices;
    std::vector<engine::TerrainVertex> water_vertices;
    std::vector<uint32_t> water_indices;
    mesh_both(section, opaque_vertices, opaque_indices, water_vertices, water_indices);

    REQUIRE(face_count(water_vertices) == 6);
    REQUIRE(quad_count(water_indices) == 6);
    REQUIRE(opaque_vertices.empty());
}

TEST_CASE("greedy mesher water against air emits faces") {
    engine::Section section;
    fill_water(section, 0, 0, 0);

    std::vector<engine::TerrainVertex> opaque_vertices;
    std::vector<uint32_t> opaque_indices;
    std::vector<engine::TerrainVertex> water_vertices;
    std::vector<uint32_t> water_indices;
    mesh_both(section, opaque_vertices, opaque_indices, water_vertices, water_indices);

    REQUIRE(face_count(water_vertices) == 6);
    REQUIRE(opaque_vertices.empty());
}

TEST_CASE("greedy mesher border neighbor solid culls exterior face") {
    engine::Section section;
    fill_solid(section, 15, 0, 0);

    const engine::BlockState stone = engine::make_block_state(engine::BLOCK_STONE, 0);
    engine::BorderCell& neighbor = section.border.face[static_cast<size_t>(engine::Face::PX)][0];
    neighbor.block       = stone;
    neighbor.sky_light   = 0;
    neighbor.block_light = 0;

    std::vector<engine::TerrainVertex> opaque_vertices;
    std::vector<uint32_t> opaque_indices;
    std::vector<engine::TerrainVertex> water_vertices;
    std::vector<uint32_t> water_indices;
    mesh_both(section, opaque_vertices, opaque_indices, water_vertices, water_indices);

    REQUIRE(face_count(opaque_vertices) == 5);
}

TEST_CASE("greedy mesher border light copied to edge vertex nibbles") {
    engine::Section section;
    fill_solid(section, 15, 0, 0);

    for (engine::BorderCell& cell : section.border.face[static_cast<size_t>(engine::Face::PX)]) {
        cell.block       = engine::make_block_state(engine::BLOCK_AIR, 0);
        cell.sky_light   = 10;
        cell.block_light = 5;
    }

    std::vector<engine::TerrainVertex> opaque_vertices;
    std::vector<uint32_t> opaque_indices;
    std::vector<engine::TerrainVertex> water_vertices;
    std::vector<uint32_t> water_indices;
    mesh_both(section, opaque_vertices, opaque_indices, water_vertices, water_indices);

    const uint8_t expected = static_cast<uint8_t>((10 << 4) | 5);
    bool found_px_face       = false;

    for (const engine::TerrainVertex& v : opaque_vertices) {
        const uint32_t face_bits = (v.packed_position_normal >> 15) & 7u;
        if (face_bits != static_cast<uint32_t>(engine::Face::PX)) {
            continue;
        }
        found_px_face = true;
        const uint32_t x = v.packed_position_normal & engine::POS_MASK;
        const uint32_t y = (v.packed_position_normal >> 5) & engine::POS_MASK;
        const uint32_t z = (v.packed_position_normal >> 10) & engine::POS_MASK;
        if (x == 16 && y == 0 && z == 0) {
            REQUIRE(v.light == expected);
        }
    }

    REQUIRE(found_px_face);
}
