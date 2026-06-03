#include <catch2/catch_test_macros.hpp>
#include "engine/gameplay/BlockState.hpp"
#include "engine/world/SectionIndexing.hpp"

TEST_CASE("section_index roundtrip") {
    for (int sx = 0; sx < 2; ++sx)
    for (int sy = 0; sy < 2; ++sy)
    for (int sz = 0; sz < 2; ++sz) {
        int idx = engine::section_index(sx, sy, sz);
        auto sc = engine::section_coord_from_index(idx);
        REQUIRE(sc.x == sx);
        REQUIRE(sc.y == sy);
        REQUIRE(sc.z == sz);
    }
}

TEST_CASE("block_index roundtrip corners") {
    for (int x : {0, 15})
    for (int y : {0, 15})
    for (int z : {0, 15}) {
        int idx = engine::block_index(x, y, z);
        auto bc = engine::block_coord_from_index(idx);
        REQUIRE(bc.x == x);
        REQUIRE(bc.y == y);
        REQUIRE(bc.z == z);
    }
}

TEST_CASE("pack_vertex corner 16") {
    uint32_t packed = engine::pack_vertex(16, 16, 16, engine::Face::PX);
    REQUIRE((packed & engine::POS_MASK) == 16);
    REQUIRE(((packed >> 5) & engine::POS_MASK) == 16);
    REQUIRE(((packed >> 10) & engine::POS_MASK) == 16);
    REQUIRE(((packed >> 15) & 7u) == uint32_t(engine::Face::PX));
}

TEST_CASE("TerrainVertex size") {
    REQUIRE(sizeof(engine::TerrainVertex) == 8);
}

TEST_CASE("BlockState pack unpack roundtrip") {
    engine::BlockState s = engine::make_block_state(engine::BlockId(0xABC), 0x5);
    REQUIRE(engine::block_id(s) == engine::BlockId(0xABC));
    REQUIRE(engine::block_props(s) == 0x5);
    engine::BlockState s2 =
        engine::make_block_state(engine::block_id(s), engine::block_props(s));
    REQUIRE(s2.raw == s.raw);
}
