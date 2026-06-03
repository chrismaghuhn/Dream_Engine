#pragma once

#include "engine/render/VulkanContext.hpp"

#include <cstdint>
#include <filesystem>
#include <vector>

#include <volk.h>

namespace engine {

/// Fullscreen triangle at far depth (§18 pass 2).
class SkyPass {
public:
    SkyPass() = default;
    ~SkyPass();

    SkyPass(const SkyPass&) = delete;
    SkyPass& operator=(const SkyPass&) = delete;

    bool init(VulkanContext& context, VkRenderPass render_pass, const std::filesystem::path& shader_dir);
    void shutdown();

    void record(VkCommandBuffer command_buffer, VkExtent2D extent);

private:
    bool create_pipeline(VkRenderPass render_pass, const std::filesystem::path& shader_dir);
    bool load_shader_module(const std::filesystem::path& path, VkShaderModule& out_module);

    VulkanContext* context_ = nullptr;
    VkPipelineLayout pipeline_layout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    std::vector<VkShaderModule> shader_modules_;
};

} // namespace engine
