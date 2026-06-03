#include "engine/gameplay/Camera.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

namespace engine {

namespace {

constexpr float kPitchLimit = 89.f;

float to_radians(float degrees) {
    return degrees * (3.14159265358979323846f / 180.f);
}

} // namespace

glm::vec3 Camera::forward() const {
    const float yaw_rad = to_radians(yaw);
    const float pitch_rad = to_radians(pitch);
    return glm::normalize(glm::vec3{
        std::cos(yaw_rad) * std::cos(pitch_rad),
        std::sin(pitch_rad),
        std::sin(yaw_rad) * std::cos(pitch_rad),
    });
}

glm::vec3 Camera::right() const {
    return glm::normalize(glm::cross(forward(), glm::vec3{0.f, 1.f, 0.f}));
}

glm::vec3 Camera::up() const {
    return glm::normalize(glm::cross(right(), forward()));
}

glm::mat4 Camera::view_matrix() const {
    return glm::lookAt(position, position + forward(), glm::vec3{0.f, 1.f, 0.f});
}

glm::mat4 Camera::projection_matrix(float aspect_ratio) const {
    return glm::perspective(to_radians(fov_y_deg), aspect_ratio, near_plane, far_plane);
}

} // namespace engine
