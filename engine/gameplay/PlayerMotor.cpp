#include "engine/gameplay/PlayerMotor.hpp"

#include "engine/gameplay/Camera.hpp"

#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>

namespace engine {

void PlayerMotor::sync_capsule_from_camera(const glm::vec3& camera_position) {
    state_.capsule.radius = kDefaultRadius;
    state_.capsule.half_height = kDefaultHalfHeight;
    state_.capsule.center = camera_position - glm::vec3{0.f, kEyeHeight, 0.f};
}

void PlayerMotor::apply_camera_to_capsule(Camera& camera) const {
    camera.position = state_.capsule.center + glm::vec3{0.f, kEyeHeight, 0.f};
}

void PlayerMotor::tick_walk(
    Camera& camera,
    const Input& input,
    float fixed_dt,
    ChunkStore& store,
    const PlayerMotorConfig& motor,
    const VoxelMovementConfig& movement) {
    sync_capsule_from_camera(camera.position);

    glm::vec3 wish_dir{0.f};
    if (input.move_forward()) {
        wish_dir += camera.forward();
    }
    if (input.move_back()) {
        wish_dir -= camera.forward();
    }
    if (input.move_right()) {
        wish_dir += camera.right();
    }
    if (input.move_left()) {
        wish_dir -= camera.right();
    }
    wish_dir.y = 0.f;
    if (glm::length(wish_dir) > 0.f) {
        wish_dir = glm::normalize(wish_dir);
    }

    glm::vec3 horizontal = wish_dir * motor.max_walk_speed;
    state_.velocity.x = horizontal.x;
    state_.velocity.z = horizontal.z;

    if (state_.on_ground) {
        state_.velocity.y = std::min(state_.velocity.y, 0.f);
        if (input.jump_pressed()) {
            state_.velocity.y = motor.jump_velocity;
        }
    }

    state_.velocity.y += motor.gravity * fixed_dt;

    const CapsuleMoveResult moved = move_and_slide(
        state_.capsule,
        state_.velocity,
        fixed_dt,
        store,
        movement,
        motor.ground_snap);

    state_.capsule.center = moved.center;
    state_.velocity = moved.velocity;
    state_.on_ground = moved.on_ground;

    apply_camera_to_capsule(camera);
}

} // namespace engine
