#include "engine/platform/Platform.hpp"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif

#include <GLFW/glfw3.h>

namespace engine {

Platform::~Platform() {
    shutdown();
}

bool Platform::init(int width, int height, const char* title) {
    if (window_) {
        return true;
    }

    if (glfwInit() != GLFW_TRUE) {
        return false;
    }
    glfw_initialized_ = true;

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

    window_ = glfwCreateWindow(width, height, title, nullptr, nullptr);
    if (!window_) {
        shutdown();
        return false;
    }

    return true;
}

void Platform::shutdown() {
    if (window_) {
        glfwDestroyWindow(window_);
        window_ = nullptr;
    }

    if (glfw_initialized_) {
        glfwTerminate();
        glfw_initialized_ = false;
    }
}

void Platform::poll() {
    if (!window_) {
        return;
    }

    glfwPollEvents();

    if (glfwGetKey(window_, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
        glfwSetWindowShouldClose(window_, GLFW_TRUE);
    }
}

bool Platform::should_close() const {
    return window_ == nullptr || glfwWindowShouldClose(window_) == GLFW_TRUE;
}

} // namespace engine
