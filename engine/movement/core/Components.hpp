#pragma once

#include "engine/movement/core/EntityWorld.hpp"

#include <string>

#include <glm/glm.hpp>

namespace engine::movement {

// Per-entity simulation transform. Stores both the current fixed-step state and
// the previous fixed-step state so the renderer can interpolate. Per the spec,
// previous_* is transient and is never serialized; on load it is reset to the
// current state to avoid interpolating from a stale position.
struct Transform {
    glm::vec3 position{0.f};
    float yaw = 0.f;
    glm::vec3 previous_position{0.f};
    float previous_yaw = 0.f;

    void sync_previous() {
        previous_position = position;
        previous_yaw = yaw;
    }
};

enum class ColliderShape { Capsule, Box, GroundPlane };

// Collision shape description. v1 supports a player capsule, static boxes, and a
// ground plane. Dimensions are interpreted per shape.
struct Collider {
    ColliderShape shape = ColliderShape::Box;
    bool is_static = true;

    // Capsule: total height (cap-to-cap) + radius.
    float radius = 0.4f;
    float height = 1.75f;

    // Box: half extents on each axis.
    glm::vec3 half_extents{0.5f};

    // GroundPlane: plane height stored in half_extents.y is unused; the plane
    // sits at the entity's transform Y.
};

// Player movement state + tuning. Definition-driven config (speed/jump/gravity)
// and persistent runtime state (velocity/grounded).
struct PlayerController {
    float speed = 4.0f;
    float jump_velocity = 5.5f;
    float gravity = 9.81f;

    glm::vec3 velocity{0.f};
    bool grounded = false;
};

// Third-person orbit camera state attached to an entity (usually the player).
struct CameraRig {
    float yaw = 0.f;   // radians
    float pitch = 0.f; // radians (clamped)
    float distance = 4.0f;
    float height = 1.5f;
    EntityId target{};
};

// Optional human-readable label for debugging/overlay.
struct DebugName {
    std::string value;
};

} // namespace engine::movement
