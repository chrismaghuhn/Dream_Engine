#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "engine/gameplay/BlockRegistry.hpp"
#include "engine/gameplay/BlockState.hpp"
#include "engine/world/ChunkImpostor.hpp"
#include "engine/world/Section.hpp"
#include "engine/world/SectionIndexing.hpp"

namespace {

void fill_section_stone(engine::Section& section) {
    const engine::BlockState stone = engine::make_block_state(engine::BLOCK_STONE, 0);
    for (int y = 0; y < engine::SECTION_DIM; ++y) {
        for (int z = 0; z < engine::SECTION_DIM; ++z) {
            for (int x = 0; x < engine::SECTION_DIM; ++x) {
                REQUIRE(section.write_block(x, y, z, stone));
            }
        }
    }
    section.sync_occupancy_from_blocks();
}

} // namespace

TEST_CASE("chunk impostor stone chunk valid dominant stone color min max y") {
    engine::Chunk chunk{};
    fill_section_stone(chunk.section_at({0, 0, 0}));

    const engine::ChunkImpostorSummary summary = engine::compute_chunk_impostor(chunk);

    REQUIRE(summary.valid);
    REQUIRE(summary.min_y == 0.f);
    REQUIRE(summary.max_y == 15.f);
    REQUIRE(summary.color.x == Catch::Approx(0.45f));
    REQUIRE(summary.color.y == Catch::Approx(0.45f));
    REQUIRE(summary.color.z == Catch::Approx(0.48f));
}

TEST_CASE("chunk impostor air chunk invalid") {
    engine::Chunk chunk{};
    const engine::ChunkImpostorSummary summary = engine::compute_chunk_impostor(chunk);
    REQUIRE_FALSE(summary.valid);
}