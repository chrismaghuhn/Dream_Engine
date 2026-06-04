#pragma once

#include "engine/render/GpuMeshPool.hpp"
#include "engine/render/PerFrameGpuWrites.hpp"
#include "engine/render/VulkanContext.hpp"
#include "engine/render/WorldRenderSnapshot.hpp"

#include <cstdint>
#include <filesystem>
#include <vector>

#include <glm/glm.hpp>
#include <volk.h>

namespace engine {

/// Transparent water pass (§18 pass 3): depth test on, write off, alpha blend.
class WaterPass {
public:
    struct DrawPushConstants {
        glm::vec3 model_translation{0.f};
        float vertex_scale = 1.f;
    };

    WaterPass() = default;
    ~WaterPass();

    WaterPass(const WaterPass&) = delete;
    WaterPass& operator=(const WaterPass&) = delete;

    bool init(VulkanContext& context,
              VkRenderPass render_pass,
              const std::filesystem::path& shader_dir,
              PerFrameGpuWriteRing& per_frame_writes,
              VkDescriptorSetLayout frame_descriptor_layout);
    void shutdown();

    void write_indirect_commands(std::uint64_t frame_index,
                                 const WorldRenderSnapshot& snapshot,
                                 const GpuMeshPool& mesh_pool,
                                 std::size_t opaque_draw_count);

    void record(VkCommandBuffer command_buffer,
                std::uint64_t frame_index,
                const WorldRenderSnapshot& snapshot,
                const GpuMeshPool& mesh_pool,
                VkDescriptorSet frame_descriptor_set,
                VkExtent2D extent,
                std::size_t opaque_draw_count);

private:
    bool create_pipeline(VkRenderPass render_pass,
                         const std::filesystem::path& shader_dir,
                         VkDescriptorSetLayout frame_descriptor_layout);
    bool load_shader_module(const std::filesystem::path& path, VkShaderModule& out_module);

    VulkanContext* context_ = nullptr;
    PerFrameGpuWriteRing* per_frame_writes_ = nullptr;

    VkPipelineLayout pipeline_layout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    std::vector<VkShaderModule> shader_modules_;
};

} // namespace engine
