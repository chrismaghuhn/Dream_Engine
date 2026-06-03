#include "engine/character/core/HitDetection.hpp"

#include "engine/character/core/AnimationController.hpp"
#include "engine/movement/core/Components.hpp"

#include <algorithm>
#include <cmath>

namespace engine::character {

// ---------------------------------------------------------------------------
// Capsule vs AABB
// ---------------------------------------------------------------------------

bool capsule_intersects_box(glm::vec3 capsule_center,
                            float radius,
                            float half_height,
                            glm::vec3 box_center,
                            glm::vec3 box_half_extents) {
    // Vertical capsule (axis along Y): compute the minimum squared distance
    // between the capsule segment and the AABB by working per-axis independently.
    //
    // For a vertical capsule the X/Z position is constant along the segment.
    // The minimum distance in each axis:
    //   X, Z: gap between capsule_center.x/z and the nearest box face.
    //   Y:    gap between the segment [center.y ± half_height] and the box Y range.
    // The capsule hits if sqrt(dx²+dy²+dz²) ≤ radius.

    const glm::vec3 box_min = box_center - box_half_extents;
    const glm::vec3 box_max = box_center + box_half_extents;

    // Signed distance in X and Z (positive = outside box on that side).
    const float dx = std::max(0.f, std::max(box_min.x - capsule_center.x,
                                             capsule_center.x - box_max.x));
    const float dz = std::max(0.f, std::max(box_min.z - capsule_center.z,
                                             capsule_center.z - box_max.z));

    // Segment Y range vs box Y range.
    const float seg_min = capsule_center.y - half_height;
    const float seg_max = capsule_center.y + half_height;
    const float dy = std::max(0.f, std::max(box_min.y - seg_max,
                                             seg_min - box_max.y));

    return dx * dx + dy * dy + dz * dz <= radius * radius;
}

// ---------------------------------------------------------------------------
// Hit-window test
// ---------------------------------------------------------------------------

bool try_hit_in_window(CombatController& combat,
                       const AnimationState& anim,
                       const AttackDef& def,
                       const engine::movement::Transform& attacker,
                       const engine::movement::Transform& target,
                       const engine::movement::Collider& target_collider) {
    if (combat.hit_consumed) {
        return false;
    }
    if (combat.phase != CombatPhase::Attacking) {
        return false;
    }

    // Compute normalized clip time using the clip duration from the combat budget.
    // We use clip_remaining and the original duration (stored in def implicitly via
    // hit_window which is normalized). We need to derive normalized time from
    // anim.time_seconds and the original clip duration.
    // Normalized time is simply anim.time_seconds / clip_duration.
    // Since we don't have clip_duration here directly, and the hit window
    // is [hit_start, hit_end] in normalized [0,1] time, we derive it from
    // anim.time_seconds. The AnimationController caps time to duration on non-looping clips.
    // For attack clips anim.looping = false; we just check time directly.
    // The AnimClip duration is needed. We approximate via: if anim.time_seconds
    // maps to the current frame, and the hit window is normalized, we need duration.
    // Solution: the caller passes normalized time directly — but try_hit_in_window
    // uses anim directly. We compute via: time / some_duration.
    // Simplest correct approach: evaluate time / def total duration if available.
    // Since AttackDef doesn't store clip_duration, use a convention: the hit window
    // is checked against the raw anim.time_seconds relative to the clip_remaining
    // budget that was set to clip_duration at attack start. We store the original
    // duration in the CombatController's clip_remaining field at attack start.
    // But clip_remaining is counting DOWN. We don't know the original value.
    //
    // Practical v1 solution: store the original clip duration in CombatController
    // so we can compute normalized time here. For now use a simpler approach:
    // convert hit_window from normalized to seconds using a known-duration lookup.
    // Since we have anim.time_seconds and the combat started at time=0, and
    // clip_remaining started at clip_duration, normalized = anim.time_seconds / original_duration.
    // We CAN compute it from: anim.time_seconds (0..clip_duration) vs def.hit_window.
    //
    // The cleanest approach for v1: AnimationController::normalized_time() expects
    // an AnimClip* which we don't have. So we accept that we check time directly:
    // the hit window was DESIGNED for the full clip duration.
    // Store clip_duration at attack start in CombatController.clip_remaining start value.
    // For the normalized check: if clip runs [0..D], window is [hit_start*D, hit_end*D].
    // Since we can't recover D from here, ask the caller to pass it, OR
    // accept raw time check with the spec's normalized window scaled by actual clip duration.
    //
    // For v1, we use a DIFFERENT approach: we check whether anim.time_seconds /
    // (anim.time_seconds + combat.clip_remaining) — but clip_remaining decrements
    // with the same dt, so: original_duration = anim.time_seconds + combat.clip_remaining.
    // Then normalized = anim.time_seconds / original_duration.

    const float elapsed = anim.time_seconds;
    const float remaining = combat.clip_remaining;
    // If remaining < 0, clip already ended — still allow hits that were in window.
    const float original_duration = elapsed + (remaining > 0.f ? remaining : 0.f);
    const float normalized = (original_duration > 1e-5f)
                                 ? elapsed / original_duration
                                 : 1.f;

    if (normalized < def.hit_start || normalized > def.hit_end) {
        return false;
    }

    // Compute hit capsule position: player pos + attack_yaw forward * range.
    const float yaw = combat.attack_yaw;
    const glm::vec3 forward(std::sin(yaw), 0.f, std::cos(yaw));
    const glm::vec3 hit_center = attacker.position + forward * def.range;

    // Target collider as AABB.
    glm::vec3 box_half{0.4f, 0.9f, 0.4f}; // fallback
    if (target_collider.shape == engine::movement::ColliderShape::Box) {
        box_half = target_collider.half_extents;
    }

    const bool hit = capsule_intersects_box(
        hit_center, def.radius, def.radius * 0.5f,
        target.position, box_half);

    if (hit) {
        combat.hit_consumed = true;
    }
    return hit;
}

} // namespace engine::character
