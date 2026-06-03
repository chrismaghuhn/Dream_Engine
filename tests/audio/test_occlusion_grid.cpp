#include <catch2/catch_test_macros.hpp>

#include "engine/audio/AudioEngine.hpp"
#include "engine/audio/OcclusionGrid.hpp"
#include "engine/core/EngineConfig.hpp"
#include "engine/core/HardwareProbe.hpp"
#include "engine/gameplay/BlockInteraction.hpp"
#include "engine/gameplay/BlockRegistry.hpp"
#include "engine/world/BlockPos.hpp"
#include "engine/world/ChunkLifecycle.hpp"
#include "engine/world/ChunkStore.hpp"
#include "engine/world/WorldConfig.hpp"
#include "engine/world/WorldEvents.hpp"
#include "engine/world/WorldModule.hpp"

TEST_CASE("occlusion grid refreshes solid cells on ChunkDirty") {
    engine::ChunkStore store;
    store.init(16);

    engine::EngineConfig config{};
    config.finalize_cpu(engine::HardwareProbe::run_cpu());

    engine::OcclusionGrid grid;
    grid.init(&store, config.occlusion_grid_radius_chunks());
    grid.set_listener_origin({4, 5, 4});

    flecs::world world;
    world.import<engine::WorldModule>();

    const engine::WorldConfig world_config{};
    const engine::ChunkCoord coord{0, 0, 0};
    REQUIRE(engine::load_chunk(world, store, coord, world_config).is_alive());

    const engine::BlockPos target = engine::BlockPos::from_world_blocks(5, 5, 6);
    const engine::BlockState stone = engine::make_block_state(engine::BLOCK_STONE, 0);
    REQUIRE(store.write_block(target, stone));

    const engine::BlockState air = engine::make_block_state(engine::BLOCK_AIR, 0);
    for (int y = 6; y <= 15; ++y) {
        REQUIRE(store.write_block(engine::BlockPos::from_world_blocks(4, y, 5), air));
    }
    grid.queue_chunk_dirty(coord);
    grid.process_pending();

    const glm::vec3 listener{4.5f, 5.5f, 5.5f};
    const glm::vec3 blocked_source{5.5f, 5.5f, 6.5f};
    const glm::vec3 open_source{4.5f, 14.5f, 5.5f};
    const float blocked_occlusion = grid.occlusion_factor(listener, blocked_source);
    const float open_occlusion = grid.occlusion_factor(listener, open_source);
    REQUIRE(blocked_occlusion > 0.f);
    REQUIRE(open_occlusion < blocked_occlusion);
}

TEST_CASE("AudioEngine registers gameplay event observers") {
    flecs::world world;
    world.import<engine::WorldModule>();

    engine::ChunkStore store;
    store.init(16);

    engine::EngineConfig config{};
    config.finalize_cpu(engine::HardwareProbe::run_cpu());

    engine::AudioEngine audio;
    REQUIRE(audio.init(world, store, config));

    int broken_count = 0;
    world.observer()
        .event<engine::EvtBlockBroken>()
        .with<engine::ChunkCoord>()
        .run([&](flecs::iter& it) {
            while (it.next()) {
                if (it.param<engine::EvtBlockBroken>()) {
                    ++broken_count;
                }
            }
        });

    const engine::WorldConfig world_config{};
    const engine::ChunkCoord coord{0, 0, 0};
    REQUIRE(engine::load_chunk(world, store, coord, world_config).is_alive());

    const engine::BlockPos target = engine::BlockPos::from_world_blocks(8, 8, 8);
    const engine::BlockState stone = engine::make_block_state(engine::BLOCK_STONE, 0);
    REQUIRE(store.write_block(target, stone));
    REQUIRE(engine::break_block_at(world, store, target, 1));
    REQUIRE(broken_count == 1);
}
