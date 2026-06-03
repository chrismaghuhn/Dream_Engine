#pragma once

#include "engine/gameplay/Inventory.hpp"

#include <cstdint>

#include <vulkan/vulkan.h>

struct GLFWwindow;

namespace engine {

class Platform;
class Renderer;

struct UiOverlayStats {
    float fps = 0.f;
    std::uint64_t sim_tick = 0;
    std::uint32_t loaded_chunks = 0;
};

struct UiInventoryState {
    Inventory* inventory = nullptr;
    bool inventory_open = false;
};

class UiHost {
public:
    bool init(Platform& platform, Renderer& renderer);
    void shutdown();

    void new_frame(const UiOverlayStats& stats, UiInventoryState& inventory_ui);
    void render(VkCommandBuffer command_buffer);
    void on_swapchain_recreated(Renderer& renderer);

    [[nodiscard]] bool initialized() const { return initialized_; }

private:
    void destroy_descriptor_pool(VkDevice device);
    void draw_inventory_ui(UiInventoryState& inventory_ui);

    GLFWwindow* window_ = nullptr;
    VkDevice device_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptor_pool_ = VK_NULL_HANDLE;
    bool initialized_ = false;
};

} // namespace engine
