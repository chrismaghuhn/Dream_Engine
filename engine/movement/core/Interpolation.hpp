#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

namespace engine::movement {

// Render-side interpolation between the previous and current fixed-step state.
[[nodiscard]] inline glm::vec3 lerp_position(const glm::vec3& prev,
                                             const glm::vec3& curr,
                                             float alpha) {
    return glm::mix(prev, curr, alpha);
}

// Shortest-path angular interpolation in radians, for yaw render interpolation.
[[nodiscard]] inline float lerp_angle(float prev, float curr, float alpha) {
    float delta = curr - prev;
    const float two_pi = glm::two_pi<float>();
    delta = std::fmod(delta + glm::pi<float>(), two_pi);
    if (delta < 0.f) {
        delta += two_pi;
    }
    delta -= glm::pi<float>();
    return prev + delta * alpha;
}

} // namespace engine::movement
