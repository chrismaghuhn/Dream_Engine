#pragma once

#include <array>

#include <glm/glm.hpp>

namespace engine {

inline std::array<glm::vec4, 6> frustum_planes_from_matrix(const glm::mat4& matrix) {
    std::array<glm::vec4, 6> planes{
        glm::vec4(matrix[0][3] + matrix[0][0], matrix[1][3] + matrix[1][0], matrix[2][3] + matrix[2][0],
                  matrix[3][3] + matrix[3][0]),
        glm::vec4(matrix[0][3] - matrix[0][0], matrix[1][3] - matrix[1][0], matrix[2][3] - matrix[2][0],
                  matrix[3][3] - matrix[3][0]),
        glm::vec4(matrix[0][3] + matrix[0][1], matrix[1][3] + matrix[1][1], matrix[2][3] + matrix[2][1],
                  matrix[3][3] + matrix[3][1]),
        glm::vec4(matrix[0][3] - matrix[0][1], matrix[1][3] - matrix[1][1], matrix[2][3] - matrix[2][1],
                  matrix[3][3] - matrix[3][1]),
        glm::vec4(matrix[0][3] + matrix[0][2], matrix[1][3] + matrix[1][2], matrix[2][3] + matrix[2][2],
                  matrix[3][3] + matrix[3][2]),
        glm::vec4(matrix[0][3] - matrix[0][2], matrix[1][3] - matrix[1][2], matrix[2][3] - matrix[2][2],
                  matrix[3][3] - matrix[3][2]),
    };

    for (glm::vec4& plane : planes) {
        const float length = glm::length(glm::vec3(plane));
        if (length > 0.f) {
            plane /= length;
        }
    }
    return planes;
}

inline bool aabb_intersects_frustum(const std::array<glm::vec4, 6>& planes,
                                    const glm::vec3& min_corner,
                                    const glm::vec3& max_corner) {
    for (const glm::vec4& plane : planes) {
        const glm::vec3 positive_vertex{
            plane.x >= 0.f ? max_corner.x : min_corner.x,
            plane.y >= 0.f ? max_corner.y : min_corner.y,
            plane.z >= 0.f ? max_corner.z : min_corner.z,
        };
        if (glm::dot(glm::vec3(plane), positive_vertex) + plane.w < 0.f) {
            return false;
        }
    }
    return true;
}

} // namespace engine
