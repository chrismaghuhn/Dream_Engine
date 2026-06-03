#include "engine/movement/core/CameraSystem.hpp"

#include <algorithm>
#include <cmath>

#include <glm/gtc/matrix_transform.hpp>

namespace engine::movement::camera {

namespace {
constexpr float kScrollStep = 0.5f;
}

void configure(CameraRig& rig, float distance, float height) {
    rig.distance = std::clamp(distance, kMinDistance, kMaxDistance);
    rig.height = height;
}

void set_pitch_degrees(CameraRig& rig, float pitch_deg) {
    rig.pitch = glm::radians(std::clamp(pitch_deg, kMinPitchDeg, kMaxPitchDeg));
}

void update(CameraRig& rig, const glm::vec2& mouse_delta, float sensitivity, float scroll) {
    rig.yaw += mouse_delta.x * sensitivity;
    // Moving the mouse up (negative delta.y in screen space) raises the pitch.
    rig.pitch -= mouse_delta.y * sensitivity;

    const float min_pitch = glm::radians(kMinPitchDeg);
    const float max_pitch = glm::radians(kMaxPitchDeg);
    rig.pitch = std::clamp(rig.pitch, min_pitch, max_pitch);

    if (scroll != 0.f) {
        rig.distance = std::clamp(rig.distance - scroll * kScrollStep, kMinDistance, kMaxDistance);
    }
}

glm::vec3 forward_dir(const CameraRig& rig) {
    return glm::vec3(std::sin(rig.yaw), 0.f, std::cos(rig.yaw));
}

glm::vec3 look_direction(const CameraRig& rig) {
    const float cp = std::cos(rig.pitch);
    return glm::vec3(cp * std::sin(rig.yaw), std::sin(rig.pitch), cp * std::cos(rig.yaw));
}

glm::vec3 eye_position(const CameraRig& rig, const glm::vec3& player_pos) {
    const glm::vec3 target = player_pos + glm::vec3(0.f, rig.height, 0.f);
    return target - look_direction(rig) * rig.distance;
}

glm::mat4 view_matrix(const CameraRig& rig, const glm::vec3& player_pos) {
    const glm::vec3 target = player_pos + glm::vec3(0.f, rig.height, 0.f);
    const glm::vec3 eye = target - look_direction(rig) * rig.distance;
    return glm::lookAt(eye, target, glm::vec3(0.f, 1.f, 0.f));
}

} // namespace engine::movement::camera
