#pragma once

#include "engine/character/core/CharacterAsset.hpp"
#include "engine/render/PassExtension.hpp"

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>
#include <volk.h>

namespace engine {
class VulkanContext;
}

namespace engine::character {

// Vulkan pass extension that draws GPU-skinned character meshes.
// Register characters before the renderer initializes; supply bone matrices + model
// per frame before calling render_frame().
class CharacterPass : public IPassExtension {
public:
    static constexpr int kMaxCharacters = 4;
    static constexpr int kMaxBones = 128;

    // Register a character asset. Returns a handle used by set_pose / remove_character.
    // May be called before or after on_renderer_init; buffers are created lazily.
    [[nodiscard]] int add_character(const CharacterAsset& asset);

    // Upload the current animation pose for frame_slot.
    // Call once per frame before Renderer::render_frame.
    void set_pose(int handle,
                  std::uint32_t frame_slot,
                  const glm::mat4& model,
                  const std::vector<glm::mat4>& bone_matrices);

    // IPassExtension
    void on_renderer_init(const PassExtensionInitContext& ctx) override;
    void on_renderer_shutdown(VulkanContext& context) override;
    void on_swapchain_recreate(const PassExtensionInitContext& ctx) override;
    void record(const PassExtensionRecordContext& ctx) override;

private:
    struct GpuMesh {
        VkBuffer vertex_buffer = VK_NULL_HANDLE;
        VkDeviceMemory vertex_memory = VK_NULL_HANDLE;
        VkBuffer index_buffer = VK_NULL_HANDLE;
        VkDeviceMemory index_memory = VK_NULL_HANDLE;
        std::uint32_t index_count = 0;
    };

    struct GpuTexture {
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        VkSampler sampler = VK_NULL_HANDLE;
    };

    struct BoneBuffer {
        VkBuffer buffer = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        void* mapped = nullptr;
    };

    struct GpuCharacter {
        bool initialized = false;
        GpuMesh mesh{};
        GpuTexture texture{};
        std::vector<BoneBuffer> bone_buffers;   // [frame_slot]
        std::vector<VkBuffer> frame_ubos;       // [frame_slot] view/proj UBO
        std::vector<VkDeviceMemory> frame_mems; // [frame_slot]
        std::vector<void*> frame_mapped;        // [frame_slot]
        std::vector<VkDescriptorSet> descriptor_sets; // [frame_slot]
        glm::mat4 model{1.f};
    };

    struct FrameUniformData {
        glm::mat4 view{1.f};
        glm::mat4 proj{1.f};
    };

    bool init_character_gpu(int idx);
    void destroy_character_gpu(int idx);

    bool create_pipeline(VkRenderPass render_pass);
    void destroy_pipeline();

    bool upload_mesh(GpuMesh& gpu, const SkinnedMeshData& mesh);
    bool upload_texture(GpuTexture& gpu, const SkinnedMeshData& mesh);
    bool create_bone_buffers(GpuCharacter& ch);
    bool create_frame_ubos(GpuCharacter& ch);
    bool alloc_descriptor_sets(GpuCharacter& ch, int char_idx);

    VkBuffer create_buffer(VkDeviceSize size,
                           VkBufferUsageFlags usage,
                           VkMemoryPropertyFlags props,
                           VkDeviceMemory& out_memory);

    VkCommandBuffer begin_one_shot() const;
    void end_one_shot(VkCommandBuffer cmd) const;

    std::uint32_t find_memory_type(std::uint32_t type_filter,
                                   VkMemoryPropertyFlags props) const;

    VulkanContext* context_ = nullptr;
    VkRenderPass render_pass_ = VK_NULL_HANDLE;
    std::uint32_t frames_in_flight_ = 0;

    VkDescriptorSetLayout descriptor_set_layout_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptor_pool_ = VK_NULL_HANDLE;
    VkPipelineLayout pipeline_layout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    std::vector<VkShaderModule> shader_modules_;

    VkCommandPool one_shot_pool_ = VK_NULL_HANDLE;

    struct PendingCharacter {
        CharacterAsset asset;
    };

    std::vector<PendingCharacter> pending_;
    std::vector<GpuCharacter> gpu_chars_;
};

} // namespace engine::character
