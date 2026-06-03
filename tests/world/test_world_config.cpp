#include <catch2/catch_test_macros.hpp>

#include "engine/world/WorldConfig.hpp"

TEST_CASE("WorldConfig defaults") {
    engine::WorldConfig w;
    REQUIRE(w.chunk_height_min == -4);
    REQUIRE(w.chunk_height_max == 8);
    REQUIRE(w.finite_bounds == false);
    REQUIRE(w.sea_level == 64);
    REQUIRE(w.rebase_radius == 512.f);
}
