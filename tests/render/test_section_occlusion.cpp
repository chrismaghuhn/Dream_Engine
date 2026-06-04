#include <catch2/catch_test_macros.hpp>

#include "engine/core/JobSystem.hpp"

#define private public
#include "engine/render/StreamingTerrainSystem.hpp"
#undef private

#include "engine/gameplay/BlockRegistry.hpp"
#include "engine/gameplay/BlockState.hpp"
#include "engine/world/BlockLight.hpp"
#include "engine/world/ChunkStore.hpp"
#include "engine/world/SectionIndexing.hpp"
#include "engine/world/WorldConfig.hpp"
#include "engine/world/WorldModule.hpp"

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

void bury_section_in_stone_shell(engine::ChunkStore& store,
                               const engine::ChunkCoord coord,
                               const std::uint8_t section_index) {
    engine::Chunk* center = store.try_get(coord);
    if (center == nullptr) {
        center = store.allocate(coord);
    }
    REQUIRE(center != nullptr);

    const glm::ivec3 section_coord = engine::section_coord_from_index(section_index);
    fill_section_stone(center->section_at(section_coord));

    for (int fi = 0; fi < 6; ++fi) {
        const engine::Face face = static_cast<engine::Face>(fi);
        engine::ChunkCoord neighbor_chunk{};
        glm::ivec3 neighbor_section_coord{};
        engine::neighbor_chunk_and_section(coord, section_coord, face, neighbor_chunk, neighbor_section_coord);

        engine::Chunk* neighbor = store.try_get(neighbor_chunk);
        if (neighbor == nullptr) {
            neighbor = store.allocate(neighbor_chunk);
        }
        REQUIRE(neighbor != nullptr);
        fill_section_stone(neighbor->section_at(neighbor_section_coord));
    }
}

} // namespace

TEST_CASE("section_fully_occluded false when center not opaque full") {
    flecs::world world;
    world.import<engine::WorldModule>();

    engine::ChunkStore store;
    store.init(4);
    engine::Chunk* chunk = store.allocate({0, 0, 0});
    REQUIRE(chunk != nullptr);

    const engine::BlockState stone = engine::make_block_state(engine::BLOCK_STONE, 0);
    for (int z = 0; z < engine::SECTION_DIM; ++z) {
        for (int x = 0; x < engine::SECTION_DIM; ++x) {
            REQUIRE(chunk->section_at({0, 0, 0}).write_block(x, 0, z, stone));
        }
    }
    chunk->section_at({0, 0, 0}).sync_occupancy_from_blocks();
    REQUIRE(engine::face_solid(chunk->section_at({0, 0, 0}).render_meta, engine::Face::NY));

    engine::JobSystem jobs;
    jobs.init({.worker_threads = 1, .io_threads = 1, .meshing_threads = 1});

    engine::StreamingTerrainSystem streaming;
    streaming.init(world, store, jobs, engine::WorldConfig{});
    streaming.store_ = &store;

    REQUIRE_FALSE(streaming.section_fully_occluded({0, 0, 0}, 0));

    jobs.shutdown();
}

TEST_CASE("section_fully_occluded true when buried by solid neighbors") {
    flecs::world world;
    world.import<engine::WorldModule>();

    engine::ChunkStore store;
    store.init(16);
    REQUIRE(store.allocate({0, 0, 0}) != nullptr);
    bury_section_in_stone_shell(store, {0, 0, 0}, 0);

    engine::JobSystem jobs;
    jobs.init({.worker_threads = 1, .io_threads = 1, .meshing_threads = 1});

    engine::StreamingTerrainSystem streaming;
    streaming.init(world, store, jobs, engine::WorldConfig{});
    streaming.store_ = &store;

    REQUIRE(streaming.section_fully_occluded({0, 0, 0}, 0));

    jobs.shutdown();
}

TEST_CASE("schedule_section_mesh skips empty section with empty_skip") {
    flecs::world world;
    world.import<engine::WorldModule>();

    engine::ChunkStore store;
    store.init(4);
    REQUIRE(store.allocate({0, 0, 0}) != nullptr);

    engine::JobSystem jobs;
    jobs.init({.worker_threads = 1, .io_threads = 1, .meshing_threads = 1});

    engine::StreamingTerrainSystem streaming;
    streaming.init(world, store, jobs, engine::WorldConfig{});

    streaming.schedule_section_mesh({0, 0, 0}, 0);

    engine::StreamingTerrainSystem::SectionMeshState& section_state =
        streaming.chunk_meshes_.at({0, 0, 0}).sections[0];
    REQUIRE(section_state.mesh_ready);
    REQUIRE(section_state.empty_skip);
    REQUIRE_FALSE(section_state.occluded_skip);
    REQUIRE(streaming.count_pending_mesh_jobs() == 0);

    jobs.shutdown();
}

TEST_CASE("schedule_section_mesh skips buried section with occluded_skip") {
    flecs::world world;
    world.import<engine::WorldModule>();

    engine::ChunkStore store;
    store.init(16);
    REQUIRE(store.allocate({0, 0, 0}) != nullptr);
    bury_section_in_stone_shell(store, {0, 0, 0}, 0);

    engine::JobSystem jobs;
    jobs.init({.worker_threads = 1, .io_threads = 1, .meshing_threads = 1});

    engine::StreamingTerrainSystem streaming;
    streaming.init(world, store, jobs, engine::WorldConfig{});

    streaming.schedule_section_mesh({0, 0, 0}, 0);

    engine::StreamingTerrainSystem::SectionMeshState& section_state =
        streaming.chunk_meshes_.at({0, 0, 0}).sections[0];
    REQUIRE(section_state.mesh_ready);
    REQUIRE(section_state.occluded_skip);
    REQUIRE_FALSE(section_state.empty_skip);
    REQUIRE(streaming.count_pending_mesh_jobs() == 0);

    jobs.shutdown();
}