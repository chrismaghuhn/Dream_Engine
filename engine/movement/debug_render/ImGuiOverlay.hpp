#pragma once

#include "engine/render/PassExtension.hpp"

#include <glm/glm.hpp>

struct GLFWwindow;

namespace engine {
class Renderer;
}

namespace engine::movement {

// Live values shown in the movement debug panel.
struct MovementOverlayState {
    bool grounded = false;
    bool wall_contact = false;
    glm::vec3 velocity{0.f};
    glm::vec3 position{0.f};
    float yaw = 0.f;   // radians
    float pitch = 0.f; // radians
    float fps = 0.f;
    bool depth_test = true;

    // Fixed-step / interpolation diagnostics (spec 13 optional display).
    int sim_steps_last_frame = 0;
    float accumulator_alpha = 0.f;
    const char* persistent_id = "";

    // Combat state.
    const char* combat_phase = "Idle";
    const char* active_clip  = "";
    int combo_index = 0;

    // Hit-window diagnostics.
    float hit_window_start = 0.f;
    float hit_window_end   = 0.f;
    float normalized_clip_time = 0.f;
    bool  hit_consumed     = false;
    float attack_yaw_deg   = 0.f;
    bool  in_hit_window    = false;
};

// Minimal ImGui overlay rendered as a pass extension (after debug lines). Owns
// its own ImGui context / Vulkan backend so movement_test needs no engine_core.
class ImGuiOverlay : public IPassExtension {
public:
    void set_window(GLFWwindow* window) { window_ = window; }
    void set_renderer(Renderer* renderer) { renderer_ = renderer; }

    // Begin a new ImGui frame and build the panel from `state`. Call once per
    // frame before Renderer::render_frame.
    void new_frame(const MovementOverlayState& state);

    [[nodiscard]] bool initialized() const { return initialized_; }

    // IPassExtension
    void on_renderer_init(const PassExtensionInitContext& ctx) override;
    void on_renderer_shutdown(VulkanContext& context) override;
    void on_swapchain_recreate(const PassExtensionInitContext& ctx) override;
    void record(const PassExtensionRecordContext& ctx) override;

private:
    GLFWwindow* window_ = nullptr;
    Renderer* renderer_ = nullptr;
    VkDescriptorPool descriptor_pool_ = VK_NULL_HANDLE;
    VulkanContext* context_ = nullptr;
    bool initialized_ = false;
    bool frame_started_ = false;
};

} // namespace engine::movement
