#pragma once

#include "engine/core/EngineConfig.hpp"
#include "engine/render/BlockTextureArray.hpp"
#include "engine/render/GpuDeferredFreeQueue.hpp"
#include "engine/render/GpuMeshPool.hpp"
#include "engine/render/MeshUploadQueue.hpp"
#include "engine/render/PassExtension.hpp"
#include "engine/render/PerFrameGpuWrites.hpp"
#include "engine/render/SnapshotRing.hpp"
#include "engine/render/ShaderManager.hpp"
#include "engine/render/SkyPass.hpp"
#include "engine/render/TerrainImpostorPass.hpp"
#include "engine/render/TerrainPass.hpp"
#include "engine/render/WaterPass.hpp"
#include "engine/render/VulkanContext.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <glm/glm.hpp>
#include <vulkan/vulkan.h>

namespace engine {

class Platform;
class UiHost;

class Renderer {
public:
    static constexpr std::uint32_t kFramesInFlight = 2;
    /// Max opaque draws per frame (snapshot + terrain pass).
    static constexpr std::size_t kMaxIndirectDraws = 512;
    /// Water indirect commands are packed after opaque reserved slots in the per-frame buffer.
    static constexpr std::size_t kMaxWaterIndirectDraws = 128;
    static constexpr std::size_t kIndirectBufferDraws =
        kMaxIndirectDraws + kMaxWaterIndirectDraws;

    Renderer() = default;
    ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    bool init(Platform& platform, const MemoryBudget& memory_budget);
    void shutdown();

    [[nodiscard]] bool initialized() const { return initialized_; }
    [[nodiscard]] const GpuCaps& gpu_caps() const { return gpu_caps_; }
    [[nodiscard]] SnapshotRing& snapshot_ring() { return snapshot_ring_; }
    [[nodiscard]] GpuMeshPool& mesh_pool() { return mesh_pool_; }
    [[nodiscard]] GpuDeferredFreeQueue& deferred_free_queue() { return deferred_free_; }
    [[nodiscard]] MeshUploadQueue& mesh_upload_queue() { return *mesh_upload_queue_; }
    [[nodiscard]] float aspect_ratio() const;
    [[nodiscard]] std::uint64_t frame_index() const { return frame_index_; }
    [[nodiscard]] VulkanContext& vulkan_context() { return context_; }
    [[nodiscard]] const VulkanContext& vulkan_context() const { return context_; }
    [[nodiscard]] VkRenderPass render_pass() const { return render_pass_; }

    void set_ui_host(UiHost* ui_host) { ui_host_ = ui_host; }

    // Register a pluggable pass recorded at a named insertion point inside the
    // main render pass. If the renderer is already initialized, the extension
    // is initialized immediately. The renderer does not take ownership.
    void register_extension(std::string_view insertion_point, IPassExtension* extension);
    /// Call after EngineConfig::finalize_gpu — mesh pool is created in init() with budget 0.
    void apply_memory_budget(const MemoryBudget& memory_budget);
    [[nodiscard]] bool device_lost() const { return device_lost_; }
    void note_device_lost();
    void recover_if_device_lost();
    /// GPU-finished upload marks from the last submit on this snapshot slot (moved out).
    [[nodiscard]] std::vector<MeshUploadFlushMark> consume_upload_marks_for_snapshot(
        std::uint32_t snapshot_slot);
    void render_frame(std::uint32_t snapshot_slot,
                      std::function<void(WorldRenderSnapshot&)> fill_snapshot = {});

private:
    struct FrameUniformStub {
        glm::mat4 view{1.f};
        glm::mat4 proj{1.f};
        glm::vec3 render_origin{0.f};
    };

    bool load_block_textures();
    bool create_render_pass();
    bool create_depth_resources();
    void destroy_depth_resources();
    bool create_framebuffers();
    bool create_command_pool_and_buffers();
    bool create_sync_objects();
    void destroy_frame_resources();
    void destroy_swapchain_resources();
    void recreate_swapchain();
    void process_deferred_frees();
    void recreate_render_passes();

    bool begin_frame(std::uint32_t& image_index, std::uint32_t snapshot_slot);
    void end_frame(std::uint32_t image_index, std::uint32_t snapshot_slot);
    void record_frame(std::uint32_t image_index,
                      std::uint32_t snapshot_slot,
                      const std::function<void(WorldRenderSnapshot&)>& fill_snapshot);

    VulkanContext context_{};
    SnapshotRing snapshot_ring_{kFramesInFlight};
    GpuDeferredFreeQueue deferred_free_{SnapshotRing::snapshot_count(kFramesInFlight)};
    GpuMeshPool mesh_pool_{};
    std::unique_ptr<MeshUploadQueue> mesh_upload_queue_;
    std::unique_ptr<PerFrameGpuWriteRing> per_frame_writes_;
    TerrainPass terrain_pass_{};
    TerrainImpostorPass impostor_pass_{};
    BlockTextureArray block_textures_{};
    SkyPass sky_pass_{};
    WaterPass water_pass_{};
    ShaderManager shader_manager_{};

    VkRenderPass render_pass_ = VK_NULL_HANDLE;
    VkImage depth_image_ = VK_NULL_HANDLE;
    VkDeviceMemory depth_memory_ = VK_NULL_HANDLE;
    VkImageView depth_image_view_ = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> framebuffers_;
    VkCommandPool command_pool_ = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> command_buffers_;

    std::vector<VkSemaphore> image_available_semaphores_;
    std::vector<VkSemaphore> render_finished_semaphores_;

    struct RegisteredExtension {
        std::string insertion_point;
        IPassExtension* extension = nullptr;
        bool initialized = false;
    };
    void init_extension(RegisteredExtension& entry);
    [[nodiscard]] PassExtensionInitContext make_extension_init_context() const;
    std::vector<RegisteredExtension> extensions_;

    GpuCaps gpu_caps_{};
    FrameUniformStub frame_uniforms_{};
    MemoryBudget memory_budget_{};
    std::uint32_t frame_index_ = 0;
    UiHost* ui_host_ = nullptr;
    bool initialized_ = false;
    bool device_lost_ = false;
    std::vector<std::vector<MeshUploadFlushMark>> snapshot_upload_marks_{
        SnapshotRing::snapshot_count(kFramesInFlight)};
};

} // namespace engine
