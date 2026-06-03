#pragma once

#include "engine/core/EngineConfig.hpp"
#include "engine/render/SnapshotRing.hpp"
#include "engine/render/VulkanContext.hpp"

#include <cstdint>
#include <vector>

#include <vulkan/vulkan.h>

namespace engine {

class Platform;

class Renderer {
public:
    static constexpr std::uint32_t kFramesInFlight = 2;

    Renderer() = default;
    ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    bool init(Platform& platform);
    void shutdown();

    [[nodiscard]] bool initialized() const { return initialized_; }
    [[nodiscard]] const GpuCaps& gpu_caps() const { return gpu_caps_; }
    [[nodiscard]] SnapshotRing& snapshot_ring() { return snapshot_ring_; }

    void render_frame();

private:
    bool create_render_pass();
    bool create_framebuffers();
    bool create_command_pool_and_buffers();
    bool create_sync_objects();
    void destroy_frame_resources();
    void destroy_swapchain_resources();
    void recreate_swapchain();

    bool begin_frame(std::uint32_t& image_index);
    void end_frame(std::uint32_t image_index, std::uint32_t snapshot_slot);
    void record_clear_pass(std::uint32_t image_index);

    VulkanContext context_{};
    SnapshotRing snapshot_ring_{kFramesInFlight};

    VkRenderPass render_pass_ = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> framebuffers_;
    VkCommandPool command_pool_ = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> command_buffers_;

    std::vector<VkSemaphore> image_available_semaphores_;
    std::vector<VkSemaphore> render_finished_semaphores_;

    GpuCaps gpu_caps_{};
    std::uint32_t frame_index_ = 0;
    bool initialized_ = false;
};

} // namespace engine
