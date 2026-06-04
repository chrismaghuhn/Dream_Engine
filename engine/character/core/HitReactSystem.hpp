#pragma once

#include "engine/character/core/CharacterComponents.hpp"
#include "engine/movement/core/Components.hpp"

namespace engine::character {

struct ScreenShake {
    float magnitude = 0.f;
    float duration = 0.f;
    float timer = 0.f;
};

// Advance a HitReact component by dt. Applies knockback translation to
// transform.position and marks playing_hit = false when the timer expires.
void hit_react_tick(HitReact& react,
                    engine::movement::Transform& transform,
                    float dt,
                    bool hitstop_active = false);

// Trigger a new hit reaction on a dummy entity.
// direction: normalised world-space vector pointing FROM attacker TO target.
void trigger_hit_react(HitReact& react,
                       engine::movement::Transform& transform,
                       const glm::vec3& direction,
                       AnimationState& anim,
                       CombatController* attacker_combat = nullptr,
                       ScreenShake* shake = nullptr,
                       int attacker_hitstop_frames = 5);

void apply_screenshake(ScreenShake& shake, float magnitude, float duration);
void tick_screenshake(ScreenShake& shake, float dt);
[[nodiscard]] glm::vec3 screenshake_offset(const ScreenShake& shake);

} // namespace engine::character
