#include "engine/character/core/CombatController.hpp"

#include "engine/character/core/AnimationController.hpp"
#include "engine/movement/core/Components.hpp"

namespace engine::character {

namespace {

float clip_duration(const std::string& name, const std::vector<AnimClip>& clips) {
    for (const AnimClip& clip : clips) {
        if (clip.name == name) {
            return clip.duration_seconds;
        }
    }
    return 1.f;
}

const AttackDef* current_def(const CombatController& combat, const AttackTable& attacks) {
    if (combat.combo_index < 0 ||
        combat.combo_index >= static_cast<int>(combat.combo_ids.size())) {
        return nullptr;
    }

    const std::string& id = combat.combo_ids[static_cast<std::size_t>(combat.combo_index)];
    auto it = attacks.find(id);
    return it != attacks.end() ? &it->second : nullptr;
}

float norm_time(const AnimationState& anim,
                const AttackDef* def,
                const std::vector<AnimClip>& clips) {
    if (!def) {
        return 0.f;
    }

    const float duration = clip_duration(def->clip, clips);
    return duration > 1e-5f ? anim.time_seconds / duration : 1.f;
}

std::string kind_to_chain(BufferedInput::Kind kind) {
    switch (kind) {
    case BufferedInput::Kind::Light:
        return kLightChain;
    case BufferedInput::Kind::Heavy:
        return kHeavyChain;
    case BufferedInput::Kind::Kick:
        return kKickChain;
    case BufferedInput::Kind::Special:
        return kSpecialChain;
    default:
        return {};
    }
}

bool start_attack(CombatController& combat,
                  engine::movement::Transform& transform,
                  AnimationState& anim,
                  const AttackTable& attacks,
                  int combo_index,
                  BufferedInput::Kind kind) {
    if (combo_index < 0 ||
        combo_index >= static_cast<int>(combat.combo_ids.size())) {
        return false;
    }

    const std::string& id = combat.combo_ids[static_cast<std::size_t>(combo_index)];
    auto it = attacks.find(id);
    if (it == attacks.end()) {
        return false;
    }

    combat.combo_index = combo_index;
    combat.hit_consumed = false;
    combat.attack_yaw = transform.yaw;
    combat.active_kind = kind;
    combat.phase = CombatPhase::Startup;

    AnimationController::crossfade_to(anim, it->second.clip, 0.08f, false);
    return true;
}

void reset_to_idle(CombatController& combat, AnimationState& anim) {
    AnimationController::crossfade_to(anim, "Walk", 0.15f, true);
    combat.phase = CombatPhase::Idle;
    combat.combo_index = 0;
    combat.active_kind = BufferedInput::Kind::None;
    combat.combo_ids.clear();
    combat.hit_consumed = false;
}

} // namespace

void combat_tick(CombatController& combat,
                 engine::movement::Transform& transform,
                 AnimationState& anim,
                 InputBuffer& buffer,
                 const AttackTable& attacks,
                 const std::vector<AnimClip>& clips,
                 const ChainTable& chains,
                 float dt) {
    (void)dt;

    if (combat.hitstop_active) {
        --combat.hitstop_frames;
        if (combat.hitstop_frames <= 0) {
            combat.hitstop_active = false;
            combat.phase = combat.phase_before_hitstop;
            anim.speed = 1.f;
        }
        return;
    }

    const AttackDef* def = current_def(combat, attacks);
    const float nt = norm_time(anim, def, clips);

    switch (combat.phase) {
    case CombatPhase::Idle: {
        const BufferedInput::Kind kind = buffer.peek();
        if (kind == BufferedInput::Kind::None || kind == BufferedInput::Kind::Dodge) {
            break;
        }

        const std::string chain = kind_to_chain(kind);
        auto it = chains.find(chain);
        if (it == chains.end() || it->second.empty()) {
            break;
        }

        buffer.consume();
        combat.combo_ids = it->second;
        (void)start_attack(combat, transform, anim, attacks, 0, kind);
        break;
    }

    case CombatPhase::Startup:
        if (def && nt >= def->hit_start_norm) {
            combat.phase = CombatPhase::Active;
        }
        break;

    case CombatPhase::Active:
        if (def && nt > def->hit_end_norm) {
            combat.phase = CombatPhase::Recovery;
        }
        break;

    case CombatPhase::Recovery: {
        const int next_index = combat.combo_index + 1;
        const bool has_next = next_index < static_cast<int>(combat.combo_ids.size());

        if (def && nt >= def->dodge_cancel_start_norm &&
            buffer.peek() == BufferedInput::Kind::Dodge) {
            buffer.consume();
            combat.dodge_requested = true;
            reset_to_idle(combat, anim);
            combat.phase = CombatPhase::DodgeCancel;
            break;
        }

        if (has_next && def && nt >= def->cancel_start_norm) {
            const BufferedInput::Kind kind = buffer.peek();
            if (kind == combat.active_kind) {
                buffer.consume();
                (void)start_attack(combat, transform, anim, attacks, next_index, kind);
                break;
            }
        }

        const float duration = def ? clip_duration(def->clip, clips) : 0.f;
        if (duration > 1e-5f && anim.time_seconds >= duration) {
            if (has_next && buffer.peek() == combat.active_kind) {
                const BufferedInput::Kind kind = buffer.peek();
                buffer.consume();
                (void)start_attack(combat, transform, anim, attacks, next_index, kind);
            } else {
                reset_to_idle(combat, anim);
            }
        }
        break;
    }

    case CombatPhase::DodgeCancel:
        combat.dodge_requested = false;
        combat.phase = CombatPhase::Idle;
        break;
    }
}

} // namespace engine::character
