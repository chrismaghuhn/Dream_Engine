#include <catch2/catch_test_macros.hpp>

#include "engine/gameplay/BlockRegistry.hpp"
#include "engine/gameplay/BlockState.hpp"
#include "engine/world/BlockPos.hpp"
#include "engine/world/ChunkStore.hpp"

#include <glm/glm.hpp>

TEST_CASE("read write block roundtrip") {
    engine::ChunkStore store;
    engine::Chunk* chunk = store.allocate(engine::ChunkCoord{0, 0, 0});
    REQUIRE(chunk != nullptr);

    const engine::BlockPos pos = engine::BlockPos::from_world_blocks(5, 10, 15);
    const engine::BlockState stone = engine::make_block_state(engine::BLOCK_STONE, 0);

    REQUIRE(store.write_block(pos, stone));
    REQUIRE(store.read_block(pos).raw == stone.raw);
}

TEST_CASE("write_block updates occupancy on solid air change") {
    engine::ChunkStore store;
    engine::Chunk* chunk = store.allocate(engine::ChunkCoord{0, 0, 0});
    REQUIRE(chunk != nullptr);

    const engine::BlockPos pos = engine::BlockPos::from_world_blocks(3, 4, 5);
    const int wx = pos.to_world_blocks().x;
    const int wy = pos.to_world_blocks().y;
    const int wz = pos.to_world_blocks().z;

    REQUIRE_FALSE(engine::occupancy_at(wx, wy, wz, store));

    const engine::BlockState stone = engine::make_block_state(engine::BLOCK_STONE, 0);
    REQUIRE(store.write_block(pos, stone));
    REQUIRE(engine::occupancy_at(wx, wy, wz, store));

    const engine::BlockState air = engine::make_block_state(engine::BLOCK_AIR, 0);
    REQUIRE(store.write_block(pos, air));
    REQUIRE_FALSE(engine::occupancy_at(wx, wy, wz, store));
}

TEST_CASE("occupancy_at cross chunk boundary") {
    engine::ChunkStore store;
    engine::Chunk* chunk = store.allocate(engine::ChunkCoord{0, 0, 0});
    REQUIRE(chunk != nullptr);
    engine::Chunk* neighbor = store.allocate(engine::ChunkCoord{1, 0, 0});
    REQUIRE(neighbor != nullptr);

    const engine::BlockPos boundary_pos = engine::BlockPos::from_world_blocks(31, 8, 8);
    const glm::ivec3 world = boundary_pos.to_world_blocks();

    const engine::BlockState stone = engine::make_block_state(engine::BLOCK_STONE, 0);
    REQUIRE(store.write_block(boundary_pos, stone));
    REQUIRE(engine::occupancy_at(world.x, world.y, world.z, store));

    const engine::BlockPos neighbor_pos = engine::BlockPos::from_world_blocks(32, 8, 8);
    const glm::ivec3 neighbor_world = neighbor_pos.to_world_blocks();
    REQUIRE_FALSE(engine::occupancy_at(neighbor_world.x, neighbor_world.y, neighbor_world.z, store));
}

TEST_CASE("occupancy_at SolidIfChunkMissing when chunk missing") {
    engine::ChunkStore store;

    REQUIRE(engine::occupancy_at(0, 0, 0, store, engine::OccupancyPolicy::SolidIfChunkMissing));
    REQUIRE_FALSE(engine::occupancy_at(0, 0, 0, store, engine::OccupancyPolicy::AirIfChunkMissing));
}

TEST_CASE("write_block fails when chunk not loaded") {
    engine::ChunkStore store;
    const engine::BlockPos pos = engine::BlockPos::from_world_blocks(0, 0, 0);
    const engine::BlockState stone = engine::make_block_state(engine::BLOCK_STONE, 0);
    REQUIRE_FALSE(store.write_block(pos, stone));
}

TEST_CASE("read_block returns air when chunk missing") {
    engine::ChunkStore store;
    const engine::BlockPos pos = engine::BlockPos::from_world_blocks(0, 0, 0);
    REQUIRE(engine::block_id(store.read_block(pos)) == engine::BLOCK_AIR);
}

TEST_CASE("ChunkStore allocate and free") {
    engine::ChunkStore store;
    const engine::ChunkCoord coord{2, -1, 0};

    engine::Chunk* chunk = store.allocate(coord);
    REQUIRE(chunk != nullptr);
    REQUIRE(store.try_get(coord) == chunk);

    store.free(coord);
    REQUIRE(store.try_get(coord) == nullptr);
}
