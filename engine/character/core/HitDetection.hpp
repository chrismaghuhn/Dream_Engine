#pragma once

#include "engine/character/core/AttackData.hpp"
#include "engine/character/core/CharacterComponents.hpp"
#include "engine/movement/core/Components.hpp"

#include <glm/glm.hpp>

namespace engine::character {

// Capsule vs AABB overlap test. capsule_center is the segment midpoint.
[[nodiscard]] bool capsule_intersects_box(glm::vec3 capsule_center,
                                          float radius,
                                          float half_height,
                                          glm::vec3 box_center,
                                          glm::vec3 box_half_extents);

// Returns true if the current animation is inside the hit window and the
// overlap test passes. Sets combat.hit_consumed on a successful hit.
[[nodiscard]] bool try_hit_in_window(CombatController& combat,
                                     const AnimationState& anim,
                                     const AttackDef& def,
                                     float clip_duration_seconds,
                                     const engine::movement::Transform& attacker,
                                     const engine::movement::Transform& target,
                                     const engine::movement::Collider& target_collider);

} // namespace engine::character
