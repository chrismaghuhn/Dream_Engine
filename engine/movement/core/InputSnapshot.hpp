#pragma once

#include <glm/glm.hpp>

namespace engine::movement {

// Input sampled once per display frame and then consumed across zero, one, or
// more fixed sim steps. Edge-triggered actions (jump) are cleared by the first
// sim step that consumes them so they fire at most once per frame, regardless
// of how many sim steps run that frame.
struct InputSnapshot {
    bool move_forward = false; // W
    bool move_back = false;    // S
    bool move_left = false;    // A
    bool move_right = false;   // D
    bool sprint = false;       // Shift

    // Edge-triggered: true only on the frame the jump key transitions to down.
    // PlayerController sets this back to false once it has applied the jump.
    bool jump_pressed = false;

    // Edge-triggered: true on the frame LMB transitions to down.
    // CombatController sets this to false once a combo attack starts.
    bool attack_pressed = false;

    glm::vec2 mouse_delta{0.f}; // pixels moved this frame (x = yaw, y = pitch)
    float scroll_delta = 0.f;   // wheel ticks this frame

    [[nodiscard]] bool any_move() const {
        return move_forward || move_back || move_left || move_right;
    }
};

} // namespace engine::movement
