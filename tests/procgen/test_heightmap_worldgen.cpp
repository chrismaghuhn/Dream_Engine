#include <catch2/catch_test_macros.hpp>

#include "engine/gameplay/BlockRegistry.hpp"
#include "engine/procgen/HeightmapWorldgen.hpp"
#include "engine/world/Chunk.hpp"

TEST_CASE("heightmap worldgen deterministic for seed") {
    constexpr uint64_t seed = 42;
    constexpr int sea_level = 64;
    const engine::ChunkCoord coord{1, 0, -2};

    engine::HeightmapWorldgen first(seed, sea_level);
    engine::HeightmapWorldgen second(seed, sea_level);

    engine::Chunk chunk_a{};
    chunk_a.coord = coord;
    engine::Chunk chunk_b{};
    chunk_b.coord = coord;

    first.fill_chunk(chunk_a);
    second.fill_chunk(chunk_b);

    for (int ly = 0; ly < 32; ++ly) {
        for (int lz = 0; lz < 32; ++lz) {
            for (int lx = 0; lx < 32; ++lx) {
                const glm::ivec3 sec = { lx >> 4, ly >> 4, lz >> 4 };
                const glm::ivec3 blk = { lx & 15, ly & 15, lz & 15 };
                REQUIRE(
                    chunk_a.section_at(sec).read_block(blk.x, blk.y, blk.z).raw
                    == chunk_b.section_at(sec).read_block(blk.x, blk.y, blk.z).raw);
            }
        }
    }

    const int wx = coord.x * 32 + 5;
    const int wy = sea_level + 3;
    const int wz = coord.z * 32 + 7;
    REQUIRE(first.block_at_world(wx, wy, wz).raw == second.block_at_world(wx, wy, wz).raw);
}

TEST_CASE("heightmap worldgen marks generated unmodified") {
    engine::HeightmapWorldgen worldgen(engine::kDefaultWorldSeed, 64);

    engine::Chunk chunk{};
    chunk.coord = {0, 0, 0};
    chunk.flags = engine::CHUNK_MODIFIED_BY_PLAYER;

    worldgen.fill_chunk(chunk);

    REQUIRE((chunk.flags & engine::CHUNK_GENERATED) != 0);
    REQUIRE((chunk.flags & engine::CHUNK_MODIFIED_BY_PLAYER) == 0);
}

TEST_CASE("heightmap worldgen uses sea level for water") {
    constexpr int sea_level = 64;
    engine::HeightmapWorldgen worldgen(42, sea_level);

    const engine::BlockState water = worldgen.block_at_world(0, sea_level, 0);
    REQUIRE(engine::block_id(water) == engine::BLOCK_WATER);

    const engine::BlockState air_above = worldgen.block_at_world(0, sea_level + 50, 0);
    REQUIRE(engine::block_id(air_above) == engine::BLOCK_AIR);
}
