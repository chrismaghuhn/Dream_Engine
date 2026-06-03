#pragma once

namespace engine {

struct PlayerMotorConfig {
    float gravity = -24.0f;
    float jump_velocity = 8.0f;
    float max_walk_speed = 5.0f;
    float ground_snap = 0.1f;
};

} // namespace engine
