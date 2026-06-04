#include <catch2/catch_test_macros.hpp>

#include "engine/gameplay/BlockRegistry.hpp"
#include "engine/gameplay/BlockState.hpp"
#include "engine/render/SectionVisibility.hpp"
#include "engine/world/BlockLight.hpp"
#include "engine/world/ChunkStore.hpp"
#include "engine/world/SectionIndexing.hpp"

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

void fill_chunk_stone(engine::Chunk& chunk) {
    for (int sy = 0; sy < engine::CHUNK_SECTIONS_PER_AXIS; ++sy) {
        for (int sz = 0; sz < engine::CHUNK_SECTIONS_PER_AXIS; ++sz) {
            for (int sx = 0; sx < engine::CHUNK_SECTIONS_PER_AXIS; ++sx) {
                fill_section_stone(chunk.section_at(sx, sy, sz));
            }
        }
    }
}

void carve_face_air(engine::Section& section, engine::Face face) {
    const engine::BlockState air = engine::make_block_state(engine::BLOCK_AIR, 0);
    const int axis = static_cast<int>(face) / 2;
    const int fixed = (static_cast<int>(face) & 1) != 0 ? 0 : engine::SECTION_DIM - 1;

    for (int y = 0; y < engine::SECTION_DIM; ++y) {
        for (int z = 0; z < engine::SECTION_DIM; ++z) {
            for (int x = 0; x < engine::SECTION_DIM; ++x) {
                glm::ivec3 pos{x, y, z};
                if (axis == 0) {
                    pos.x = fixed;
                } else if (axis == 1) {
                    pos.y = fixed;
                } else {
                    pos.z = fixed;
                }
                REQUIRE(section.write_block(pos.x, pos.y, pos.z, air));
            }
        }
    }
    section.sync_occupancy_from_blocks();
}

engine::SectionVisibilityResult run_visibility(
    engine::ChunkStore& store,
    glm::vec3 focus_world,
    engine::TerrainOcclusionConfig config = {}) {
    return engine::compute_section_visibility(store, focus_world, 64, config);
}

} // namespace

TEST_CASE("section_key_from_world at origin") {
    const engine::SectionVisKey key = engine::section_key_from_world(glm::vec3(0.f, 0.f, 0.f));
    REQUIRE(key.coord == engine::ChunkCoord{0, 0, 0});
    REQUIRE(key.section_index == 0);
}

TEST_CASE("section_key_from_world stays in chunk before boundary") {
    const engine::SectionVisKey key = engine::section_key_from_world(glm::vec3(31.9f, 0.f, 31.9f));
    REQUIRE(key.coord == engine::ChunkCoord{0, 0, 0});
    REQUIRE(key.section_index == 3);
}

TEST_CASE("section_key_from_world crosses positive chunk boundary") {
    const engine::SectionVisKey key = engine::section_key_from_world(glm::vec3(32.f, 0.f, 32.f));
    REQUIRE(key.coord == engine::ChunkCoord{1, 0, 1});
    REQUIRE(key.section_index == 0);
}

TEST_CASE("section_key_from_world uses floor for negative coordinates") {
    const engine::SectionVisKey key = engine::section_key_from_world(glm::vec3(-0.1f, 0.f, -0.1f));
    REQUIRE(key.coord == engine::ChunkCoord{-1, 0, -1});
    REQUIRE(key.section_index == 3);
}

TEST_CASE("section_key_from_world at negative chunk boundary") {
    const engine::SectionVisKey key = engine::section_key_from_world(glm::vec3(-32.f, 0.f, -32.f));
    REQUIRE(key.coord == engine::ChunkCoord{-1, 0, -1});
    REQUIRE(key.section_index == 0);
}

TEST_CASE("connectivity BFS reaches neighbor through open portal") {
    engine::ChunkStore store;
    store.init(8);
    engine::Chunk* chunk = store.allocate({0, 0, 0});
    REQUIRE(chunk != nullptr);

    fill_chunk_stone(*chunk);
    carve_face_air(chunk->section_at({0, 0, 0}), engine::Face::PX);
    carve_face_air(chunk->section_at({1, 0, 0}), engine::Face::NX);

    const glm::vec3 focus{8.f, 8.f, 8.f};
    const engine::SectionVisibilityResult result = run_visibility(store, focus);
    REQUIRE(result.ran_bfs);
    REQUIRE_FALSE(result.truncated);
    REQUIRE(result.visible.contains(engine::SectionVisKey{{0, 0, 0}, 0}));
    REQUIRE(result.visible.contains(engine::SectionVisKey{{0, 0, 0}, 1}));
}

TEST_CASE("connectivity BFS does not cross double solid face") {
    engine::ChunkStore store;
    store.init(8);
    engine::Chunk* chunk = store.allocate({0, 0, 0});
    REQUIRE(chunk != nullptr);

    fill_chunk_stone(*chunk);

    const glm::vec3 focus{8.f, 8.f, 8.f};
    const engine::SectionVisibilityResult result = run_visibility(store, focus);
    REQUIRE(result.ran_bfs);
    REQUIRE(result.visible.contains(engine::SectionVisKey{{0, 0, 0}, 0}));
    REQUIRE_FALSE(result.visible.contains(engine::SectionVisKey{{0, 0, 0}, 1}));
}

TEST_CASE("connectivity BFS always includes seed section") {
    engine::ChunkStore store;
    store.init(4);
    REQUIRE(store.allocate({0, 0, 0}) != nullptr);

    const glm::vec3 focus{8.f, 8.f, 8.f};
    const engine::SectionVisKey seed = engine::section_key_from_world(focus);
    const engine::SectionVisibilityResult result = run_visibility(store, focus);
    REQUIRE(result.ran_bfs);
    REQUIRE(result.visible.contains(seed));
}

TEST_CASE("connectivity truncated disables per-section cull") {
    engine::ChunkStore store;
    store.init(8);
    engine::Chunk* chunk = store.allocate({0, 0, 0});
    REQUIRE(chunk != nullptr);

    fill_chunk_stone(*chunk);
    carve_face_air(chunk->section_at({0, 0, 0}), engine::Face::PX);
    carve_face_air(chunk->section_at({1, 0, 0}), engine::Face::NX);

    const glm::vec3 focus{8.f, 8.f, 8.f};
    engine::TerrainOcclusionConfig config{};
    config.max_bfs_sections = 1;
    const engine::SectionVisibilityResult result = run_visibility(store, focus, config);

    REQUIRE(result.ran_bfs);
    REQUIRE(result.truncated);
    REQUIRE_FALSE(engine::connectivity_culling_active(result));

    const engine::SectionVisKey seed{{0, 0, 0}, 0};
    const engine::SectionVisKey neighbor{{0, 0, 0}, 1};
    REQUIRE(engine::connectivity_allows_draw(result, seed));
    REQUIRE(engine::connectivity_allows_draw(result, neighbor));
}