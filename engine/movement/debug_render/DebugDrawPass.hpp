#pragma once

#include "engine/render/PassExtension.hpp"

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>
#include <volk.h>

namespace engine::movement {

// Immediate-mode debug line renderer plugged into the Renderer as a pass
// extension. Accumulate primitives each frame via push_*; record() uploads them
// to a per-frame host-visible vertex buffer and draws them as a single line list.
class DebugDrawPass : public IPassExtension {
public:
    struct LineVertex {
        glm::vec3 pos{0.f};
        std::uint32_t color = 0xFFFFFFFFu; // packed RGBA8 (R in low byte)
    };

    static constexpr std::uint32_t kMaxVertices = 65536;

    // Pack a colour to the RGBA8 layout expected by the shader.
    [[nodiscard]] static std::uint32_t rgba(std::uint8_t r,
                                            std::uint8_t g,
                                            std::uint8_t b,
                                            std::uint8_t a = 255);

    // Clear accumulated primitives. Call once at the start of each frame.
    void begin_frame() { vertices_.clear(); }

    void set_depth_test(bool enabled) { depth_test_ = enabled; }
    [[nodiscard]] bool depth_test() const { return depth_test_; }
    [[nodiscard]] std::size_t vertex_count() const { return vertices_.size(); }

    void push_line(const glm::vec3& a, const glm::vec3& b, std::uint32_t color);
    void push_box(const glm::vec3& center, const glm::vec3& half_extents, std::uint32_t color);
    // Vertical capsule whose segment midpoint is `center`.
    void push_capsule(const glm::vec3& center,
                      float radius,
                      float half_height,
                      std::uint32_t color);
    void push_arrow(const glm::vec3& origin,
                    const glm::vec3& dir,
                    float length,
                    std::uint32_t color);
    // Small 3-axis cross marker, e.g. for contact/probe points.
    void push_cross(const glm::vec3& center, float size, std::uint32_t color);

    // IPassExtension
    void on_renderer_init(const PassExtensionInitContext& ctx) override;
    void on_renderer_shutdown(VulkanContext& context) override;
    void on_swapchain_recreate(const PassExtensionInitContext& ctx) override;
    void record(const PassExtensionRecordContext& ctx) override;

private:
    struct FrameBuffer {
        VkBuffer buffer = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        void* mapped = nullptr;
    };

    bool create_pipelines(VkRenderPass render_pass);
    void destroy_pipelines();
    bool create_vertex_buffers();
    void destroy_vertex_buffers();
    void push_circle_xz(const glm::vec3& center, float radius, int segments, std::uint32_t color);
    void push_vertical_arc(const glm::vec3& center,
                           float radius,
                           const glm::vec3& plane_dir,
                           bool upper,
                           std::uint32_t color);

    VulkanContext* context_ = nullptr;
    VkPipelineLayout pipeline_layout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_depth_ = VK_NULL_HANDLE;
    VkPipeline pipeline_no_depth_ = VK_NULL_HANDLE;
    std::vector<VkShaderModule> shader_modules_;

    std::uint32_t frames_in_flight_ = 2;
    std::vector<FrameBuffer> vertex_buffers_;
    std::vector<LineVertex> vertices_;
    bool depth_test_ = true;
};

} // namespace engine::movement
