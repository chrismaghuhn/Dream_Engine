#include <catch2/catch_test_macros.hpp>

#include "engine/gameplay/BlockRegistry.hpp"
#include "engine/gameplay/BlockState.hpp"
#include "engine/world/ChunkStore.hpp"
#include "engine/world/Section.hpp"
#include "engine/world/SectionIndexing.hpp"
#include "engine/world/TerrainLod.hpp"

#include <glm/glm.hpp>

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

} // namespace

TEST_CASE("chunk_horizontal_distance_sq known offset") {
    const float dist_sq =
        engine::chunk_horizontal_distance_sq({0, 0, 0}, glm::vec3(16.f, 0.f, 16.f));
    REQUIRE(dist_sq == 0.f);

    const float dist_sq_xz =
        engine::chunk_horizontal_distance_sq({1, 0, 0}, glm::vec3(16.f, 0.f, 16.f));
    REQUIRE(dist_sq_xz == 1024.f);
}

TEST_CASE("chunk lod selection under lod0_far stays Lod0") {
    engine::TerrainLodConfig config{};
    config.lod0_far_blocks = 96.f;
    config.lod_hysteresis_blocks = 16.f;

    const float dist = 80.f;
    const engine::TerrainLodLevel lod =
        engine::select_chunk_lod(dist * dist, engine::TerrainLodLevel::Lod0, config);
    REQUIRE(lod == engine::TerrainLodLevel::Lod0);
}

TEST_CASE("chunk lod selection beyond lod0_far becomes Lod1") {
    engine::TerrainLodConfig config{};
    config.lod0_far_blocks = 96.f;
    config.lod_hysteresis_blocks = 16.f;

    const float dist = 120.f;
    const engine::TerrainLodLevel lod =
        engine::select_chunk_lod(dist * dist, engine::TerrainLodLevel::Lod0, config);
    REQUIRE(lod == engine::TerrainLodLevel::Lod1);
}

TEST_CASE("chunk lod hysteresis prevents oscillation at band edge") {
    engine::TerrainLodConfig config{};
    config.lod0_far_blocks = 100.f;
    config.lod_hysteresis_blocks = 10.f;

    engine::TerrainLodLevel lod = engine::TerrainLodLevel::Lod0;
    lod = engine::select_chunk_lod(100.f * 100.f, lod, config);
    REQUIRE(lod == engine::TerrainLodLevel::Lod0);

    lod = engine::select_chunk_lod(105.f * 105.f, lod, config);
    REQUIRE(lod == engine::TerrainLodLevel::Lod0);

    lod = engine::select_chunk_lod(111.f * 111.f, lod, config);
    REQUIRE(lod == engine::TerrainLodLevel::Lod1);

    lod = engine::select_chunk_lod(105.f * 105.f, lod, config);
    REQUIRE(lod == engine::TerrainLodLevel::Lod1);

    lod = engine::select_chunk_lod(89.f * 89.f, lod, config);
    REQUIRE(lod == engine::TerrainLodLevel::Lod0);
}

TEST_CASE("downsample water sets chunk has_water") {
    engine::Chunk chunk{};
    const engine::BlockState water = engine::make_block_state(engine::BLOCK_WATER, 0);
    REQUIRE(chunk.section_at({0, 0, 0}).write_block(0, 0, 0, water));
    chunk.section_at({0, 0, 0}).sync_occupancy_from_blocks();
    engine::recompute_chunk_render_meta(chunk);
    REQUIRE(chunk.render_meta.has_water);
}

TEST_CASE("water chunk forces lod0 water border at distance") {
    engine::ChunkStore store;
    store.init(8);
    engine::Chunk* chunk = store.allocate({0, 0, 0});
    REQUIRE(chunk != nullptr);

    const engine::BlockState water = engine::make_block_state(engine::BLOCK_WATER, 0);
    REQUIRE(chunk->section_at({0, 0, 0}).write_block(0, 0, 0, water));
    chunk->section_at({0, 0, 0}).sync_occupancy_from_blocks();
    engine::recompute_chunk_render_meta(*chunk);

    REQUIRE(engine::chunk_force_lod0_water_border(store, {0, 0, 0}));

    engine::TerrainLodConfig config{};
    const float               far_dist = 200.f;
    const engine::TerrainLodLevel lod =
        engine::select_chunk_lod(far_dist * far_dist, engine::TerrainLodLevel::Lod0, config);
    REQUIRE(lod == engine::TerrainLodLevel::Lod1);
}

