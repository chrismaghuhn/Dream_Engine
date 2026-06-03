#include "engine/movement/core/PlayerMovement.hpp"

#include "engine/movement/core/MovementWorld.hpp"

#include <algorithm>
#include <cmath>

namespace engine::movement {

void build_collision_world(const MovementWorld& world, CollisionWorld& out) {
    out.clear();
    const TransformStore& transforms = world.transforms();
    world.colliders().for_each([&](std::uint32_t index, const Collider& collider) {
        const EntityId id = EntityId{index, 0u}; // index-only lookup into stores
        const Transform* tf = transforms.get(id);
        const glm::vec3 position = tf != nullptr ? tf->position : glm::vec3(0.f);
        switch (collider.shape) {
        case ColliderShape::Box:
            out.add_box(position, collider.half_extents);
            break;
        case ColliderShape::GroundPlane:
            out.set_ground_plane(position.y);
            break;
        case ColliderShape::Capsule:
            break; // kinematic player; not a static obstacle
        }
    });
}

MoveDebug player_tick(Transform& transform,
                      PlayerController& pc,
                      const CapsuleCollider& capsule,
                      const CollisionWorld& world,
                      InputSnapshot& input,
                      float camera_yaw,
                      float dt,
                      const MovementTuning& tuning) {
    glm::vec3 pos = transform.position;

    // Camera-relative basis on the XZ plane.
    const glm::vec3 forward(std::sin(camera_yaw), 0.f, std::cos(camera_yaw));
    const glm::vec3 right(std::cos(camera_yaw), 0.f, -std::sin(camera_yaw));

    glm::vec3 wish(0.f);
    wish += forward * ((input.move_forward ? 1.f : 0.f) - (input.move_back ? 1.f : 0.f));
    wish += right * ((input.move_right ? 1.f : 0.f) - (input.move_left ? 1.f : 0.f));
    const bool moving = glm::dot(wish, wish) > 1e-6f;
    if (moving) {
        wish = glm::normalize(wish);
    }

    const float target_speed = input.sprint ? pc.speed * tuning.sprint_multiplier : pc.speed;
    const glm::vec3 target_horizontal = wish * target_speed;

    const float blend = pc.grounded ? std::clamp(tuning.ground_friction * dt, 0.f, 1.f)
                                     : std::clamp(tuning.air_control, 0.f, 1.f);
    pc.velocity.x = glm::mix(pc.velocity.x, target_horizontal.x, blend);
    pc.velocity.z = glm::mix(pc.velocity.z, target_horizontal.z, blend);

    // Gravity.
    pc.velocity.y -= pc.gravity * dt;

    // Jump: edge-triggered, consumed on the first grounded step that reads it.
    if (input.jump_pressed && pc.grounded) {
        pc.velocity.y = pc.jump_velocity;
        pc.grounded = false;
        input.jump_pressed = false;
    }

    // Integrate then depenetrate.
    pos += pc.velocity * dt;
    const ContactResult contact = world.resolve(capsule, pos);
    pos = contact.position;

    // Grounding is separate from wall contact (spec 10.4): use the resolved
    // floor contact, plus a short downward probe while descending.
    bool grounded = contact.grounded;
    GroundProbe probe;
    if (!grounded && pc.velocity.y <= 0.f) {
        probe = world.probe_ground(capsule, pos, tuning.ground_probe_distance);
        if (probe.hit) {
            grounded = true;
        }
    }
    pc.grounded = grounded;

    // Stop-on-impact response (spec 10.3: no sliding in v1). Classify each
    // resolved contact and stop the relevant velocity component outright:
    //  - floor  -> zero downward velocity on landing (spec 10.4)
    //  - ceiling -> zero upward velocity
    //  - wall   -> zero ALL horizontal velocity (no sliding along the wall)
    if (contact.hit) {
        bool floor = false;
        bool ceiling = false;
        bool wall = false;
        const auto classify = [&](const glm::vec3& n) {
            if (n.y > CollisionWorld::kGroundThreshold) {
                floor = true;
            } else if (n.y < -CollisionWorld::kGroundThreshold) {
                ceiling = true;
            } else {
                wall = true;
            }
        };
        if (contact.contact_normals.empty()) {
            classify(contact.normal);
        } else {
            for (const glm::vec3& n : contact.contact_normals) {
                classify(n);
            }
        }

        if (floor && pc.velocity.y < 0.f) {
            pc.velocity.y = 0.f;
        }
        if (ceiling && pc.velocity.y > 0.f) {
            pc.velocity.y = 0.f;
        }
        if (wall) {
            pc.velocity.x = 0.f;
            pc.velocity.z = 0.f;
        }
    }

    // Player facing is movement-driven and explicit (spec 11.2).
    const glm::vec3 horizontal_vel(pc.velocity.x, 0.f, pc.velocity.z);
    if (glm::dot(horizontal_vel, horizontal_vel) > 0.04f) {
        transform.yaw = std::atan2(horizontal_vel.x, horizontal_vel.z);
    }

    transform.position = pos;

    MoveDebug debug;
    debug.grounded = pc.grounded;
    debug.wall_contact = contact.wall_contact;
    debug.probe_hit = probe.hit;
    debug.probe_point = probe.point;
    debug.contact_points = contact.contact_points;
    debug.contact_normals = contact.contact_normals;
    return debug;
}

} // namespace engine::movement
