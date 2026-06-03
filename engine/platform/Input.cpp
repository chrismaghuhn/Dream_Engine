#include "engine/platform/Input.hpp"

#include <GLFW/glfw3.h>

namespace engine {

void Input::set_cursor_captured(GLFWwindow* window, bool captured) {
    if (!window) {
        return;
    }

    cursor_captured_ = captured;
    glfwSetInputMode(window, GLFW_CURSOR, captured ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
    if (captured) {
        first_mouse_ = true;
    }
}

void Input::begin_frame(GLFWwindow* window) {
    if (!window) {
        delta_time_ = 0.f;
        mouse_delta_x_ = 0.f;
        mouse_delta_y_ = 0.f;
        return;
    }

    const double now = glfwGetTime();
    if (last_time_ > 0.0) {
        delta_time_ = static_cast<float>(now - last_time_);
    } else {
        delta_time_ = 0.f;
    }
    last_time_ = now;

    update_keys(window);
    update_mouse(window);
}

void Input::update_keys(GLFWwindow* window) {
    move_forward_ = glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS;
    move_back_ = glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS;
    move_left_ = glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS;
    move_right_ = glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS;
    move_up_ = glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS;
    move_down_ = glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS ||
                 glfwGetKey(window, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS;
}

void Input::update_mouse(GLFWwindow* window) {
    mouse_delta_x_ = 0.f;
    mouse_delta_y_ = 0.f;

    if (!cursor_captured_) {
        return;
    }

    double cursor_x = 0.0;
    double cursor_y = 0.0;
    glfwGetCursorPos(window, &cursor_x, &cursor_y);

    if (first_mouse_) {
        last_cursor_x_ = cursor_x;
        last_cursor_y_ = cursor_y;
        first_mouse_ = false;
        return;
    }

    mouse_delta_x_ = static_cast<float>(cursor_x - last_cursor_x_);
    mouse_delta_y_ = static_cast<float>(cursor_y - last_cursor_y_);
    last_cursor_x_ = cursor_x;
    last_cursor_y_ = cursor_y;
}

} // namespace engine
