#include <catch2/catch_test_macros.hpp>

#include "engine/core/JobSystem.hpp"
#include "engine/render/StreamingTerrainSystem.hpp"
#include "engine/world/ChunkLifecycle.hpp"
#include "engine/world/ChunkStore.hpp"
#include "engine/world/WorldConfig.hpp"
#include "engine/world/WorldModule.hpp"

TEST_CASE("EvtChunkLoaded observer schedules terrain mesh jobs") {
    flecs::world world;
    world.import<engine::WorldModule>();

    engine::ChunkStore store;
    store.init(32);

    engine::JobSystem jobs;
    jobs.init({.worker_threads = 2, .io_threads = 1, .meshing_threads = 2});

    const engine::WorldConfig world_config{};
    engine::StreamingTerrainSystem streaming;
    streaming.init(world, store, jobs, world_config);
    streaming.register_observers(world);

    const engine::ChunkCoord coord{0, 2, 0};
    const flecs::entity entity = engine::load_chunk(world, store, coord, world_config);
    REQUIRE(entity.is_alive());

    for (int i = 0; i < 200; ++i) {
        jobs.wait_meshing();
        if (streaming.count_mesh_ready_sections() > 0) {
            break;
        }
    }

    REQUIRE(streaming.count_mesh_ready_sections() > 0);

    jobs.shutdown();
}
