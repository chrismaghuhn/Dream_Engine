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

    engine::load_chunk(world, store, far_chunk);
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
