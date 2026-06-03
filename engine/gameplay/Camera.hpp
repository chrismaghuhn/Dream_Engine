#pragma once

#include <glm/glm.hpp>

namespace engine {

struct Camera {
    glm::vec3 position{0.f, 8.f, 0.f};
    float yaw = -90.f;
    float pitch = 0.f;
    float fov_y_deg = 70.f;
    float near_plane = 0.1f;
    float far_plane = 4096.f;

    [[nodiscard]] glm::vec3 forward() const;
    [[nodiscard]] glm::vec3 right() const;
    [[nodiscard]] glm::vec3 up() const;

    [[nodiscard]] glm::mat4 view_matrix() const;
    [[nodiscard]] glm::mat4 projection_matrix(float aspect_ratio) const;
};

} // namespace engine
