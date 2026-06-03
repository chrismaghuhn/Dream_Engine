#include <catch2/catch_test_macros.hpp>

#include "engine/gameplay/BlockState.hpp"
#include "engine/world/Section.hpp"
#include "engine/world/SectionIndexing.hpp"

TEST_CASE("palette rejects 4097th unique state") {
    engine::Section section;

    for (int i = 1; i < static_cast<int>(engine::Section::kMaxPaletteEntries); ++i) {
        const engine::BlockState state = engine::make_block_state(engine::BlockId(i & 0xFFF), 0);
        const int x = i % engine::SECTION_DIM;
        const int z = (i / engine::SECTION_DIM) % engine::SECTION_DIM;
        const int y = i / (engine::SECTION_DIM * engine::SECTION_DIM);
        REQUIRE(section.write_block(x, y, z, state));
    }

    REQUIRE(section.palette.size() == engine::Section::kMaxPaletteEntries);

    const engine::BlockState overflow =
        engine::make_block_state(engine::BlockId(0xABC), 0xF);
    REQUIRE_FALSE(section.write_block(0, 0, 0, overflow));
}

TEST_CASE("palette reuses existing state without growth") {
    engine::Section section;
    const engine::BlockState stone = engine::make_block_state(engine::BLOCK_STONE, 0);

    REQUIRE(section.write_block(0, 0, 0, stone));
    const size_t size_after_first = section.palette.size();

    REQUIRE(section.write_block(1, 0, 0, stone));
    REQUIRE(section.palette.size() == size_after_first);
    REQUIRE(section.read_block(0, 0, 0).raw == stone.raw);
    REQUIRE(section.read_block(1, 0, 0).raw == stone.raw);
}
