#include <catch2/catch_test_macros.hpp>

#include "engine/gameplay/BlockRegistry.hpp"
#include "engine/gameplay/BlockState.hpp"
#include "engine/gameplay/VoxelMovementConfig.hpp"
#include "engine/physics/VoxelCapsuleResolver.hpp"
#include "engine/world/BlockPos.hpp"
#include "engine/world/ChunkStore.hpp"

namespace {

void write_solid(engine::ChunkStore& store, int wx, int wy, int wz) {
    const engine::BlockPos pos = engine::BlockPos::from_world_blocks(wx, wy, wz);
    const engine::BlockState stone = engine::make_block_state(engine::BLOCK_STONE, 0);
    REQUIRE(store.write_block(pos, stone));
}

void allocate_air_shell(engine::ChunkStore& store) {
    for (int dz = -1; dz <= 1; ++dz) {
        for (int dx = -1; dx <= 1; ++dx) {
            REQUIRE(store.allocate(engine::ChunkCoord{dx, 0, dz}) != nullptr);
            REQUIRE(store.allocate(engine::ChunkCoord{dx, -1, dz}) != nullptr);
        }
    }
}

void fill_floor(engine::ChunkStore& store, int y, int min_x, int max_x, int min_z, int max_z) {
    for (int z = min_z; z <= max_z; ++z) {
        for (int x = min_x; x <= max_x; ++x) {
            write_solid(store, x, y, z);
        }
    }
}

engine::Capsule make_capsule(float x, float y, float z) {
    engine::Capsule capsule{};
    capsule.center = {x, y, z};
    capsule.radius = 0.3f;
    capsule.half_height = 0.6f;
    return capsule;
}

} // namespace

TEST_CASE("capsule single drop reaches floor") {
    engine::ChunkStore store;
    store.init(32);
    allocate_air_shell(store);
    fill_floor(store, 0, 0, 12, 0, 12);

    engine::Capsule capsule = make_capsule(6.f, 4.f, 6.f);
    engine::VoxelMovementConfig movement{};
    REQUIRE_FALSE(engine::occupancy_at(6, -1, 6, store));
    REQUIRE_FALSE(engine::capsule_overlaps_solid(capsule, store, movement.skin_width));
    const engine::CapsuleMoveResult moved =
        engine::move_and_slide(capsule, glm::vec3{0.f, -20.f, 0.f}, 1.f, store, movement, 0.1f);

    const glm::vec3 half = engine::capsule_half_extents(capsule, movement.skin_width);
    REQUIRE(moved.center.y < 1.f + half.y + 0.2f);
    REQUIRE(moved.center.y > 1.f + half.y - 0.2f);
}

TEST_CASE("capsule falls onto ground and stops") {
    engine::ChunkStore store;
    store.init(32);
    allocate_air_shell(store);
    fill_floor(store, 0, 0, 12, 0, 12);

    engine::Capsule capsule = make_capsule(6.f, 4.f, 6.f);
    engine::VoxelMovementConfig movement{};
    REQUIRE_FALSE(engine::capsule_overlaps_solid(capsule, store, movement.skin_width));
    glm::vec3 velocity{0.f, 0.f, 0.f};
    const float dt = 1.f / 60.f;
    const float gravity = -24.f;
    for (int i = 0; i < 90; ++i) {
        velocity.y += gravity * dt;
    }
    const engine::CapsuleMoveResult moved =
        engine::move_and_slide(capsule, velocity, 0.25f, store, movement, 0.1f);
    capsule.center = moved.center;
    velocity = moved.velocity;

    REQUIRE_FALSE(engine::capsule_overlaps_solid(capsule, store, movement.skin_width));
    const glm::vec3 half = engine::capsule_half_extents(capsule, movement.skin_width);
    const float expected_rest_y = 1.f + half.y;
    REQUIRE(capsule.center.y > expected_rest_y - 0.15f);
    REQUIRE(capsule.center.y < expected_rest_y + 0.15f);
    REQUIRE(std::abs(velocity.y) < 0.05f);
}

TEST_CASE("capsule cannot tunnel through one block wall") {
    engine::ChunkStore store;
    store.init(32);
    allocate_air_shell(store);
    fill_floor(store, 0, 0, 12, 0, 12);
    write_solid(store, 6, 1, 6);
    write_solid(store, 6, 2, 6);
    write_solid(store, 6, 3, 6);

    engine::Capsule capsule = make_capsule(4.f, 2.f, 6.f);
    engine::VoxelMovementConfig movement{};
    glm::vec3 velocity{20.f, 0.f, 0.f};

    const engine::CapsuleMoveResult moved =
        engine::move_and_slide(capsule, velocity, 1.f / 30.f, store, movement, 0.1f);

    REQUIRE(moved.center.x < 5.7f);
}

TEST_CASE("capsule step-up within step_height") {
    engine::ChunkStore store;
    store.init(32);
    allocate_air_shell(store);
    fill_floor(store, 0, 0, 12, 0, 12);
    write_solid(store, 5, 1, 6);

    engine::Capsule capsule = make_capsule(4.f, 2.f, 6.f);
    engine::VoxelMovementConfig movement{};
    movement.step_height = 1.0f;
    glm::vec3 velocity{3.f, 0.f, 0.f};

    const float start_y = capsule.center.y;
    for (int i = 0; i < 24; ++i) {
        const engine::CapsuleMoveResult moved =
            engine::move_and_slide(capsule, velocity, 1.f / 60.f, store, movement, 0.1f);
        capsule.center = moved.center;
        velocity = moved.velocity;
    }

    REQUIRE(capsule.center.x > 5.2f);
    REQUIRE(capsule.center.y >= start_y - 0.05f);
    REQUIRE(capsule.center.y <= start_y + movement.step_height + 0.05f);
}

TEST_CASE("SolidIfChunkMissing at streaming edge is solid") {
    engine::ChunkStore store;
    const int wx = 64;
    const int wy = 0;
    const int wz = 0;

    REQUIRE(engine::occupancy_at(wx, wy, wz, store, engine::OccupancyPolicy::SolidIfChunkMissing));
    REQUIRE_FALSE(engine::occupancy_at(wx, wy, wz, store, engine::OccupancyPolicy::AirIfChunkMissing));
}
