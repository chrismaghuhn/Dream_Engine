#include "engine/character/core/CombatController.hpp"

#include "engine/movement/core/Components.hpp"
#include "engine/movement/core/InputSnapshot.hpp"

namespace engine::character {

namespace {

// Look up clip duration from the asset's clip list by clip name.
// Returns 0 if not found.
float clip_duration(const std::string& clip_name, const std::vector<AnimClip>& clips) {
    for (const AnimClip& c : clips) {
        if (c.name == clip_name) {
            return c.duration_seconds;
        }
    }
    return 0.f;
}

// Start a specific attack in the combo chain. Returns false if attack id unknown.
bool start_attack(CombatController& combat,
                  engine::movement::Transform& transform,
                  AnimationState& anim,
                  const AttackTable& attacks,
                  const std::vector<AnimClip>& clips,
                  int combo_idx) {
    if (combo_idx < 0 || combo_idx >= static_cast<int>(combat.combo_ids.size())) {
        return false;
    }
    const std::string& id = combat.combo_ids[static_cast<std::size_t>(combo_idx)];
    auto it = attacks.find(id);
    if (it == attacks.end()) {
        return false;
    }
    const AttackDef& def = it->second;

    combat.combo_index      = combo_idx;
    combat.hit_consumed     = false;
    combat.attack_yaw       = transform.yaw;

    anim.active_clip  = def.clip;
    anim.time_seconds = 0.f;
    anim.looping      = false;
    anim.speed        = 1.f;

    // Set clip countdown from the actual clip duration.
    const float dur = clip_duration(def.clip, clips);
    combat.clip_remaining = dur > 0.f ? dur : 1.f; // fallback: 1 s if clip not found

    return true;
}

} // namespace

void combat_tick(CombatController& combat,
                 engine::movement::Transform& transform,
                 AnimationState& anim,
                 engine::movement::InputSnapshot& input,
                 const AttackTable& attacks,
                 const std::vector<AnimClip>& clips,
                 float dt) {
    switch (combat.phase) {

    case CombatPhase::Idle: {
        if (input.attack_light && !combat.combo_ids.empty()) {
            input.attack_light = false;
            if (start_attack(combat, transform, anim, attacks, clips, 0)) {
                combat.phase = CombatPhase::Attacking;
            }
        }
        break;
    }

    case CombatPhase::Attacking: {
        // Consume any attack request — no new combo until Idle.
        input.attack_light = false;

        // Advance animation time (clip_remaining is the budget).
        anim.time_seconds += dt * anim.speed;
        combat.clip_remaining -= dt;

        if (combat.clip_remaining <= 0.f) {
            const int next_idx = combat.combo_index + 1;
            const int last_attack_idx = static_cast<int>(combat.combo_ids.size()) - 1;

            if (next_idx <= last_attack_idx) {
                // Advance to next hit in combo.
                if (!start_attack(combat, transform, anim, attacks, clips, next_idx)) {
                    combat.phase = CombatPhase::Idle;
                }
            } else {
                // All hits done — fetch recovery duration from last attack def.
                const std::string& last_id = combat.combo_ids[
                    static_cast<std::size_t>(combat.combo_index)];
                auto it = attacks.find(last_id);
                const float recovery_time =
                    (it != attacks.end()) ? it->second.recovery : 0.2f;

                combat.phase              = CombatPhase::Recovery;
                combat.recovery_remaining = recovery_time;

                anim.active_clip  = "Walk"; // return to locomotion clip
                anim.looping      = true;
                anim.time_seconds = 0.f;
            }
        }
        break;
    }

    case CombatPhase::Recovery: {
        // Ignore attack requests during recovery.
        input.attack_light = false;

        combat.recovery_remaining -= dt;
        if (combat.recovery_remaining <= 0.f) {
            combat.phase              = CombatPhase::Idle;
            combat.recovery_remaining = 0.f;
        }
        break;
    }

    } // switch
}

} // namespace engine::character
