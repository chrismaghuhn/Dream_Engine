#pragma once

#include "engine/movement/core/Components.hpp"

#include <glm/glm.hpp>

namespace engine::movement::camera {

inline constexpr float kMinPitchDeg = -80.f;
inline constexpr float kMaxPitchDeg = 20.f;
inline constexpr float kMinDistance = 1.5f;
inline constexpr float kMaxDistance = 12.f;

// Gothic-style third-person orbit camera. Pure math operating on a CameraRig
// component: consumes a mouse delta and produces a view matrix. No GLFW here.
void configure(CameraRig& rig, float distance, float height);
void set_pitch_degrees(CameraRig& rig, float pitch_deg);

// mouse_delta in pixels (x = yaw, y = pitch); scroll in wheel ticks.
void update(CameraRig& rig, const glm::vec2& mouse_delta, float sensitivity, float scroll);

// Horizontal forward used for camera-relative movement (XZ plane).
[[nodiscard]] glm::vec3 forward_dir(const CameraRig& rig);

// Full 3D look direction including pitch.
[[nodiscard]] glm::vec3 look_direction(const CameraRig& rig);

// Camera eye position for a given player position.
[[nodiscard]] glm::vec3 eye_position(const CameraRig& rig, const glm::vec3& player_pos);

// View matrix looking at player_pos + height.
[[nodiscard]] glm::mat4 view_matrix(const CameraRig& rig, const glm::vec3& player_pos);

} // namespace engine::movement::camera
