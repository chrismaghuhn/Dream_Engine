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
                       float clip_duration_seconds,
                       const engine::movement::Transform& attacker,
                       const engine::movement::Transform& target,
                       const engine::movement::Collider& target_collider) {
    if (combat.hit_consumed) {
        return false;
    }
    if (combat.phase != CombatPhase::Active) {
        return false;
    }

    const float normalized = attack_norm_time(anim.time_seconds, def, clip_duration_seconds);

    if (normalized < def.hit_start_norm || normalized > def.hit_end_norm) {
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
