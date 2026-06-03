#pragma once

#include "engine/character/core/CharacterComponents.hpp"
#include "engine/movement/core/Components.hpp"

namespace engine::character {

// Advance a HitReact component by dt. Applies knockback translation to
// transform.position and marks playing_hit = false when the timer expires.
void hit_react_tick(HitReact& react, engine::movement::Transform& transform, float dt);

// Trigger a new hit reaction on a dummy entity.
// direction: normalised world-space vector pointing FROM attacker TO target.
void trigger_hit_react(HitReact& react,
                       engine::movement::Transform& transform,
                       const glm::vec3& direction,
                       AnimationState& anim);

} // namespace engine::character
