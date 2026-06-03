#pragma once

#include "engine/character/core/AttackData.hpp"
#include "engine/character/core/CharacterAsset.hpp"
#include "engine/character/core/CharacterComponents.hpp"
#include "engine/character/core/InputBuffer.hpp"

#include <string>
#include <unordered_map>
#include <vector>

namespace engine::movement {
struct Transform;
} // namespace engine::movement

namespace engine::character {

using ChainTable = std::unordered_map<std::string, std::vector<std::string>>;

inline constexpr const char* kLightChain = "light_chain";
inline constexpr const char* kHeavyChain = "heavy_chain";
inline constexpr const char* kKickChain = "kick_chain";
inline constexpr const char* kSpecialChain = "special_chain";

// Advance the combat FSM by one fixed simulation step.
// AnimationController::tick must run before this function; combat_tick never
// advances AnimationState::time_seconds.
void combat_tick(CombatController& combat,
                 engine::movement::Transform& transform,
                 AnimationState& anim,
                 InputBuffer& buffer,
                 const AttackTable& attacks,
                 const std::vector<AnimClip>& clips,
                 const ChainTable& chains,
                 float dt);

} // namespace engine::character
