#include "engine/movement/core/CollisionWorld.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace engine::movement {

namespace {

constexpr float kEpsilon = 1e-5f;

glm::vec3 clamp_vec(const glm::vec3& v, const glm::vec3& lo, const glm::vec3& hi) {
    return glm::vec3(std::clamp(v.x, lo.x, hi.x),
                     std::clamp(v.y, lo.y, hi.y),
                     std::clamp(v.z, lo.z, hi.z));
}

} // namespace

ContactResult CollisionWorld::resolve(const CapsuleCollider& capsule, glm::vec3 position) const {
    ContactResult result;
    result.position = position;

    glm::vec3 last_normal(0.f);

    for (int iter = 0; iter < kMaxSubsteps; ++iter) {
        bool any_contact = false;

        if (ground_enabled_) {
            const float bottom = result.position.y - capsule.total_half_height();
            if (bottom < ground_y_) {
                const float penetration = ground_y_ - bottom;
                result.position.y += penetration;
                const glm::vec3 normal(0.f, 1.f, 0.f);
                last_normal = normal;
                result.hit = true;
                result.grounded = true;
                any_contact = true;
                if (iter == 0) {
                    result.contact_points.push_back(
                        glm::vec3(result.position.x, ground_y_, result.position.z));
                    result.contact_normals.push_back(normal);
                }
            }
        }

        for (const BoxCollider& box : boxes_) {
            const float seg_min = result.position.y - capsule.half_height;
            const float seg_max = result.position.y + capsule.half_height;
            const float sphere_y = std::clamp(box.center.y, seg_min, seg_max);
            const glm::vec3 sphere(result.position.x, sphere_y, result.position.z);

            const glm::vec3 closest = clamp_vec(sphere, box.min(), box.max());
            glm::vec3 diff = sphere - closest;
            float dist2 = glm::dot(diff, diff);

            if (dist2 < capsule.radius * capsule.radius) {
                glm::vec3 normal;
                float penetration;
                if (dist2 > kEpsilon) {
                    const float dist = std::sqrt(dist2);
                    normal = diff / dist;
                    penetration = capsule.radius - dist;
                } else {
                    const glm::vec3 to_min = sphere - box.min();
                    const glm::vec3 to_max = box.max() - sphere;
                    float min_pen = to_min.x;
                    normal = glm::vec3(-1.f, 0.f, 0.f);
                    auto consider = [&](float pen, const glm::vec3& n) {
                        if (pen < min_pen) {
                            min_pen = pen;
                            normal = n;
                        }
                    };
                    consider(to_max.x, glm::vec3(1.f, 0.f, 0.f));
                    consider(to_min.y, glm::vec3(0.f, -1.f, 0.f));
                    consider(to_max.y, glm::vec3(0.f, 1.f, 0.f));
                    consider(to_min.z, glm::vec3(0.f, 0.f, -1.f));
                    consider(to_max.z, glm::vec3(0.f, 0.f, 1.f));
                    penetration = capsule.radius + min_pen;
                }

                result.position += normal * penetration;
                last_normal = normal;
                result.hit = true;
                any_contact = true;
                if (normal.y > kGroundThreshold) {
                    result.grounded = true;
                } else if (std::abs(normal.y) < kGroundThreshold) {
                    result.wall_contact = true;
                }
                if (iter == 0) {
                    result.contact_points.push_back(closest);
                    result.contact_normals.push_back(normal);
                }
            }
        }

        if (!any_contact) {
            break;
        }
    }

    result.normal = last_normal;
    return result;
}

GroundProbe CollisionWorld::probe_ground(const CapsuleCollider& capsule,
                                         glm::vec3 position,
                                         float max_distance) const {
    GroundProbe probe;
    const float bottom = position.y - capsule.total_half_height();
    float best_top = -std::numeric_limits<float>::infinity();

    if (ground_enabled_ && ground_y_ <= bottom + kEpsilon) {
        best_top = ground_y_;
    }

    for (const BoxCollider& box : boxes_) {
        // Only consider boxes the capsule is horizontally above.
        const glm::vec3 lo = box.min();
        const glm::vec3 hi = box.max();
        const bool over_x = position.x >= lo.x - capsule.radius && position.x <= hi.x + capsule.radius;
        const bool over_z = position.z >= lo.z - capsule.radius && position.z <= hi.z + capsule.radius;
        if (over_x && over_z && hi.y <= bottom + kEpsilon) {
            best_top = std::max(best_top, hi.y);
        }
    }

    if (best_top > -std::numeric_limits<float>::infinity()) {
        const float dist = bottom - best_top;
        if (dist <= max_distance) {
            probe.hit = true;
            probe.distance = dist;
            probe.point = glm::vec3(position.x, best_top, position.z);
        }
    }
    return probe;
}

} // namespace engine::movement
