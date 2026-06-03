#pragma once
#include <glm/glm.hpp>

namespace engine::movement {

struct InputSnapshot {
    bool move_forward = false;
    bool move_back    = false;
    bool move_left    = false;
    bool move_right   = false;
    bool sprint       = false;

    bool jump_pressed    = false;  // edge-triggered, Space
    bool dodge_pressed   = false;  // edge-triggered, Space (same key — jump cleared first, dodge used in combat)

    // Edge-triggered attack buttons (cleared by InputBuffer push)
    bool attack_light   = false;  // LMB
    bool attack_heavy   = false;  // RMB
    bool attack_kick    = false;  // Q
    bool attack_special = false;  // E

    glm::vec2 mouse_delta{0.f};
    float     scroll_delta = 0.f;

    [[nodiscard]] bool any_move() const {
        return move_forward || move_back || move_left || move_right;
    }
    [[nodiscard]] bool any_attack() const {
        return attack_light || attack_heavy || attack_kick || attack_special;
    }
};

} // namespace engine::movement
