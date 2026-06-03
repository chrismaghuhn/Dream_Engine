#include "engine/character/core/HitReactSystem.hpp"

#include <algorithm>
#include <cmath>

namespace engine::character {

void hit_react_tick(HitReact& react,
                    engine::movement::Transform& transform,
                    float dt) {
    if (!react.playing_hit || react.timer <= 0.f) {
        react.playing_hit = false;
        return;
    }

    // Ease-out knockback: move fastest at start, slow to stop.
    const float t = react.timer / react.knockback_duration;
    const float speed = t; // linear ease-out (proportional to remaining time)
    const float move   = glm::length(react.knockback_delta) * speed * dt /
                         (react.knockback_duration > 1e-5f ? react.knockback_duration : 1.f);

    if (glm::length(react.knockback_delta) > 1e-5f) {
        transform.position += glm::normalize(react.knockback_delta) * move;
    }

    react.timer -= dt;
    if (react.timer <= 0.f) {
        react.timer       = 0.f;
        react.playing_hit = false;
    }
}

void trigger_hit_react(HitReact& react,
                       engine::movement::Transform& /*transform*/,
                       const glm::vec3& direction,
                       AnimationState& anim) {
    react.playing_hit       = true;
    react.timer             = react.knockback_duration;
    // Horizontal knockback only.
    react.knockback_delta   = glm::vec3(direction.x, 0.f, direction.z) *
                              react.knockback_distance;

    anim.active_clip  = react.hit_clip;
    anim.time_seconds = 0.f;
    anim.looping      = false;
    anim.speed        = 1.f;
}

} // namespace engine::character
