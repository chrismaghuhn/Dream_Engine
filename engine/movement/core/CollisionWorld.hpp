#pragma once

#include <vector>

#include <glm/glm.hpp>

namespace engine::movement {

// Vertical capsule: a central segment of length 2*half_height along Y, with
// hemispherical caps of `radius`. `position` is the segment midpoint.
struct CapsuleCollider {
    float radius = 0.4f;
    float half_height = 0.5f;

    [[nodiscard]] float total_half_height() const { return half_height + radius; }

    // Build from a Collider component's (radius, total height) pair.
    [[nodiscard]] static CapsuleCollider from_dimensions(float radius, float total_height) {
        CapsuleCollider c;
        c.radius = radius;
        const float half = 0.5f * total_height - radius;
        c.half_height = half > 0.f ? half : 0.f;
        return c;
    }
};

// Axis-aligned box. v1 ignores rotation; arena boxes are axis aligned.
struct BoxCollider {
    glm::vec3 center{0.f};
    glm::vec3 half_extents{0.5f};

    [[nodiscard]] glm::vec3 min() const { return center - half_extents; }
    [[nodiscard]] glm::vec3 max() const { return center + half_extents; }
};

// Result of depenetrating a capsule against the world. Records contact points
// and normals so debug draw can show simulation-truth markers.
struct ContactResult {
    glm::vec3 position{0.f}; // depenetrated capsule position
    glm::vec3 normal{0.f};   // last resolved contact normal
    bool grounded = false;   // a contact had normal.y > kGroundThreshold
    bool wall_contact = false; // a contact had a near-horizontal normal
    bool hit = false;        // any depenetration occurred

    std::vector<glm::vec3> contact_points;
    std::vector<glm::vec3> contact_normals;
};

// Downward ground probe result, used to confirm grounding independently of
// wall contacts (spec 10.4: grounding must be separate from wall collision).
struct GroundProbe {
    bool hit = false;
    glm::vec3 point{0.f};
    float distance = 0.f;
};

class CollisionWorld {
public:
    static constexpr float kGroundThreshold = 0.7f;
    static constexpr int kMaxSubsteps = 3;

    void set_ground_plane(float y) {
        ground_y_ = y;
        ground_enabled_ = true;
    }
    void disable_ground_plane() { ground_enabled_ = false; }
    void add_box(const glm::vec3& center, const glm::vec3& half_extents) {
        boxes_.push_back(BoxCollider{center, half_extents});
    }
    void clear() {
        boxes_.clear();
        ground_enabled_ = false;
    }

    [[nodiscard]] const std::vector<BoxCollider>& boxes() const { return boxes_; }
    [[nodiscard]] bool ground_enabled() const { return ground_enabled_; }
    [[nodiscard]] float ground_y() const { return ground_y_; }

    // Iteratively push the capsule out of all colliders (up to kMaxSubsteps).
    [[nodiscard]] ContactResult resolve(const CapsuleCollider& capsule,
                                        glm::vec3 position) const;

    // Cast the capsule bottom downward up to max_distance; reports the nearest
    // floor (box top face or ground plane) directly below.
    [[nodiscard]] GroundProbe probe_ground(const CapsuleCollider& capsule,
                                           glm::vec3 position,
                                           float max_distance) const;

private:
    std::vector<BoxCollider> boxes_;
    float ground_y_ = 0.f;
    bool ground_enabled_ = false;
};

} // namespace engine::movement
