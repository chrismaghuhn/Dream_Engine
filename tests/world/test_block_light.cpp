#include <catch2/catch_test_macros.hpp>

#include "engine/gameplay/BlockRegistry.hpp"
#include "engine/world/BlockLight.hpp"
#include "engine/world/BlockPos.hpp"
#include "engine/world/ChunkLifecycle.hpp"
#include "engine/world/ChunkStore.hpp"
#include "engine/world/SectionIndexing.hpp"
#include "engine/world/WorldConfig.hpp"
#include "engine/world/WorldModule.hpp"

namespace {

uint8_t block_light_at(engine::ChunkStore& store, const engine::BlockPos& pos) {
    const engine::Chunk* chunk = store.try_get(pos.chunk);
    REQUIRE(chunk != nullptr);
    const glm::ivec3 sec = pos.section_coord();
    const glm::ivec3 blk = pos.block_in_section();
    const engine::Section& section = chunk->section_at(sec);
    const int idx = engine::block_index(blk.x, blk.y, blk.z);
    return section.block_light[static_cast<size_t>(idx)];
}

void clear_section_to_air(engine::ChunkStore& store, engine::ChunkCoord chunk, glm::ivec3 section) {
    engine::Chunk* c = store.try_get(chunk);
    REQUIRE(c != nullptr);
    engine::Section& sec = c->section_at(section);
    const engine::BlockState air = engine::make_block_state(engine::BLOCK_AIR, 0);
    for (int y = 0; y < engine::SECTION_DIM; ++y) {
        for (int z = 0; z < engine::SECTION_DIM; ++z) {
            for (int x = 0; x < engine::SECTION_DIM; ++x) {
                REQUIRE(sec.write_block(x, y, z, air));
                sec.occupancy.set(x, y, z, false);
                sec.block_light[static_cast<size_t>(engine::block_index(x, y, z))] = 0;
            }
        }
    }
}

void clear_chunk_to_air(engine::ChunkStore& store, engine::ChunkCoord chunk) {
    for (int sy = 0; sy < engine::CHUNK_SECTIONS_PER_AXIS; ++sy) {
        for (int sz = 0; sz < engine::CHUNK_SECTIONS_PER_AXIS; ++sz) {
            for (int sx = 0; sx < engine::CHUNK_SECTIONS_PER_AXIS; ++sx) {
                clear_section_to_air(store, chunk, {sx, sy, sz});
            }
        }
    }
}

void place_torch(engine::ChunkStore& store, const engine::BlockPos& pos) {
    const engine::BlockState torch = engine::make_block_state(engine::BLOCK_TORCH, 0);
    REQUIRE(store.write_block(pos, torch));
}

} // namespace

TEST_CASE("torch flood radius is deterministic along axis") {
    flecs::world world;
    world.import<engine::WorldModule>();

    engine::ChunkStore store;
    store.init(16);

    const engine::WorldConfig world_config{};
    const engine::ChunkCoord coord{0, 0, 0};
    REQUIRE(engine::load_chunk(world, store, coord, world_config).is_alive());
    clear_chunk_to_air(store, coord);

    const engine::BlockPos torch_pos = engine::BlockPos::from_world_blocks(8, 8, 8);
    place_torch(store, torch_pos);

    REQUIRE(block_light_at(store, torch_pos) == engine::light_emission(engine::BLOCK_TORCH));

    for (int d = 1; d <= engine::light_emission(engine::BLOCK_TORCH); ++d) {
        const engine::BlockPos sample = engine::BlockPos::from_world_blocks(8 + d, 8, 8);
        const uint8_t expected =
            static_cast<uint8_t>(engine::light_emission(engine::BLOCK_TORCH) - d);
        REQUIRE(block_light_at(store, sample) == expected);
    }

    const engine::BlockPos beyond =
        engine::BlockPos::from_world_blocks(8 + engine::light_emission(engine::BLOCK_TORCH) + 1, 8, 8);
    REQUIRE(block_light_at(store, beyond) == 0);
}

TEST_CASE("removed torch invalidates propagated block light") {
    flecs::world world;
    world.import<engine::WorldModule>();

    engine::ChunkStore store;
    store.init(16);

    const engine::WorldConfig world_config{};
    const engine::ChunkCoord coord{0, 0, 0};
    REQUIRE(engine::load_chunk(world, store, coord, world_config).is_alive());
    clear_chunk_to_air(store, coord);

    const engine::BlockPos torch_pos = engine::BlockPos::from_world_blocks(8, 8, 8);
    place_torch(store, torch_pos);

    const engine::BlockPos lit_neighbor = engine::BlockPos::from_world_blocks(9, 8, 8);
    REQUIRE(block_light_at(store, lit_neighbor) > 0);

    const engine::BlockState air = engine::make_block_state(engine::BLOCK_AIR, 0);
    REQUIRE(store.write_block(torch_pos, air));
    REQUIRE(block_light_at(store, torch_pos) == 0);
    REQUIRE(block_light_at(store, lit_neighbor) == 0);
}

TEST_CASE("cross-chunk border torch propagates into neighbor chunk") {
    flecs::world world;
    world.import<engine::WorldModule>();

    engine::ChunkStore store;
    store.init(16);

    const engine::WorldConfig world_config{};
    const engine::ChunkCoord chunk_a{0, 0, 0};
    const engine::ChunkCoord chunk_b{1, 0, 0};
    REQUIRE(engine::load_chunk(world, store, chunk_a, world_config).is_alive());
    REQUIRE(engine::load_chunk(world, store, chunk_b, world_config).is_alive());

    clear_section_to_air(store, chunk_a, {1, 0, 0});
    clear_section_to_air(store, chunk_b, {0, 0, 0});

    const engine::BlockPos torch_pos = engine::BlockPos::from_world_blocks(31, 8, 8);
    place_torch(store, torch_pos);

    const engine::BlockPos neighbor_pos = engine::BlockPos::from_world_blocks(32, 8, 8);
    const uint8_t expected =
        static_cast<uint8_t>(engine::light_emission(engine::BLOCK_TORCH) - 1);
    REQUIRE(block_light_at(store, neighbor_pos) == expected);

    engine::Section& neighbor_section = store.try_get(chunk_b)->section_at({0, 0, 0});
    REQUIRE(neighbor_section.block_light[static_cast<size_t>(engine::block_index(0, 8, 8))]
            == expected);
}
