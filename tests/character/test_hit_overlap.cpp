#include <catch2/catch_test_macros.hpp>

#include "engine/character/core/HitDetection.hpp"

using namespace engine::character;

TEST_CASE("capsule directly above box hits it", "[hit_overlap]") {
    // Capsule center at (0, 1, 0), box at origin half=1.
    REQUIRE(capsule_intersects_box(
        {0.f, 1.f, 0.f}, 0.35f, 0.5f,
        {0.f, 0.f, 0.f}, {1.f, 1.f, 1.f}));
}

TEST_CASE("capsule far away does not hit box", "[hit_overlap]") {
    REQUIRE_FALSE(capsule_intersects_box(
        {10.f, 0.f, 0.f}, 0.35f, 0.5f,
        {0.f, 0.f, 0.f}, {0.5f, 0.5f, 0.5f}));
}

TEST_CASE("capsule just touching box surface hits", "[hit_overlap]") {
    // Box max X = 1.0; capsule radius = 0.35 → touches at X = 1.35.
    REQUIRE(capsule_intersects_box(
        {1.34f, 0.f, 0.f}, 0.35f, 0.5f,
        {0.f, 0.f, 0.f}, {1.f, 1.f, 1.f}));
}

TEST_CASE("capsule just outside box does not hit", "[hit_overlap]") {
    // Box max X = 1.0; capsule at 1.4 with radius 0.35 → gap = 0.05.
    REQUIRE_FALSE(capsule_intersects_box(
        {1.4f, 0.f, 0.f}, 0.35f, 0.5f,
        {0.f, 0.f, 0.f}, {1.f, 1.f, 1.f}));
}

TEST_CASE("capsule below box does not hit", "[hit_overlap]") {
    // Box occupies Y [-0.5, 0.5]; capsule segment Y [2.5, 3.5].
    REQUIRE_FALSE(capsule_intersects_box(
        {0.f, 3.f, 0.f}, 0.35f, 0.5f,
        {0.f, 0.f, 0.f}, {1.f, 0.5f, 1.f}));
}

TEST_CASE("wall collision hit (attack yaw pointing at target)", "[hit_overlap]") {
    // Attacker at origin, yaw=0 (forward = +Z), range=1.25 → hit center at Z=1.25.
    // Target box at Z=1.25, half=0.4 → directly overlapping.
    REQUIRE(capsule_intersects_box(
        {0.f, 1.f, 1.25f}, 0.35f, 0.35f,
        {0.f, 1.f, 1.25f}, {0.4f, 0.9f, 0.4f}));
}
