#include "engine/physics/VoxelCapsuleResolver.hpp"

#include <algorithm>
#include <cmath>
#include <functional>

namespace engine {

glm::vec3 capsule_half_extents(const Capsule& capsule, float skin_width) {
    const float inset = std::max(skin_width, 0.f);
    return {
        std::max(capsule.radius - inset, 0.01f),
        std::max(capsule.half_height + capsule.radius - inset, 0.01f),
        std::max(capsule.radius - inset, 0.01f),
    };
}

namespace {

constexpr float kResolveEpsilon = 1e-4f;

bool block_overlaps_capsule(
    const Capsule& capsule,
    float skin_width,
    int wx,
    int wy,
    int wz) {
    const glm::vec3 half = capsule_half_extents(capsule, skin_width);
    const glm::vec3 cmin = capsule.center - half;
    const glm::vec3 cmax = capsule.center + half;
    const glm::vec3 bmin(static_cast<float>(wx), static_cast<float>(wy), static_cast<float>(wz));
    const glm::vec3 bmax = bmin + glm::vec3{1.f};
    return cmin.x < bmax.x && cmax.x > bmin.x && cmin.y < bmax.y && cmax.y > bmin.y && cmin.z < bmax.z &&
           cmax.z > bmin.z;
}

void for_each_block_in_capsule_bounds(
    const Capsule& capsule,
    float skin_width,
    const std::function<void(int, int, int)>& fn) {
    const glm::vec3 half = capsule_half_extents(capsule, skin_width);
    const glm::ivec3 min_block = glm::ivec3(glm::floor(capsule.center - half));
    const glm::ivec3 max_block = glm::ivec3(glm::floor(capsule.center + half));

    for (int z = min_block.z; z <= max_block.z; ++z) {
        for (int y = min_block.y; y <= max_block.y; ++y) {
            for (int x = min_block.x; x <= max_block.x; ++x) {
                fn(x, y, z);
            }
        }
    }
}

} // namespace

bool capsule_overlaps_solid(
    const Capsule& capsule,
    ChunkStore& store,
    float skin_width,
    OccupancyPolicy policy) {
    bool overlaps = false;

    for_each_block_in_capsule_bounds(capsule, skin_width, [&](int x, int y, int z) {
        if (overlaps) {
            return;
        }
        if (!occupancy_at(x, y, z, store, policy)) {
            return;
        }
        if (block_overlaps_capsule(capsule, skin_width, x, y, z)) {
            overlaps = true;
        }
    });

    return overlaps;
}

bool capsule_on_ground(
    const Capsule& capsule,
    ChunkStore& store,
    float ground_probe,
    OccupancyPolicy policy) {
    const float foot_y = capsule.center.y - capsule.half_height - capsule.radius;
    const int probe_y = static_cast<int>(std::floor(foot_y - std::max(ground_probe, 0.01f)));
    const int r = static_cast<int>(std::ceil(capsule.radius));

    for (int dz = -r; dz <= r; ++dz) {
        for (int dx = -r; dx <= r; ++dx) {
            const float wx_f = capsule.center.x + static_cast<float>(dx);
            const float wz_f = capsule.center.z + static_cast<float>(dz);
            if (glm::length(glm::vec2{wx_f - capsule.center.x, wz_f - capsule.center.z}) > capsule.radius + 0.05f) {
                continue;
            }
            const int wx = static_cast<int>(std::floor(wx_f));
            const int wz = static_cast<int>(std::floor(wz_f));
            if (occupancy_at(wx, probe_y, wz, store, policy)) {
                return true;
            }
        }
    }
    return false;
}

namespace {

float resolve_axis_move(
    Capsule& capsule,
    float delta,
    int axis,
    ChunkStore& store,
    const VoxelMovementConfig& movement,
    OccupancyPolicy policy) {
    if (std::abs(delta) < kResolveEpsilon) {
        return delta;
    }

    const float original = capsule.center[axis];
    const float move_dir = delta > 0.f ? 1.f : -1.f;

    auto overlaps_at = [&](float offset) {
        capsule.center[axis] = original + offset;
        return capsule_overlaps_solid(capsule, store, movement.skin_width, policy);
    };

    const float start = capsule.center[axis];
    float lo = 0.f;
    float hi = delta;

    auto overlaps_from_start = [&](float offset) {
        capsule.center[axis] = start + offset;
        return capsule_overlaps_solid(capsule, store, movement.skin_width, policy);
    };

    if (!overlaps_from_start(hi)) {
        capsule.center[axis] = start + hi;
        return delta - (capsule.center[axis] - original);
    }

    if (!overlaps_from_start(0.f)) {
        while (hi - lo > kResolveEpsilon) {
            const float mid = (lo + hi) * 0.5f;
            if (overlaps_from_start(mid)) {
                hi = mid;
            } else {
                lo = mid;
            }
        }
    } else {
        lo = 0.f;
        hi = 0.f;
    }

    capsule.center[axis] = start + lo;
    return delta - (capsule.center[axis] - original);
}

void apply_gravity_snap(
    Capsule& capsule,
    glm::vec3& velocity,
    ChunkStore& store,
    float ground_snap,
    float skin_width,
    OccupancyPolicy policy) {
    if (!capsule_on_ground(capsule, store, 0.05f, policy)) {
        return;
    }

    if (velocity.y <= 0.f) {
        velocity.y = 0.f;
    }

    float probe = 0.f;
    while (probe < ground_snap) {
        capsule.center.y -= kResolveEpsilon;
        if (capsule_overlaps_solid(capsule, store, skin_width, policy)) {
            capsule.center.y += kResolveEpsilon;
            break;
        }
        probe += kResolveEpsilon;
    }
}

} // namespace

CapsuleMoveResult move_and_slide(
    const Capsule& capsule_in,
    glm::vec3 velocity,
    float delta_time,
    ChunkStore& store,
    const VoxelMovementConfig& movement,
    float ground_snap,
    OccupancyPolicy policy) {
    CapsuleMoveResult result{};
    result.velocity = velocity;
    Capsule capsule = capsule_in;

    const glm::vec3 desired = velocity * delta_time;
    const bool was_grounded = capsule_on_ground(capsule, store, 0.05f, policy);

    constexpr float kMaxSubstep = 0.25f;
    const float desired_len = glm::length(desired);
    const int substep_count =
        std::max(1, static_cast<int>(std::ceil(desired_len / kMaxSubstep)));
    const glm::vec3 substep_delta = desired / static_cast<float>(substep_count);

    for (int step = 0; step < substep_count; ++step) {
        for (int iter = 0; iter < movement.max_solver_iterations; ++iter) {
            (void)iter;

            const float vertical_left =
                resolve_axis_move(capsule, substep_delta.y, 1, store, movement, policy);
            if (std::abs(vertical_left) < std::abs(substep_delta.y) - kResolveEpsilon) {
                result.velocity.y = 0.f;
            }

            const glm::vec3 before_horizontal = capsule.center;
            const float horizontal_x =
                resolve_axis_move(capsule, substep_delta.x, 0, store, movement, policy);
            const float horizontal_z =
                resolve_axis_move(capsule, substep_delta.z, 2, store, movement, policy);

            const bool horizontal_blocked = std::abs(horizontal_x) > kResolveEpsilon ||
                                          std::abs(horizontal_z) > kResolveEpsilon;

            if (horizontal_blocked) {
                const glm::vec3 saved = before_horizontal;
                capsule.center = saved;
                capsule.center.y += movement.step_height;

                if (!capsule_overlaps_solid(capsule, store, movement.skin_width, policy)) {
                    (void)resolve_axis_move(capsule, substep_delta.x, 0, store, movement, policy);
                    (void)resolve_axis_move(capsule, substep_delta.z, 2, store, movement, policy);
                    (void)resolve_axis_move(
                        capsule,
                        -(movement.step_height + capsule.radius),
                        1,
                        store,
                        movement,
                        policy);
                } else {
                    capsule.center = saved;
                    (void)resolve_axis_move(capsule, substep_delta.x, 0, store, movement, policy);
                    (void)resolve_axis_move(capsule, substep_delta.z, 2, store, movement, policy);
                }

                if (std::abs(capsule.center.x - saved.x) < kResolveEpsilon &&
                    std::abs(capsule.center.z - saved.z) < kResolveEpsilon) {
                    result.velocity.x = 0.f;
                    result.velocity.z = 0.f;
                }
            }

            break;
        }
    }

    apply_gravity_snap(capsule, result.velocity, store, ground_snap, movement.skin_width, policy);

    result.center = capsule.center;
    result.on_ground = capsule_on_ground(capsule, store, 0.05f, policy);
    result.landed_this_tick = result.on_ground && !was_grounded && result.velocity.y <= 0.f;

    if (result.on_ground && result.velocity.y < 0.f) {
        result.velocity.y = 0.f;
    }

    return result;
}

} // namespace engine
