#pragma once

#include "engine/render/VulkanContext.hpp"
#include "engine/render/WorldRenderSnapshot.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <vector>

#include <glm/glm.hpp>
#include <volk.h>

namespace engine {

class TerrainImpostorPass {
public:
    struct FrameUniformGpu {
        alignas(16) glm::mat4 view{1.f};
        alignas(16) glm::mat4 proj{1.f};
        alignas(16) glm::vec4 render_origin{0.f};
        alignas(16) glm::vec4 ambient_fog{0.55f, 0.58f, 0.62f, 0.f};
    };

    struct DrawPushConstants {
        glm::vec3 model_translation{0.f};
        float min_y = 0.f;
        float max_y = 0.f;
        glm::vec3 color{0.45f};
        float pad = 0.f;
    };

    TerrainImpostorPass() = default;
    ~TerrainImpostorPass();

    TerrainImpostorPass(const TerrainImpostorPass&) = delete;
    TerrainImpostorPass& operator=(const TerrainImpostorPass&) = delete;

    bool init(VulkanContext& context, VkRenderPass render_pass, const std::filesystem::path& shader_dir);
    void shutdown();

    void write_frame_uniforms(std::uint64_t frame_index, const FrameUniformGpu& uniforms);

    void record(VkCommandBuffer command_buffer,
                std::uint64_t frame_index,
                const WorldRenderSnapshot& snapshot,
                VkExtent2D extent);

private:
    bool create_static_geometry();
    bool create_frame_uniform_buffers();
    bool create_descriptor_layout();
    bool create_pipeline(VkRenderPass render_pass, const std::filesystem::path& shader_dir);
    bool load_shader_module(const std::filesystem::path& path, VkShaderModule& out_module);

    VulkanContext* context_ = nullptr;

    VkPipelineLayout pipeline_layout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptor_layout_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptor_pool_ = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> descriptor_sets_;

    VkBuffer vertex_buffer_ = VK_NULL_HANDLE;
    VkDeviceMemory vertex_memory_ = VK_NULL_HANDLE;
    VkBuffer index_buffer_ = VK_NULL_HANDLE;
    VkDeviceMemory index_memory_ = VK_NULL_HANDLE;
    std::uint32_t index_count_ = 0;

    static constexpr std::uint32_t kFramesInFlight = 2;
    std::array<VkBuffer, kFramesInFlight> frame_ubos_{};
    std::array<VkDeviceMemory, kFramesInFlight> frame_ubo_memories_{};

    std::vector<VkShaderModule> shader_modules_;
};

} // namespace engine