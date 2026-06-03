#pragma once

struct GLFWwindow;

namespace engine {

class Platform {
public:
    Platform() = default;
    ~Platform();

    Platform(const Platform&) = delete;
    Platform& operator=(const Platform&) = delete;

    bool init(int width, int height, const char* title);
    void shutdown();

    void poll();
    [[nodiscard]] bool should_close() const;

    [[nodiscard]] GLFWwindow* window() const { return window_; }

private:
    GLFWwindow* window_ = nullptr;
    bool glfw_initialized_ = false;
};

} // namespace engine
