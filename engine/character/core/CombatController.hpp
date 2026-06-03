#pragma once

#include "engine/character/core/AttackData.hpp"
#include "engine/character/core/CharacterAsset.hpp"
#include "engine/character/core/CharacterComponents.hpp"

#include <vector>

namespace engine::movement {
struct Transform;
struct InputSnapshot;
} // namespace engine::movement

namespace engine::character {

// Advance the combat FSM by dt seconds.
// - Idle + attack_pressed → start first combo attack, lock attack_yaw.
// - Attacking → clip_remaining counts down; on expiry advance combo or enter Recovery.
// - Recovery → recovery_remaining counts down; on expiry return to Idle.
// - Movement is frozen while Attacking or in Recovery (caller responsibility).
// clips: the character asset's clip list, used for duration lookup when starting attacks.
void combat_tick(CombatController& combat,
                 engine::movement::Transform& transform,
                 AnimationState& anim,
                 engine::movement::InputSnapshot& input,
                 const AttackTable& attacks,
                 const std::vector<AnimClip>& clips,
                 float dt);

} // namespace engine::character
