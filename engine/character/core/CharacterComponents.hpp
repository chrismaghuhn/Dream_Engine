#pragma once

#include "engine/character/core/InputBuffer.hpp"

#include <cstdint>
#include <string>
#include <vector>

#include <glm/glm.hpp>

namespace engine::character {

struct SkinnedModel {
    std::string glb_path;
    std::uint32_t catalog_id = 0;
};

struct AnimationState {
    std::string active_clip;
    float time_seconds = 0.f;
    float speed = 1.f;
    bool looping = true;

    // Blend-out slot: previous clip fading to 0.
    std::string blend_clip;
    float blend_time = 0.f;
    float blend_weight = 0.f;
    float blend_duration = 0.12f;
};

// Startup: pre-hit wind-up frames (norm_time < hit_start_norm).
// Active: hit window open (hit_start_norm <= norm_time <= hit_end_norm).
// Recovery: committed end-lag after the active window.
// DodgeCancel: one-frame state, applies dodge impulse then returns to Idle.
enum class CombatPhase { Idle, Startup, Active, Recovery, DodgeCancel };

struct CombatController {
    CombatPhase phase = CombatPhase::Idle;
    int combo_index = 0;
    std::vector<std::string> combo_ids;
    BufferedInput::Kind active_kind = BufferedInput::Kind::None;

    float attack_yaw = 0.f;
    bool hit_consumed = false;

    // Hitstop overlay (not a CombatPhase value).
    bool hitstop_active = false;
    CombatPhase phase_before_hitstop = CombatPhase::Idle;
    int hitstop_frames = 0;

    // Set true for one frame by DodgeCancel; MovementApp reads and clears it.
    bool dodge_requested = false;
};

struct HitReact {
    float knockback_distance = 0.3f;
    float knockback_duration = 0.25f;
    std::string hit_clip = "Hit_Reaction_1";
    float timer = 0.f;
    glm::vec3 knockback_delta{0.f};
    bool playing_hit = false;
};

} // namespace engine::character
