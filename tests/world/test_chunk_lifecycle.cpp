#include <catch2/catch_test_macros.hpp>

#include "engine/world/ChunkLifecycle.hpp"
#include "engine/world/ChunkStore.hpp"
#include "engine/world/WorldEvents.hpp"
#include "engine/world/WorldModule.hpp"

TEST_CASE("load_chunk creates entity with valid ChunkSlotRef") {
    flecs::world world;
    world.import<engine::WorldModule>();

    engine::ChunkStore store;
    const engine::ChunkCoord coord{1, -2, 3};

    const flecs::entity chunk_entity = engine::load_chunk(world, store, coord);
    REQUIRE(chunk_entity.is_alive());
    REQUIRE(store.try_get(coord) != nullptr);
    REQUIRE(store.entity_for(coord) == chunk_entity.id());

    const engine::ChunkCoord* entity_coord = chunk_entity.get<engine::ChunkCoord>();
    REQUIRE(entity_coord != nullptr);
    REQUIRE(*entity_coord == coord);

    const engine::ChunkSlotRef* slot_ref = chunk_entity.get<engine::ChunkSlotRef>();
    REQUIRE(slot_ref != nullptr);
    REQUIRE(store.validate_slot_ref(*slot_ref));
    REQUIRE(store.try_get_via_ref(*slot_ref) == store.try_get(coord));
}

TEST_CASE("unload_chunk bumps generation and rejects stale ChunkSlotRef") {
    flecs::world world;
    world.import<engine::WorldModule>();

    engine::ChunkStore store;
    const engine::ChunkCoord coord{0, 0, 0};

    const flecs::entity chunk_entity = engine::load_chunk(world, store, coord);
    const engine::ChunkSlotRef stale_ref = *chunk_entity.get<engine::ChunkSlotRef>();
    REQUIRE(store.validate_slot_ref(stale_ref));

    engine::unload_chunk(world, store, coord);

    REQUIRE_FALSE(chunk_entity.is_alive());
    REQUIRE(store.try_get(coord) == nullptr);
    REQUIRE_FALSE(store.validate_slot_ref(stale_ref));
    REQUIRE(store.try_get_via_ref(stale_ref) == nullptr);
    REQUIRE(store.entity_for(coord) == 0);
}

TEST_CASE("unload_chunk sets pending_unload before entity delete") {
    flecs::world world;
    world.import<engine::WorldModule>();

    engine::ChunkStore store;
    const engine::ChunkCoord coord{4, 0, -1};
    bool observed_pending = false;

    world.observer<engine::ChunkSlotRef>()
        .event(flecs::OnRemove)
        .each([&](flecs::entity, engine::ChunkSlotRef) {
            observed_pending = store.is_pending_unload(coord);
        });

    engine::load_chunk(world, store, coord);
    REQUIRE_FALSE(store.is_pending_unload(coord));

    engine::unload_chunk(world, store, coord);

    REQUIRE(observed_pending);
    REQUIRE_FALSE(store.is_pending_unload(coord));
}

TEST_CASE("reload after unload assigns new generation") {
    flecs::world world;
    world.import<engine::WorldModule>();

    engine::ChunkStore store;
    const engine::ChunkCoord coord{2, 0, 0};

    const flecs::entity first = engine::load_chunk(world, store, coord);
    const engine::ChunkSlotRef first_ref = *first.get<engine::ChunkSlotRef>();

    engine::unload_chunk(world, store, coord);

    const flecs::entity second = engine::load_chunk(world, store, coord);
    const engine::ChunkSlotRef second_ref = *second.get<engine::ChunkSlotRef>();

    REQUIRE(second_ref.slot_id == first_ref.slot_id);
    REQUIRE(second_ref.generation > first_ref.generation);
    REQUIRE(store.validate_slot_ref(second_ref));
    REQUIRE_FALSE(store.validate_slot_ref(first_ref));
}

TEST_CASE("ChunkDirty observer fires on chunk entity") {
    flecs::world world;
    world.import<engine::WorldModule>();

    engine::ChunkStore store;
    const engine::ChunkCoord coord{0, 1, 0};
    const flecs::entity chunk_entity = engine::load_chunk(world, store, coord);
    REQUIRE(chunk_entity.is_alive());

    chunk_entity.add<engine::ChunkDirty>();
    REQUIRE(chunk_entity.has<engine::ChunkDirty>());
}
