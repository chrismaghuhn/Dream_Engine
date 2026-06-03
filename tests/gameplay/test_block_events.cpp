#include <catch2/catch_test_macros.hpp>

#include "engine/gameplay/BlockInteraction.hpp"
#include "engine/gameplay/BlockRegistry.hpp"
#include "engine/world/BlockPos.hpp"
#include "engine/world/ChunkLifecycle.hpp"
#include "engine/world/ChunkStore.hpp"
#include "engine/world/WorldConfig.hpp"
#include "engine/world/WorldEvents.hpp"
#include "engine/world/WorldModule.hpp"

namespace {

void write_solid_block(engine::ChunkStore& store, const engine::BlockPos& pos) {
    const engine::BlockState stone = engine::make_block_state(engine::BLOCK_STONE, 0);
    REQUIRE(store.write_block(pos, stone));
}

} // namespace

TEST_CASE("break emits exactly one EvtBlockBroken") {
    flecs::world world;
    world.import<engine::WorldModule>();

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

    engine::ChunkStore store;
    store.init(16);

    const engine::WorldConfig world_config{};
    const engine::ChunkCoord coord{0, 0, 0};
    REQUIRE(engine::load_chunk(world, store, coord, world_config).is_alive());

    const engine::BlockPos target = engine::BlockPos::from_world_blocks(8, 8, 8);
    write_solid_block(store, target);

    REQUIRE(engine::break_block_at(world, store, target, 1));
    REQUIRE(broken_count == 1);
}

TEST_CASE("place emits exactly one EvtBlockPlaced") {
    flecs::world world;
    world.import<engine::WorldModule>();

    int placed_count = 0;
    world.observer()
        .event<engine::EvtBlockPlaced>()
        .with<engine::ChunkCoord>()
        .run([&](flecs::iter& it) {
            while (it.next()) {
                if (it.param<engine::EvtBlockPlaced>()) {
                    ++placed_count;
                }
            }
        });

    engine::ChunkStore store;
    store.init(16);

    const engine::WorldConfig world_config{};
    const engine::ChunkCoord coord{0, 0, 0};
    REQUIRE(engine::load_chunk(world, store, coord, world_config).is_alive());

    const engine::BlockPos target = engine::BlockPos::from_world_blocks(4, 5, 6);
    const engine::BlockState air = engine::make_block_state(engine::BLOCK_AIR, 0);
    REQUIRE(store.write_block(target, air));
    REQUIRE(engine::place_block_at(world, store, target, engine::BLOCK_STONE, 1));
    REQUIRE(placed_count == 1);
}

TEST_CASE("raycast reaches diagonal block within reach") {
    engine::ChunkStore store;
    store.init(4);
    REQUIRE(store.allocate(engine::ChunkCoord{0, 0, 0}) != nullptr);

    const engine::BlockPos target = engine::BlockPos::from_world_blocks(4, 8, 4);
    REQUIRE(store.write_block(target, engine::make_block_state(engine::BLOCK_STONE, 0)));

    engine::Camera camera{};
    camera.position = glm::vec3{0.5f, 8.5f, 0.5f};
    camera.yaw = 45.f;
    camera.pitch = 0.f;

    const auto hit = engine::raycast_blocks(camera, store, 6.f);

    REQUIRE(hit.has_value());
    REQUIRE(hit->block.to_world_blocks() == target.to_world_blocks());
}

TEST_CASE("break_block_at adds ChunkDirty to chunk entity") {
    flecs::world world;
    world.import<engine::WorldModule>();

    engine::ChunkStore store;
    store.init(16);

    const engine::WorldConfig world_config{};
    const engine::ChunkCoord coord{0, 0, 0};
    flecs::entity chunk_entity = engine::load_chunk(world, store, coord, world_config);
    REQUIRE(chunk_entity.is_alive());

    const engine::BlockPos target = engine::BlockPos::from_world_blocks(8, 8, 8);
    const engine::BlockState stone = engine::make_block_state(engine::BLOCK_STONE, 0);
    REQUIRE(store.write_block(target, stone));

    REQUIRE_FALSE(chunk_entity.has<engine::ChunkDirty>());
    REQUIRE(engine::break_block_at(world, store, target, 1));
    REQUIRE(chunk_entity.has<engine::ChunkDirty>());
}

