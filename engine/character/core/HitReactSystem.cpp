#include "engine/character/core/HitReactSystem.hpp"

#include <algorithm>
#include <cmath>

namespace engine::character {

void hit_react_tick(HitReact& react,
                    engine::movement::Transform& transform,
                    float dt,
                    bool hitstop_active) {
    if (!react.playing_hit || react.timer <= 0.f) {
        react.playing_hit = false;
        return;
    }

    if (hitstop_active) {
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
                       AnimationState& anim,
                       CombatController* attacker_combat,
                       ScreenShake* shake,
                       int attacker_hitstop_frames) {
    react.playing_hit       = true;
    react.timer             = react.knockback_duration;
    // Horizontal knockback only.
    react.knockback_delta   = glm::vec3(direction.x, 0.f, direction.z) *
                              react.knockback_distance;

    anim.active_clip  = react.hit_clip;
    anim.time_seconds = 0.f;
    anim.looping      = false;
    anim.speed        = 1.f;

    if (attacker_combat != nullptr) {
        attacker_combat->hitstop_active = true;
        attacker_combat->phase_before_hitstop = attacker_combat->phase;
        attacker_combat->hitstop_frames = attacker_hitstop_frames;
    }

    if (shake != nullptr) {
        apply_screenshake(*shake, 0.04f, 0.12f);
    }
}

void apply_screenshake(ScreenShake& shake, float magnitude, float duration) {
    shake.magnitude = std::max(shake.magnitude, magnitude);
    shake.duration = std::max(shake.duration, duration);
    shake.timer = std::max(shake.timer, duration);
}

void tick_screenshake(ScreenShake& shake, float dt) {
    if (shake.timer <= 0.f) {
        shake.timer = 0.f;
        shake.magnitude = 0.f;
        return;
    }

    shake.timer -= dt;
    if (shake.timer <= 0.f) {
        shake.timer = 0.f;
        shake.magnitude = 0.f;
    }
}

glm::vec3 screenshake_offset(const ScreenShake& shake) {
    if (shake.timer <= 0.f || shake.duration <= 1e-5f || shake.magnitude <= 0.f) {
        return glm::vec3(0.f);
    }

    const float norm = shake.timer / shake.duration;
    const float amount = shake.magnitude * norm;
    const float phase = shake.timer * 97.f;
    return glm::vec3(std::sin(phase) * amount,
                     std::sin(phase * 1.37f) * amount * 0.5f,
                     0.f);
}

} // namespace engine::character
