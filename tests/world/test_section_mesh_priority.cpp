#include <catch2/catch_test_macros.hpp>

#include "engine/world/SectionIndexing.hpp"

#include <glm/glm.hpp>

TEST_CASE("section_mesh_distance_sq orders nearer section first") {
    const glm::vec3 focus{16.f, 16.f, 16.f};
    const float near_dist = engine::section_mesh_distance_sq({0, 0, 0}, 0, focus);
    const float far_dist  = engine::section_mesh_distance_sq({4, 0, 0}, 0, focus);
    REQUIRE(near_dist < far_dist);
}

TEST_CASE("section_mesh_distance_sq orders vertical sections by focus height") {
    const glm::vec3 focus{16.f, 24.f, 16.f};
    const float lower = engine::section_mesh_distance_sq({0, 0, 0}, 0, focus);
    const float upper = engine::section_mesh_distance_sq({0, 0, 0}, 4, focus);
    REQUIRE(upper < lower);
}