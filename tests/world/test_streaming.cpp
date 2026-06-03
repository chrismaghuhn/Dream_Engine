#include <catch2/catch_test_macros.hpp>

#include "engine/core/EngineConfig.hpp"
#include "engine/core/HardwareProbe.hpp"
#include "engine/core/math.hpp"
#include "engine/world/ChunkLifecycle.hpp"
#include "engine/world/ChunkStore.hpp"
#include "engine/world/StreamingConfig.hpp"
#include "engine/world/StreamingSystem.hpp"
#include "engine/world/WorldModule.hpp"

namespace {

engine::StreamingConfig test_streaming_config() {
    engine::StreamingConfig config{};
    config.max_loaded_chunks = 512;
    config.horizontal_radius_chunks = 3;
    config.vertical_radius_chunks = 1;
    // Unlimited per-frame budget so all in-disk chunks load in a single call.
    config.max_chunks_load_per_update = 0;
    return config;
}

engine::WorldConfig test_world_config() {
    engine::WorldConfig world{};
    world.chunk_height_min = -4;
    world.chunk_height_max = 8;
    return world;
}

} // namespace

TEST_CASE("chunk inside euclidean disk loads") {
    flecs::world world;
    world.import<engine::WorldModule>();

    engine::ChunkStore store;
    store.init(64);

    const engine::StreamingConfig streaming = test_streaming_config();
    const engine::WorldConfig world_config = test_world_config();
    const engine::ChunkCoord player{0, 0, 0};
    const engine::ChunkCoord inside{2, 0, 1};

    REQUIRE(engine::chunk_in_streaming_set(inside, player, streaming, world_config));

    engine::update_streaming(store, world, streaming, world_config, player);
    REQUIRE(store.try_get(inside) != nullptr);
}

TEST_CASE("chunk outside euclidean disk unloads") {
    flecs::world world;
    world.import<engine::WorldModule>();

    engine::ChunkStore store;
    store.init(64);

    const engine::StreamingConfig streaming = test_streaming_config();
    const engine::WorldConfig world_config = test_world_config();
    const engine::ChunkCoord far_chunk{10, 0, 0};

    engine::load_chunk(world, store, far_chunk, world_config);
    REQUIRE(store.try_get(far_chunk) != nullptr);

    const engine::ChunkCoord player{0, 0, 0};
    REQUIRE_FALSE(engine::chunk_in_streaming_set(far_chunk, player, streaming, world_config));

    engine::update_streaming(store, world, streaming, world_config, player);
    REQUIRE(store.try_get(far_chunk) == nullptr);
}

TEST_CASE("chunk outside disk is not loaded by streaming") {
    flecs::world world;
    world.import<engine::WorldModule>();

    engine::ChunkStore store;
    store.init(64);

    const engine::StreamingConfig streaming = test_streaming_config();
    const engine::WorldConfig world_config = test_world_config();
    const engine::ChunkCoord player{0, 0, 0};
    const engine::ChunkCoord outside{10, 0, -10};

    REQUIRE_FALSE(engine::chunk_in_streaming_set(outside, player, streaming, world_config));

    engine::update_streaming(store, world, streaming, world_config, player);
    REQUIRE(store.try_get(outside) == nullptr);
}

TEST_CASE("streaming uses floor_div for negative world blocks") {
    const engine::WorldPosition pos = engine::WorldPosition::from_world_blocks(-1, -1, -1);
    REQUIRE(pos.chunk == engine::ChunkCoord{-1, -1, -1});

    flecs::world world;
    world.import<engine::WorldModule>();

    engine::ChunkStore store;
    store.init(64);

    const engine::StreamingConfig streaming = test_streaming_config();
    const engine::WorldConfig world_config = test_world_config();

    engine::update_streaming(store, world, streaming, world_config, pos);
    REQUIRE(store.try_get(pos.chunk) != nullptr);
}

TEST_CASE("update_streaming respects max_chunks_load_per_update") {
    flecs::world world;
    world.import<engine::WorldModule>();

    engine::ChunkStore store;
    store.init(20000);

    engine::StreamingConfig streaming{};
    streaming.max_loaded_chunks = 20000;
    streaming.horizontal_radius_chunks = 12;
    streaming.vertical_radius_chunks = 1;
    streaming.max_chunks_load_per_update = 7;

    const engine::WorldConfig world_config = test_world_config();
    const engine::ChunkCoord player{0, 0, 0};

    engine::update_streaming(store, world, streaming, world_config, player);
    REQUIRE(store.loaded_count() == 7);

    engine::update_streaming(store, world, streaming, world_config, player);
    REQUIRE(store.loaded_count() > 7);
}

TEST_CASE("update_streaming prioritizes player height layer when load budget is limited") {
    flecs::world world;
    world.import<engine::WorldModule>();

    engine::ChunkStore store;
    store.init(128);

    engine::StreamingConfig streaming{};
    streaming.max_loaded_chunks = 128;
    streaming.horizontal_radius_chunks = 3;
    streaming.vertical_radius_chunks = 2;
    streaming.max_chunks_load_per_update = 4;

    const engine::WorldConfig world_config = test_world_config();
    const engine::ChunkCoord player{0, 2, 0};

    engine::update_streaming(store, world, streaming, world_config, player);

    REQUIRE(store.try_get(player) != nullptr);
}

TEST_CASE("load_spawn_neighborhood loads 3x3x3 without full streaming radius") {
    flecs::world world;
    world.import<engine::WorldModule>();

    engine::ChunkStore store;
    store.init(64);

    const engine::WorldConfig world_config = test_world_config();
    const engine::ChunkCoord spawn{0, 0, 0};

    engine::load_spawn_neighborhood(store, world, world_config, spawn);
    REQUIRE(store.try_get(spawn) != nullptr);
    REQUIRE(store.try_get(engine::ChunkCoord{1, 0, 0}) != nullptr);
    REQUIRE(store.try_get(engine::ChunkCoord{12, 0, 0}) == nullptr);
}
