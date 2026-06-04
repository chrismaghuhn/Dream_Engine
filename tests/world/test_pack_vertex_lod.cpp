#include <catch2/catch_test_macros.hpp>

#include "engine/world/SectionIndexing.hpp"

TEST_CASE("pack_vertex LOD1 coarse corner 16 is representable") {
    const uint32_t packed = engine::pack_vertex(16, 16, 16, engine::Face::PX);
    REQUIRE((packed & engine::POS_MASK) == 16u);
    REQUIRE(((packed >> 5) & engine::POS_MASK) == 16u);
    REQUIRE(((packed >> 10) & engine::POS_MASK) == 16u);
}

TEST_CASE("pack_vertex 32 wraps to zero and is forbidden for LOD1") {
    const uint32_t packed = engine::pack_vertex(32, 0, 0, engine::Face::PX);
    REQUIRE((packed & engine::POS_MASK) == 0u);
}