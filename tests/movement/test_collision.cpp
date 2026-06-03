#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "engine/movement/core/CollisionWorld.hpp"
#include "engine/movement/core/Components.hpp"
#include "engine/movement/core/PlayerMovement.hpp"

#include <algorithm>

#include <glm/gtc/constants.hpp>

using Catch::Matchers::WithinAbs;
using namespace engine::movement;

namespace {
PlayerController make_pc(float gravity, float jump_velocity = 7.f) {
    PlayerController pc;
    pc.speed = 4.f;
    pc.gravity = gravity;
    pc.jump_velocity = jump_velocity;
    return pc;
}
} // namespace

TEST_CASE("capsule depenetrates from ground plane") {
    CollisionWorld world;
    world.set_ground_plane(0.f);

    CapsuleCollider capsule;
    capsule.radius = 0.4f;
    capsule.half_height = 0.5f;

    const ContactResult result = world.resolve(capsule, glm::vec3(0.f, 0.2f, 0.f));
    REQUIRE(result.hit);
    REQUIRE(result.grounded);
    REQUIRE_THAT(result.position.y, WithinAbs(capsule.total_half_height(), 1e-4));
    REQUIRE_THAT(result.normal.y, WithinAbs(1.0, 1e-4));
    REQUIRE_FALSE(result.contact_points.empty());
}

TEST_CASE("capsule above ground is untouched") {
    CollisionWorld world;
    world.set_ground_plane(0.f);
    CapsuleCollider capsule;
    const ContactResult result = world.resolve(capsule, glm::vec3(0.f, 5.f, 0.f));
    REQUIRE_FALSE(result.hit);
    REQUIRE_FALSE(result.grounded);
}

TEST_CASE("wall contact does not set grounded") {
    CollisionWorld world;
    world.add_box(glm::vec3(0.f, 0.f, 0.f), glm::vec3(1.f, 1.f, 1.f));

    CapsuleCollider capsule;
    capsule.radius = 0.4f;
    capsule.half_height = 0.5f;

    const ContactResult result = world.resolve(capsule, glm::vec3(1.2f, 0.f, 0.f));
    REQUIRE(result.hit);
    REQUIRE(result.wall_contact);
    REQUIRE_FALSE(result.grounded);
    REQUIRE_THAT(result.position.x, WithinAbs(1.4, 1e-3));
    REQUIRE_THAT(result.normal.x, WithinAbs(1.0, 1e-3));
}

TEST_CASE("player settles on ground under gravity") {
    CollisionWorld world;
    world.set_ground_plane(0.f);

    Transform tf;
    tf.position = glm::vec3(0.f, 5.f, 0.f);
    PlayerController pc = make_pc(18.f);
    CapsuleCollider capsule;

    InputSnapshot input;
    for (int i = 0; i < 240; ++i) {
        input.jump_pressed = false;
        player_tick(tf, pc, capsule, world, input, 0.f, 1.f / 60.f);
    }

    REQUIRE(pc.grounded);
    REQUIRE_THAT(tf.position.y, WithinAbs(capsule.total_half_height(), 1e-2));
    REQUIRE_THAT(pc.velocity.y, WithinAbs(0.0, 1e-2));
}

TEST_CASE("grounded player can jump; airborne player cannot re-jump") {
    CollisionWorld world;
    world.set_ground_plane(0.f);

    Transform tf;
    CapsuleCollider capsule;
    tf.position = glm::vec3(0.f, capsule.total_half_height(), 0.f);
    PlayerController pc = make_pc(18.f, 7.f);
    pc.grounded = true;

    InputSnapshot input;
    input.jump_pressed = true;
    player_tick(tf, pc, capsule, world, input, 0.f, 1.f / 60.f);
    REQUIRE_FALSE(input.jump_pressed); // consumed
    const float vel_after_jump = pc.velocity.y;
    REQUIRE(vel_after_jump > 0.f);
    REQUIRE_FALSE(pc.grounded);

    // Re-pressing while airborne must not jump again.
    input.jump_pressed = true;
    player_tick(tf, pc, capsule, world, input, 0.f, 1.f / 60.f);
    REQUIRE(pc.velocity.y < vel_after_jump);
}

TEST_CASE("jump produces expected arc height") {
    CollisionWorld world;
    world.set_ground_plane(0.f);

    Transform tf;
    CapsuleCollider capsule;
    tf.position = glm::vec3(0.f, capsule.total_half_height(), 0.f);
    PlayerController pc = make_pc(18.f, 7.f);
    pc.grounded = true;

    const float rest_y = capsule.total_half_height();

    InputSnapshot input;
    input.jump_pressed = true;
    player_tick(tf, pc, capsule, world, input, 0.f, 1.f / 60.f);

    float peak = tf.position.y;
    for (int i = 0; i < 240; ++i) {
        input.jump_pressed = false;
        player_tick(tf, pc, capsule, world, input, 0.f, 1.f / 60.f);
        peak = std::max(peak, tf.position.y);
    }

    const float expected_rise = (pc.jump_velocity * pc.jump_velocity) / (2.f * pc.gravity);
    const float actual_rise = peak - rest_y;
    REQUIRE_THAT(actual_rise, WithinAbs(expected_rise, expected_rise * 0.1));
}

TEST_CASE("walking into a wall stops on impact (no sliding)") {
    CollisionWorld world;
    world.set_ground_plane(0.f);
    // Wall face at x = 1.5.
    world.add_box(glm::vec3(2.f, 1.f, 0.f), glm::vec3(0.5f, 1.f, 5.f));

    Transform tf;
    CapsuleCollider capsule; // radius 0.4
    tf.position = glm::vec3(0.f, capsule.total_half_height(), 0.f);
    PlayerController pc = make_pc(18.f);
    pc.grounded = true;

    InputSnapshot input;
    input.move_forward = true; // with yaw = +90deg, forward = +X
    const float yaw = glm::half_pi<float>();

    for (int i = 0; i < 240; ++i) {
        player_tick(tf, pc, capsule, world, input, yaw, 1.f / 60.f);
    }

    // Blocked just outside the wall face (1.5 - radius), never tunneling through.
    REQUIRE(tf.position.x <= 1.5f - capsule.radius + 1e-2f);
    // Pushing diagonally would slide; stop-on-impact kills horizontal velocity.
    REQUIRE_THAT(pc.velocity.x, WithinAbs(0.0, 1e-2));
}

TEST_CASE("ground probe detects floor below the capsule") {
    CollisionWorld world;
    world.set_ground_plane(0.f);
    CapsuleCollider capsule;

    // Capsule resting exactly on the plane: probe should hit just below.
    const glm::vec3 resting(0.f, capsule.total_half_height(), 0.f);
    const GroundProbe probe = world.probe_ground(capsule, resting, 0.1f);
    REQUIRE(probe.hit);
    REQUIRE_THAT(probe.point.y, WithinAbs(0.0, 1e-4));
}