TEST_CASE("loaded xz neighbor with water forces shoreline lod0 border") {
    engine::ChunkStore store;
    store.init(8);

    REQUIRE(store.allocate({0, 0, 0}) != nullptr);
    REQUIRE(store.allocate({1, 0, 0}) != nullptr);

    engine::Chunk* land = store.try_get({0, 0, 0});
    engine::Chunk* wet  = store.try_get({1, 0, 0});
    REQUIRE(land != nullptr);
    REQUIRE(wet != nullptr);

    fill_section_stone(land->section_at({0, 0, 0}));

    const engine::BlockState water = engine::make_block_state(engine::BLOCK_WATER, 0);
    REQUIRE(wet->section_at({0, 0, 0}).write_block(0, 0, 0, water));
    wet->section_at({0, 0, 0}).sync_occupancy_from_blocks();
    engine::recompute_chunk_render_meta(*wet);
    engine::recompute_chunk_render_meta(*land);

    REQUIRE_FALSE(land->render_meta.has_water);
    REQUIRE(engine::chunk_force_lod0_water_border(store, {0, 0, 0}));
}

TEST_CASE("unloaded xz neighbor with water does not force water border") {
    engine::ChunkStore store;
    store.init(4);

    engine::Chunk* land = store.allocate({0, 0, 0});
    REQUIRE(land != nullptr);
    fill_section_stone(land->section_at({0, 0, 0}));
    engine::recompute_chunk_render_meta(*land);

    REQUIRE_FALSE(engine::chunk_force_lod0_water_border(store, {0, 0, 0}));
}

TEST_CASE("streaming edge requires lod0 when neighbor chunk missing") {
    engine::ChunkStore store;
    store.init(4);

    engine::Chunk* chunk = store.allocate({0, 0, 0});
    REQUIRE(chunk != nullptr);
    fill_section_stone(chunk->section_at({1, 0, 0}));
    chunk->section_at({1, 0, 0}).border.dirty = false;

    REQUIRE(engine::chunk_requires_lod0_streaming_edge(store, {0, 0, 0}));
}

TEST_CASE("streaming edge requires lod0 when section border dirty") {
    engine::ChunkStore store;
    store.init(8);

    REQUIRE(store.allocate({0, 0, 0}) != nullptr);
    REQUIRE(store.allocate({1, 0, 0}) != nullptr);

    engine::Chunk* center = store.try_get({0, 0, 0});
    engine::Chunk* east   = store.try_get({1, 0, 0});
    REQUIRE(center != nullptr);
    REQUIRE(east != nullptr);

    fill_section_stone(center->section_at({1, 0, 0}));
    fill_section_stone(east->section_at({0, 0, 0}));
    center->section_at({1, 0, 0}).border.dirty = true;

    REQUIRE(engine::chunk_requires_lod0_streaming_edge(store, {0, 0, 0}));
}

TEST_CASE("streaming edge clear when neighbors loaded and borders clean") {
    engine::ChunkStore store;
    store.init(16);

    static constexpr engine::ChunkCoord kFaceNeighbors[] = {
        {0, 0, 0},
        {1, 0, 0},
        {-1, 0, 0},
        {0, 1, 0},
        {0, -1, 0},
        {0, 0, 1},
        {0, 0, -1},
    };

    for (const engine::ChunkCoord coord : kFaceNeighbors) {
        REQUIRE(store.allocate(coord) != nullptr);
    }

    engine::Chunk* center = store.try_get({0, 0, 0});
    REQUIRE(center != nullptr);

    fill_section_stone(center->section_at({1, 0, 0}));
    fill_section_stone(store.try_get({1, 0, 0})->section_at({0, 0, 0}));

    for (const engine::ChunkCoord coord : kFaceNeighbors) {
        engine::Chunk* chunk = store.try_get(coord);
        REQUIRE(chunk != nullptr);
        for (engine::Section& section : chunk->sections) {
            section.border.dirty = false;
        }
    }

    REQUIRE_FALSE(engine::chunk_requires_lod0_streaming_edge(store, {0, 0, 0}));
}