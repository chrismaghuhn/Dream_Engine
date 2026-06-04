#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "engine/movement/core/CameraSystem.hpp"
#include "engine/movement/core/Components.hpp"

#include <glm/gtc/constants.hpp>

using Catch::Matchers::WithinAbs;
using namespace engine::movement;

TEST_CASE("pitch is clamped to its range") {
    CameraRig cam;
    camera::configure(cam, 5.f, 1.7f);

    camera::update(cam, glm::vec2(0.f, -10000.f), 0.01f, 0.f);
    REQUIRE_THAT(cam.pitch, WithinAbs(glm::radians(camera::kMinPitchDeg), 1e-4));

    camera::update(cam, glm::vec2(0.f, 10000.f), 0.01f, 0.f);
    REQUIRE_THAT(cam.pitch, WithinAbs(glm::radians(camera::kMaxPitchDeg), 1e-4));
}

TEST_CASE("mouse up raises the orbit camera eye") {
    CameraRig cam;
    camera::configure(cam, 5.f, 1.7f);
    camera::set_pitch_degrees(cam, -15.f);

    const glm::vec3 player(0.f, 0.f, 0.f);
    const float before_y = camera::eye_position(cam, player).y;

    camera::update(cam, glm::vec2(0.f, -10.f), 0.01f, 0.f);

    const float after_y = camera::eye_position(cam, player).y;
    REQUIRE(after_y > before_y);
}

TEST_CASE("distance clamps on scroll") {
    CameraRig cam;
    camera::configure(cam, 5.f, 1.7f);
    camera::update(cam, glm::vec2(0.f), 0.f, 1000.f);
    REQUIRE_THAT(cam.distance, WithinAbs(camera::kMinDistance, 1e-4));
    camera::update(cam, glm::vec2(0.f), 0.f, -1000.f);
    REQUIRE_THAT(cam.distance, WithinAbs(camera::kMaxDistance, 1e-4));
}

TEST_CASE("forward direction at yaw zero points along +Z") {
    CameraRig cam;
    cam.yaw = 0.f;
    const glm::vec3 fwd = camera::forward_dir(cam);
    REQUIRE_THAT(fwd.x, WithinAbs(0.0, 1e-4));
    REQUIRE_THAT(fwd.z, WithinAbs(1.0, 1e-4));
    REQUIRE_THAT(fwd.y, WithinAbs(0.0, 1e-4));
}

TEST_CASE("eye sits behind and above the player") {
    CameraRig cam;
    camera::configure(cam, 5.f, 1.7f);
    cam.yaw = 0.f;
    camera::set_pitch_degrees(cam, 0.f);

    const glm::vec3 player(0.f, 0.f, 0.f);
    const glm::vec3 eye = camera::eye_position(cam, player);
    REQUIRE_THAT(eye.z, WithinAbs(-5.0, 1e-3));
    REQUIRE_THAT(eye.y, WithinAbs(1.7, 1e-3));
}
