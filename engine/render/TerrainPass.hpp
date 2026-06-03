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

class TerrainPass {
public:
    struct FrameUniformGpu {
        alignas(16) glm::mat4 view{1.f};
        alignas(16) glm::mat4 proj{1.f};
        alignas(16) glm::vec4 render_origin{0.f};
    };

    struct DrawPushConstants {
        glm::vec3 model_translation{0.f};
        float pad = 0.f;
    };

    TerrainPass() = default;
    ~TerrainPass();

    TerrainPass(const TerrainPass&) = delete;
    TerrainPass& operator=(const TerrainPass&) = delete;

    bool init(VulkanContext& context,
              VkRenderPass render_pass,
              const std::filesystem::path& shader_dir,
              PerFrameGpuWriteRing& per_frame_writes);
    void shutdown();

    /// Point descriptor binding 1 (sampler2DArray) at the block texture array.
    /// Call once after the texture is uploaded, before the first frame.
    void bind_block_texture(VkImageView image_view, VkSampler sampler);

    void write_frame_uniforms(std::uint64_t frame_index, const FrameUniformGpu& uniforms);
    void write_indirect_commands(std::uint64_t frame_index,
                                 const WorldRenderSnapshot& snapshot,
                                 const GpuMeshPool& mesh_pool);

    void record(VkCommandBuffer command_buffer,
                std::uint64_t frame_index,
                const WorldRenderSnapshot& snapshot,
                const GpuMeshPool& mesh_pool,
                VkExtent2D extent);

    [[nodiscard]] VkDescriptorSetLayout descriptor_layout() const { return descriptor_layout_; }
    [[nodiscard]] VkDescriptorSet frame_descriptor_set(std::uint64_t frame_index) const;

private:
    bool create_pipeline(VkRenderPass render_pass, const std::filesystem::path& shader_dir);
    bool create_descriptor_layout();
    bool load_shader_module(const std::filesystem::path& path, VkShaderModule& out_module);

    VulkanContext* context_ = nullptr;
    PerFrameGpuWriteRing* per_frame_writes_ = nullptr;

    VkPipelineLayout pipeline_layout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptor_layout_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptor_pool_ = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> descriptor_sets_;
    std::vector<VkShaderModule> shader_modules_;
};

} // namespace engine
