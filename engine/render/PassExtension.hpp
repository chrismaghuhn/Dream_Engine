#pragma once

#include "engine/render/VulkanContext.hpp"

#include <cstdint>

#include <glm/glm.hpp>
#include <vulkan/vulkan.h>

namespace engine {

// Lifecycle context handed to a pass extension on init / swapchain recreate.
struct PassExtensionInitContext {
    VulkanContext* context = nullptr;
    VkRenderPass render_pass = VK_NULL_HANDLE;
    VkExtent2D extent{};
    std::uint32_t frames_in_flight = 0;
};

// Per-frame context handed to a pass extension when recording draw commands.
struct PassExtensionRecordContext {
    VkCommandBuffer command_buffer = VK_NULL_HANDLE;
    std::uint32_t frame_index = 0; // global, monotonically increasing
    std::uint32_t frame_slot = 0;  // snapshot_slot (0..snapshot_count-1)
    VkExtent2D extent{};
    glm::mat4 view{1.f};
    glm::mat4 proj{1.f};
};

// A pluggable render pass recorded inside the main render pass at a named
// insertion point. Lets prototypes (debug draw, character pass, ...) add
// geometry without the core Renderer knowing about them specifically.
class IPassExtension {
public:
    virtual ~IPassExtension() = default;

    virtual void on_renderer_init(const PassExtensionInitContext& ctx) = 0;
    virtual void on_renderer_shutdown(VulkanContext& context) = 0;
    virtual void on_swapchain_recreate(const PassExtensionInitContext& ctx) = 0;
    virtual void record(const PassExtensionRecordContext& ctx) = 0;
};

// Named insertion points within Renderer::record_frame.
namespace pass_insertion {
inline constexpr const char* kBeforeImgui = "before_imgui";
} // namespace pass_insertion

} // namespace engine
