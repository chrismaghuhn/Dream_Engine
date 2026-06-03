#include <catch2/catch_test_macros.hpp>

#include "engine/world/WorldEvents.hpp"
#include "engine/world/WorldModule.hpp"

TEST_CASE("WorldModule registers world event types") {
    flecs::world world;
    world.import<engine::WorldModule>();

    REQUIRE(world.component<engine::EvtChunkDirty>().id() != 0);
    REQUIRE(world.component<engine::EvtOriginShift>().id() != 0);
    REQUIRE(world.component<engine::ChunkDirty>().id() != 0);
    REQUIRE(world.component<engine::ChunkSlotRef>().id() != 0);
    REQUIRE(world.component<engine::ChunkCoord>().id() != 0);
}
