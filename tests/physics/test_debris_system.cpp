#include <catch2/catch_test_macros.hpp>

#include "engine/gameplay/BlockInteraction.hpp"
#include "engine/gameplay/BlockRegistry.hpp"
#include "engine/physics/DebrisSystem.hpp"
#include "engine/physics/ObjectLayerMatrix.hpp"
#include "engine/physics/PhysicsSystem.hpp"
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

void break_at(flecs::world& world, engine::ChunkStore& store, const engine::BlockPos& pos) {
    REQUIRE(engine::break_block_at(world, store, pos, 1));
}

engine::DebrisSystem make_debris_system(
    flecs::world& world,
    engine::PhysicsSystem& physics,
    const int max_active,
    const float despawn_radius) {
    engine::DebrisSystem debris;
    debris.init(
        world,
        physics,
        engine::DebrisConfig{
            .max_active_debris = max_active,
            .despawn_radius = despawn_radius,
        });
    return debris;
}

} // namespace

TEST_CASE("debris spawns on block break up to max_active cap", "[physics][debris]") {
    flecs::world world;
    world.import<engine::WorldModule>();

    engine::PhysicsSystem physics;
    REQUIRE(physics.init());
    engine::DebrisSystem debris = make_debris_system(world, physics, 2, 512.f);

    engine::ChunkStore store;
    store.init(16);

    const engine::WorldConfig world_config{};
    const engine::ChunkCoord coord{0, 0, 0};
    REQUIRE(engine::load_chunk(world, store, coord, world_config).is_alive());

    const engine::BlockPos b0 = engine::BlockPos::from_world_blocks(1, 8, 8);
    const engine::BlockPos b1 = engine::BlockPos::from_world_blocks(2, 8, 8);
    const engine::BlockPos b2 = engine::BlockPos::from_world_blocks(3, 8, 8);
    write_solid_block(store, b0);
    write_solid_block(store, b1);
    write_solid_block(store, b2);

    break_at(world, store, b0);
    break_at(world, store, b1);
    REQUIRE(debris.active_count(world) == 2);

    break_at(world, store, b2);
    REQUIRE(debris.active_count(world) == 2);

    physics.shutdown();
}

TEST_CASE("debris despawn when player moves beyond radius", "[physics][debris]") {
    flecs::world world;
    world.import<engine::WorldModule>();

    engine::PhysicsSystem physics;
    REQUIRE(physics.init());
    engine::DebrisSystem debris = make_debris_system(world, physics, 8, 16.f);

    engine::ChunkStore store;
    store.init(16);

    const engine::WorldConfig world_config{};
    const engine::ChunkCoord coord{0, 0, 0};
    REQUIRE(engine::load_chunk(world, store, coord, world_config).is_alive());

    const engine::BlockPos target = engine::BlockPos::from_world_blocks(8, 8, 8);
    write_solid_block(store, target);
    break_at(world, store, target);
    REQUIRE(debris.active_count(world) == 1);

    debris.tick(world, glm::vec3(8.5f, 8.5f, 8.5f));
    REQUIRE(debris.active_count(world) == 1);

    debris.tick(world, glm::vec3(100.f, 8.5f, 8.5f));
    REQUIRE(debris.active_count(world) == 0);

    physics.shutdown();
}

TEST_CASE("debris Jolt bodies use Debris layer and skip debris-debris pairs", "[physics][debris][layers]") {
#if !defined(ENGINE_HAS_JOLT)
    SKIP("Jolt Physics not linked (ENGINE_HAS_JOLT=0)");
#else
    REQUIRE_FALSE(engine::can_collide(engine::ObjectLayer::Debris, engine::ObjectLayer::Debris));

    engine::PhysicsSystem physics;
    REQUIRE(physics.init());

    engine::DebrisBodyHandle first = physics.create_debris_box(glm::vec3(0.5f, 1.5f, 2.5f));
    engine::DebrisBodyHandle second = physics.create_debris_box(glm::vec3(1.5f, 1.5f, 2.5f));
    REQUIRE(first.valid);
    REQUIRE(second.valid);
    REQUIRE(physics.debris_body_layer(first) == engine::ObjectLayer::Debris);
    REQUIRE(physics.debris_body_layer(second) == engine::ObjectLayer::Debris);
    REQUIRE_FALSE(engine::can_collide(physics.debris_body_layer(first), physics.debris_body_layer(second)));

    physics.destroy_debris_body(first);
    physics.destroy_debris_body(second);
    physics.shutdown();
#endif
}

TEST_CASE("debris_config_from_engine uses streaming radius when override is zero", "[physics][debris][config]") {
    engine::EngineConfig config;
    engine::CpuHardware cpu{};
    cpu.ram_bytes = 8ULL * 1024 * 1024 * 1024;
    cpu.physical_cores = 4;
    config.finalize_cpu(cpu);

    const engine::DebrisConfig debris_cfg = engine::debris_config_from_engine(config);
    REQUIRE(debris_cfg.max_active_debris >= 64);
    REQUIRE(debris_cfg.despawn_radius ==
            static_cast<float>(config.streaming().horizontal_radius_chunks * 32));
}
