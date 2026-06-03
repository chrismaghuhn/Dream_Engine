#pragma once

#include "engine/movement/core/CollisionWorld.hpp"
#include "engine/movement/core/Components.hpp"
#include "engine/movement/core/InputSnapshot.hpp"

#include <vector>

#include <glm/glm.hpp>

namespace engine::movement {

class MovementWorld;

// Simulation-truth debug output for one player step. Held by the app layer for
// debug draw; never persisted.
struct MoveDebug {
    bool grounded = false;
    bool wall_contact = false;
    bool probe_hit = false;
    glm::vec3 probe_point{0.f};
    std::vector<glm::vec3> contact_points;
    std::vector<glm::vec3> contact_normals;
};

// Tuning that is not part of the per-entity component (kept out of the strict
// definition schema to keep it minimal, per spec 7.3).
struct MovementTuning {
    float sprint_multiplier = 1.8f;
    float ground_friction = 12.f;
    float air_control = 0.25f;
    float ground_probe_distance = 0.08f;
};

// Populate a CollisionWorld from the world's static colliders (boxes + ground
// plane). Capsule colliders (the kinematic player) are skipped.
void build_collision_world(const MovementWorld& world, CollisionWorld& out);

// Advance one fixed step for a player entity. Mutates `input` (clears
// jump_pressed once applied), writes the new position/yaw into `transform`, and
// updates `pc.velocity`/`pc.grounded`. `camera_yaw` provides the movement basis.
MoveDebug player_tick(Transform& transform,
                      PlayerController& pc,
                      const CapsuleCollider& capsule,
                      const CollisionWorld& world,
                      InputSnapshot& input,
                      float camera_yaw,
                      float dt,
                      const MovementTuning& tuning = MovementTuning{});

} // namespace engine::movement