TEST_CASE("break_block_at creates missing chunk entity before marking dirty") {
    flecs::world world;
    world.import<engine::WorldModule>();

    engine::ChunkStore store;
    store.init(16);

    const engine::ChunkCoord coord{0, 0, 0};
    REQUIRE(store.allocate(coord) != nullptr);

    const engine::BlockPos target = engine::BlockPos::from_world_blocks(8, 8, 8);
    const engine::BlockState stone = engine::make_block_state(engine::BLOCK_STONE, 0);
    REQUIRE(store.write_block(target, stone));
    REQUIRE(store.entity_for(coord) == 0);

    REQUIRE(engine::break_block_at(world, store, target, 1));

    const uint64_t entity_id = store.entity_for(coord);
    REQUIRE(entity_id != 0);
    flecs::entity chunk_entity(world, entity_id);
    REQUIRE(chunk_entity.is_alive());
    REQUIRE(chunk_entity.has<engine::ChunkDirty>());
}

TEST_CASE("ChunkDirty removal allows re-trigger on subsequent block mutation") {
    // Simulates the StreamingTerrainSystem observer that removes ChunkDirty after
    // scheduling a remesh. Verifies the tag is added again on the next mutation.
    flecs::world world;
    world.import<engine::WorldModule>();

    int dirty_count = 0;
    world.observer<engine::ChunkDirty>()
        .event(flecs::OnAdd)
        .each([&](flecs::entity entity, engine::ChunkDirty) {
            ++dirty_count;
            entity.remove<engine::ChunkDirty>();
        });

    engine::ChunkStore store;
    store.init(16);

    const engine::WorldConfig world_config{};
    const engine::ChunkCoord coord{0, 0, 0};
    REQUIRE(engine::load_chunk(world, store, coord, world_config).is_alive());

    const engine::BlockPos pos1 = engine::BlockPos::from_world_blocks(4, 4, 4);
    const engine::BlockPos pos2 = engine::BlockPos::from_world_blocks(5, 4, 4);

    REQUIRE(store.write_block(pos1, engine::make_block_state(engine::BLOCK_STONE, 0)));
    REQUIRE(store.write_block(pos2, engine::make_block_state(engine::BLOCK_STONE, 0)));

    REQUIRE(engine::break_block_at(world, store, pos1, 1));
    REQUIRE(dirty_count == 1);

    REQUIRE(engine::break_block_at(world, store, pos2, 2));
    REQUIRE(dirty_count == 2);
}

TEST_CASE("stale BlockMutation is rejected") {
    flecs::world world;
    world.import<engine::WorldModule>();

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

    engine::ChunkStore store;
    store.init(16);

    const engine::WorldConfig world_config{};
    const engine::ChunkCoord coord{0, 0, 0};
    REQUIRE(engine::load_chunk(world, store, coord, world_config).is_alive());

    const engine::BlockPos target = engine::BlockPos::from_world_blocks(2, 3, 4);
    write_solid_block(store, target);

    const engine::BlockState wrong_old = engine::make_block_state(engine::BLOCK_DIRT, 0);
    const engine::BlockMutation mutation{
        target,
        wrong_old,
        engine::make_block_state(engine::BLOCK_AIR, 0),
        0,
        1,
    };

    REQUIRE_FALSE(engine::apply_block_mutation(world, store, mutation).applied);
    REQUIRE(store.read_block(target).raw == engine::make_block_state(engine::BLOCK_STONE, 0).raw);
    REQUIRE(broken_count == 0);
}
