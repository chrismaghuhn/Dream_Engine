#pragma once

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
};

enum class CombatPhase { Idle, Attacking, Recovery };

struct CombatController {
    CombatPhase phase = CombatPhase::Idle;
    int combo_index = 0;
    std::vector<std::string> combo_ids;
    float attack_yaw = 0.f;
    float clip_remaining = 0.f;      // countdown to end of current attack clip
    float recovery_remaining = 0.f;  // countdown for Recovery phase
    bool hit_consumed = false;
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
