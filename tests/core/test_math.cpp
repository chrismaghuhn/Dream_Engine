#include <catch2/catch_test_macros.hpp>
#include "engine/core/math.hpp"

TEST_CASE("floor_div negative") {
    REQUIRE(engine::floor_div(-1, 8) == -1);
    REQUIRE(engine::floor_div(-32, 32) == -1);
}
TEST_CASE("positive_mod negative") {
    REQUIRE(engine::positive_mod(-1, 8) == 7);
    REQUIRE(engine::positive_mod(-32, 32) == 0);
}
TEST_CASE("block_to_chunk boundaries") {
    auto c = engine::block_to_chunk(31, 0, 31);
    REQUIRE(c.x == 0);
    auto c2 = engine::block_to_chunk(-1, -1, -1);
    REQUIRE(c2.x == -1);
}
TEST_CASE("block_to_chunk all axes negative boundary") {
    auto c = engine::block_to_chunk(-1, -32, 32);
    REQUIRE(c.x == -1);
    REQUIRE(c.y == -1);
    REQUIRE(c.z == 1);
}
TEST_CASE("block_local_in_chunk") {
    REQUIRE(engine::block_local_in_chunk(-1, -1, -1) == glm::ivec3(31, 31, 31));
    REQUIRE(engine::block_local_in_chunk(32, 32, 32) == glm::ivec3(0, 0, 0));
    REQUIRE(engine::block_local_in_chunk(31, 15, 0) == glm::ivec3(31, 15, 0));
}
