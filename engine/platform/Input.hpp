#pragma once

struct GLFWwindow;

namespace engine {

class Input {
public:
    void begin_frame(GLFWwindow* window);

    [[nodiscard]] float delta_time() const { return delta_time_; }
    [[nodiscard]] float mouse_delta_x() const { return mouse_delta_x_; }
    [[nodiscard]] float mouse_delta_y() const { return mouse_delta_y_; }

    [[nodiscard]] bool move_forward() const { return move_forward_; }
    [[nodiscard]] bool move_back() const { return move_back_; }
    [[nodiscard]] bool move_left() const { return move_left_; }
    [[nodiscard]] bool move_right() const { return move_right_; }
    [[nodiscard]] bool move_up() const { return move_up_; }
    [[nodiscard]] bool move_down() const { return move_down_; }

    [[nodiscard]] bool break_pressed() const { return break_pressed_; }
    [[nodiscard]] bool place_pressed() const { return place_pressed_; }
    [[nodiscard]] bool save_pressed() const { return save_pressed_; }
    [[nodiscard]] bool load_pressed() const { return load_pressed_; }

    void set_cursor_captured(GLFWwindow* window, bool captured);

private:
    void update_keys(GLFWwindow* window);
    void update_mouse(GLFWwindow* window);

    double last_time_ = 0.0;
    float delta_time_ = 0.f;
    float mouse_delta_x_ = 0.f;
    float mouse_delta_y_ = 0.f;
    double last_cursor_x_ = 0.0;
    double last_cursor_y_ = 0.0;
    bool first_mouse_ = true;
    bool cursor_captured_ = false;

    bool move_forward_ = false;
    bool move_back_ = false;
    bool move_left_ = false;
    bool move_right_ = false;
    bool move_up_ = false;
    bool move_down_ = false;

    bool break_down_ = false;
    bool place_down_ = false;
    bool save_down_ = false;
    bool load_down_ = false;
    bool break_pressed_ = false;
    bool place_pressed_ = false;
    bool save_pressed_ = false;
    bool load_pressed_ = false;
};

} // namespace engine
