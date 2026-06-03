#include <catch2/catch_test_macros.hpp>

#include "engine/gameplay/BlockRegistry.hpp"
#include "engine/procgen/TerrainGraph.hpp"
#include "engine/world/Chunk.hpp"

TEST_CASE("terrain graph deterministic for seed") {
    constexpr uint64_t seed = 42;
    constexpr int sea_level = 64;
    const engine::ChunkCoord coord{1, 0, -2};

    engine::TerrainGraph first(seed, sea_level);
    engine::TerrainGraph second(seed, sea_level);

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

TEST_CASE("terrain graph marks generated unmodified") {
    engine::TerrainGraph terrain(42, 64);

    engine::Chunk chunk{};
    chunk.coord = {0, 0, 0};
    chunk.flags = engine::CHUNK_MODIFIED_BY_PLAYER;

    terrain.fill_chunk(chunk);

    REQUIRE((chunk.flags & engine::CHUNK_GENERATED) != 0);
    REQUIRE((chunk.flags & engine::CHUNK_MODIFIED_BY_PLAYER) == 0);
}

TEST_CASE("terrain graph uses sea level for water") {
    constexpr int sea_level = 64;
    engine::TerrainGraph terrain(42, sea_level);

    bool found_submerged_column = false;
    for (int wx = -128; wx <= 128 && !found_submerged_column; wx += 16) {
        for (int wz = -128; wz <= 128 && !found_submerged_column; wz += 16) {
            if (terrain.surface_height_at(wx, wz) >= sea_level) {
                continue;
            }
            const engine::BlockState water = terrain.block_at_world(wx, sea_level, wz);
            REQUIRE(engine::block_id(water) == engine::BLOCK_WATER);
            found_submerged_column = true;
        }
    }
    REQUIRE(found_submerged_column);

    const engine::BlockState air_above = terrain.block_at_world(0, sea_level + 50, 0);
    REQUIRE(engine::block_id(air_above) == engine::BLOCK_AIR);
}

TEST_CASE("terrain graph assigns biomes") {
    engine::TerrainGraph terrain(100, 64);

    const engine::Biome first = terrain.biome_at(128, 128);
    const engine::Biome second = terrain.biome_at(128, 128);
    const engine::Biome distant = terrain.biome_at(900, -400);

    REQUIRE(first == second);
    REQUIRE(static_cast<int>(first) >= 0);
    REQUIRE(static_cast<int>(distant) >= 0);
}
