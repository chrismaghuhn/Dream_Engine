#include "engine/render/Renderer.hpp"

#include "engine/platform/Platform.hpp"
#include "engine/render/VkCheck.hpp"
#include "engine/ui/UiHost.hpp"

#include <spdlog/spdlog.h>

#include <array>
#include <algorithm>

#include <glm/glm.hpp>

namespace engine {

namespace {

constexpr std::array<float, 4> kClearColor = {0.08f, 0.10f, 0.14f, 1.0f};

#ifndef ENGINE_SHADER_DIR
#define ENGINE_SHADER_DIR "."
#endif

#ifndef ENGINE_SHADER_SOURCE_DIR
#define ENGINE_SHADER_SOURCE_DIR "."
#endif

[[nodiscard]] std::uint32_t find_memory_type(VkPhysicalDevice physical_device,
                                             std::uint32_t type_filter,
                                             VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memory_properties{};
    vkGetPhysicalDeviceMemoryProperties(physical_device, &memory_properties);

    for (std::uint32_t i = 0; i < memory_properties.memoryTypeCount; ++i) {
        if ((type_filter & (1u << i)) != 0 &&
            (memory_properties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    return UINT32_MAX;
}

} // namespace

Renderer::~Renderer() {
    shutdown();
}

bool Renderer::init(Platform& platform, const MemoryBudget& memory_budget) {
    if (initialized_) {
        return true;
    }

    memory_budget_ = memory_budget;

#if defined(NDEBUG)
    constexpr bool enable_validation = false;
#else
    constexpr bool enable_validation = true;
#endif

    if (!context_.init(platform, enable_validation)) {
        SPDLOG_ERROR("Failed to initialize Vulkan context");
        return false;
    }

    snapshot_ring_.init(context_.device());

    if (!create_render_pass()) {
        shutdown();
        return false;
    }

    if (!create_depth_resources()) {
        shutdown();
        return false;
    }

    if (!create_framebuffers()) {
        shutdown();
        return false;
    }

    if (!create_command_pool_and_buffers()) {
        shutdown();
        return false;
    }

    if (!create_sync_objects()) {
        shutdown();
        return false;
    }

    gpu_caps_ = context_.query_gpu_caps();

    deferred_free_.set_free_callback([this](const std::uint32_t slot_id) {
        mesh_pool_.release_immediate(slot_id);
    });
    deferred_free_.set_fence_checker([this](const std::uint32_t snapshot_slot) {
        if (snapshot_slot >= snapshot_ring_.slot_count()) {
            return false;
        }
        return vkGetFenceStatus(context_.device(), snapshot_ring_.fence(snapshot_slot)) ==
               VK_SUCCESS;
    });

    mesh_pool_.init(context_.device(),
                    context_.physical_device(),
                    memory_budget_.gpu_mesh_vram,
                    &deferred_free_);

    const std::size_t staging_cpu_ram = memory_budget_.chunk_mesh_cpu_ram > 0
        ? memory_budget_.chunk_mesh_cpu_ram
        : (128ULL * 1024 * 1024);
    mesh_upload_queue_ = std::make_unique<MeshUploadQueue>(kFramesInFlight, staging_cpu_ram);
    mesh_upload_queue_->init(context_.device(), context_.physical_device(), gpu_caps_);

    per_frame_writes_ = std::make_unique<PerFrameGpuWriteRing>(kFramesInFlight,
                                                               gpu_caps_,
                                                               kIndirectBufferDraws,
                                                               sizeof(TerrainPass::FrameUniformGpu));
    per_frame_writes_->init(context_.device(), context_.physical_device());
    const std::filesystem::path shader_dir = std::filesystem::path(ENGINE_SHADER_DIR);
    const std::filesystem::path shader_source_dir = std::filesystem::path(ENGINE_SHADER_SOURCE_DIR);
    shader_manager_.init(shader_source_dir, shader_dir);
    shader_manager_.register_shader(
        "terrain",
        ShaderManager::ShaderSource{
            .vert = shader_source_dir / "terrain.vert",
            .frag = shader_source_dir / "terrain.frag",
            .vert_spv = shader_dir / "terrain.vert.spv",
            .frag_spv = shader_dir / "terrain.frag.spv",
        });
    shader_manager_.register_shader(
        "sky",
        ShaderManager::ShaderSource{
            .vert = shader_source_dir / "sky.vert",
            .frag = shader_source_dir / "sky.frag",
            .vert_spv = shader_dir / "sky.vert.spv",
            .frag_spv = shader_dir / "sky.frag.spv",
        });
    shader_manager_.register_shader(
        "water",
        ShaderManager::ShaderSource{
            .vert = shader_source_dir / "water.vert",
            .frag = shader_source_dir / "water.frag",
            .vert_spv = shader_dir / "water.vert.spv",
            .frag_spv = shader_dir / "water.frag.spv",
        });
    shader_manager_.register_shader(
        "impostor",
        ShaderManager::ShaderSource{
            .vert = shader_source_dir / "impostor.vert",
            .frag = shader_source_dir / "impostor.frag",
            .vert_spv = shader_dir / "impostor.vert.spv",
            .frag_spv = shader_dir / "impostor.frag.spv",
        });

    if (!terrain_pass_.init(context_, render_pass_, shader_dir, *per_frame_writes_)) {
        SPDLOG_ERROR("Failed to initialize terrain pass");
        shutdown();
        return false;
    }

    if (!load_block_textures()) {
        SPDLOG_ERROR("Failed to load block textures");
        shutdown();
        return false;
    }
    terrain_pass_.bind_block_texture(block_textures_.image_view(), block_textures_.sampler());

    if (!impostor_pass_.init(context_, render_pass_, shader_dir)) {
        SPDLOG_ERROR("Failed to initialize terrain impostor pass");
        shutdown();
        return false;
    }

    if (!sky_pass_.init(context_, render_pass_, shader_dir)) {
        SPDLOG_ERROR("Failed to initialize sky pass");
        shutdown();
        return false;
    }

    if (!water_pass_.init(context_,
                          render_pass_,
                          shader_dir,
                          *per_frame_writes_,
                          terrain_pass_.descriptor_layout())) {
        SPDLOG_ERROR("Failed to initialize water pass");
        shutdown();
        return false;
    }

    initialized_ = true;

    // Initialize any extensions registered before init() ran.
    for (RegisteredExtension& entry : extensions_) {
        init_extension(entry);
    }

    SPDLOG_INFO("Renderer initialized: {} MiB VRAM, queue family {}",
                gpu_caps_.vram_bytes / (1024 * 1024),
                gpu_caps_.graphics_queue_family);
    return true;
}

PassExtensionInitContext Renderer::make_extension_init_context() const {
    return PassExtensionInitContext{
        .context = const_cast<VulkanContext*>(&context_),
        .render_pass = render_pass_,
        .extent = context_.swapchain_extent(),
        // Use snapshot_count so per-extension per-frame resources (e.g. vertex
        // buffers in DebugDrawPass) are sized to the actual ring depth, not just
        // kFramesInFlight. Extensions index their per-frame resources by
        // frame_slot which now equals snapshot_slot (0..slot_count-1).
        .frames_in_flight = snapshot_ring_.slot_count(),
    };
}

void Renderer::init_extension(RegisteredExtension& entry) {
    if (entry.extension == nullptr || entry.initialized) {
        return;
    }
    entry.extension->on_renderer_init(make_extension_init_context());
    entry.initialized = true;
}

void Renderer::register_extension(std::string_view insertion_point, IPassExtension* extension) {
    if (extension == nullptr) {
        return;
    }
    RegisteredExtension entry{std::string(insertion_point), extension, false};
    if (initialized_ && render_pass_ != VK_NULL_HANDLE) {
        init_extension(entry);
    }
    extensions_.push_back(std::move(entry));
}

void Renderer::apply_memory_budget(const MemoryBudget& memory_budget) {
    memory_budget_ = memory_budget;
    if (!initialized_) {
        return;
    }

    mesh_pool_.set_bytes_budget(memory_budget_.gpu_mesh_vram);
    SPDLOG_INFO(
        "Mesh pool VRAM budget applied: {} MiB (used {} KiB)",
        memory_budget_.gpu_mesh_vram / (1024 * 1024),
        mesh_pool_.bytes_used() / 1024);
}

void Renderer::shutdown() {
    if (!initialized_ && context_.device() == VK_NULL_HANDLE) {
        return;
    }

    if (context_.device() != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(context_.device());
    }

    for (RegisteredExtension& entry : extensions_) {
        if (entry.initialized && entry.extension != nullptr) {
            entry.extension->on_renderer_shutdown(context_);
            entry.initialized = false;
        }
    }

    sky_pass_.shutdown();
    water_pass_.shutdown();
    impostor_pass_.shutdown();
    block_textures_.shutdown();
    terrain_pass_.shutdown();
    shader_manager_.shutdown();
    per_frame_writes_.reset();
    mesh_upload_queue_.reset();
    mesh_pool_.shutdown();

    destroy_frame_resources();
    destroy_depth_resources();
    destroy_swapchain_resources();
    snapshot_ring_.shutdown();
    context_.shutdown();

    gpu_caps_ = {};
    memory_budget_ = {};
    frame_index_ = 0;
    initialized_ = false;
}

float Renderer::aspect_ratio() const {
    if (!context_.has_valid_extent()) {
        return 16.f / 9.f;
    }

    const VkExtent2D extent = context_.swapchain_extent();
    if (extent.height == 0) {
        return 16.f / 9.f;
    }

    return static_cast<float>(extent.width) / static_cast<float>(extent.height);
}

void Renderer::process_deferred_frees() {
    deferred_free_.set_fence_checker([this](const std::uint32_t snapshot_slot) {
        if (snapshot_slot >= snapshot_ring_.slot_count()) {
            return false;
        }
        const VkResult status =
            vkGetFenceStatus(context_.device(), snapshot_ring_.fence(snapshot_slot));
        return status == VK_SUCCESS;
    });
    deferred_free_.process_completed();
}

void Renderer::note_device_lost() {
    device_lost_ = true;
}

std::vector<MeshUploadFlushMark> Renderer::consume_upload_marks_for_snapshot(
    const std::uint32_t snapshot_slot) {
    if (snapshot_slot >= snapshot_upload_marks_.size()) {
        return {};
    }
    return std::move(snapshot_upload_marks_[snapshot_slot]);
}

void Renderer::recover_if_device_lost() {
    if (!device_lost_ || !initialized_) {
        return;
    }

    SPDLOG_WARN("Recovering from Vulkan device lost");
    vkDeviceWaitIdle(context_.device());
    deferred_free_.flush_pending_immediate();
    snapshot_ring_.reset_fences_after_idle();
    for (std::vector<MeshUploadFlushMark>& marks : snapshot_upload_marks_) {
        marks.clear();
    }
    recreate_swapchain();
    device_lost_ = false;
}

void Renderer::render_frame(const std::uint32_t snapshot_slot,
                            std::function<void(WorldRenderSnapshot&)> fill_snapshot) {
    if (!initialized_) {
        return;
    }

    if (!context_.has_valid_extent()) {
        return;
    }

    if (device_lost_) {
        return;
    }

    process_deferred_frees();
    shader_manager_.poll_hot_reload();
    if (shader_manager_.shaders_dirty()) {
        recreate_render_passes();
        shader_manager_.clear_dirty();
    }

    const WorldRenderSnapshot& snapshot = snapshot_ring_.snapshot(snapshot_slot);
    frame_uniforms_.view = snapshot.view;
    frame_uniforms_.proj = snapshot.proj;
    frame_uniforms_.render_origin = snapshot.render_origin;

    std::uint32_t image_index = 0;
    if (!begin_frame(image_index, snapshot_slot)) {
        return;
    }

    record_frame(image_index, snapshot_slot, fill_snapshot);
    end_frame(image_index, snapshot_slot);
    ++frame_index_;
}

bool Renderer::begin_frame(std::uint32_t& image_index, const std::uint32_t snapshot_slot) {
    // Index by snapshot_slot: pick_write_slot() already waited for the fence,
    // so this slot's command buffer and semaphores are guaranteed idle.
    const VkResult acquire_result = vkAcquireNextImageKHR(context_.device(),
                                                            context_.swapchain(),
                                                            UINT64_MAX,
                                                            image_available_semaphores_[snapshot_slot],
                                                            VK_NULL_HANDLE,
                                                            &image_index);

    if (acquire_result == VK_ERROR_OUT_OF_DATE_KHR) {
        recreate_swapchain();
        return false;
    }
    if (acquire_result == VK_ERROR_DEVICE_LOST) {
        SPDLOG_CRITICAL("Vulkan device lost during acquire");
        device_lost_ = true;
        return false;
    }
    if (acquire_result != VK_SUCCESS && acquire_result != VK_SUBOPTIMAL_KHR) {
        VK_CHECK(acquire_result);
    }

    return true;
}

void Renderer::record_frame(const std::uint32_t image_index,
                            const std::uint32_t snapshot_slot,
                            const std::function<void(WorldRenderSnapshot&)>& fill_snapshot) {
    VkCommandBuffer command_buffer = command_buffers_[snapshot_slot];
    const WorldRenderSnapshot& snapshot = snapshot_ring_.snapshot(snapshot_slot);

    VK_CHECK(vkResetCommandBuffer(command_buffer, 0));

    const VkCommandBufferBeginInfo begin_info{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    VK_CHECK(vkBeginCommandBuffer(command_buffer, &begin_info));

    mesh_upload_queue_->flush(command_buffer, frame_index_, mesh_pool_);

    if (fill_snapshot) {
        fill_snapshot(snapshot_ring_.snapshot(snapshot_slot));
    }

    const TerrainPass::FrameUniformGpu uniforms{
        .view = frame_uniforms_.view,
        .proj = frame_uniforms_.proj,
        .render_origin = glm::vec4(frame_uniforms_.render_origin, 0.f),
    };
    const TerrainImpostorPass::FrameUniformGpu impostor_uniforms{
        .view = frame_uniforms_.view,
        .proj = frame_uniforms_.proj,
        .render_origin = glm::vec4(frame_uniforms_.render_origin, 0.f),
        .ambient_fog = snapshot.ambient_fog,
    };
    const std::size_t opaque_draw_count = snapshot.opaque_sections.size();

    terrain_pass_.write_frame_uniforms(frame_index_, uniforms);
    impostor_pass_.write_frame_uniforms(frame_index_, impostor_uniforms);
    terrain_pass_.write_indirect_commands(frame_index_, snapshot, mesh_pool_);
    water_pass_.write_indirect_commands(frame_index_, snapshot, mesh_pool_, opaque_draw_count);

    const VkClearValue clear_values[] = {
        {.color = {{kClearColor[0], kClearColor[1], kClearColor[2], kClearColor[3]}}},
        {.depthStencil = {1.f, 0}},
    };
    const VkRenderPassBeginInfo render_pass_info{
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = render_pass_,
        .framebuffer = framebuffers_[image_index],
        .renderArea = {.extent = context_.swapchain_extent()},
        .clearValueCount = 2,
        .pClearValues = clear_values,
    };

    vkCmdBeginRenderPass(command_buffer, &render_pass_info, VK_SUBPASS_CONTENTS_INLINE);
    terrain_pass_.record(
        command_buffer, frame_index_, snapshot, mesh_pool_, context_.swapchain_extent());
    impostor_pass_.record(
        command_buffer, frame_index_, snapshot, context_.swapchain_extent());
    sky_pass_.record(command_buffer, context_.swapchain_extent());
    water_pass_.record(command_buffer,
                       frame_index_,
                       snapshot,
                       mesh_pool_,
                       terrain_pass_.frame_descriptor_set(frame_index_),
                       context_.swapchain_extent(),
                       opaque_draw_count);

    // Pass extensions at "before_imgui": world geometry is complete, so debug
    // lines / character meshes depth-test correctly against opaque + water.
    if (!extensions_.empty()) {
        const PassExtensionRecordContext ext_ctx{
            .command_buffer = command_buffer,
            .frame_index = static_cast<std::uint32_t>(frame_index_),
            .frame_slot = snapshot_slot,
            .extent = context_.swapchain_extent(),
            .view = frame_uniforms_.view,
            .proj = frame_uniforms_.proj,
        };
        for (RegisteredExtension& entry : extensions_) {
            if (entry.initialized && entry.insertion_point == pass_insertion::kBeforeImgui) {
                entry.extension->record(ext_ctx);
            }
        }
    }

    if (ui_host_ != nullptr) {
        ui_host_->render(command_buffer);
    }
    vkCmdEndRenderPass(command_buffer);
    VK_CHECK(vkEndCommandBuffer(command_buffer));
}

void Renderer::end_frame(const std::uint32_t image_index, const std::uint32_t snapshot_slot) {
    VkCommandBuffer command_buffer = command_buffers_[snapshot_slot];

    const VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    const std::array<VkSemaphore, 1> wait_semaphores = {image_available_semaphores_[snapshot_slot]};
    const std::array<VkSemaphore, 1> signal_semaphores = {render_finished_semaphores_[image_index]};

    const VkSubmitInfo submit_info{
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .waitSemaphoreCount = static_cast<uint32_t>(wait_semaphores.size()),
        .pWaitSemaphores = wait_semaphores.data(),
        .pWaitDstStageMask = &wait_stage,
        .commandBufferCount = 1,
        .pCommandBuffers = &command_buffer,
        .signalSemaphoreCount = static_cast<uint32_t>(signal_semaphores.size()),
        .pSignalSemaphores = signal_semaphores.data(),
    };

    const VkResult submit_result =
        vkQueueSubmit(context_.graphics_queue(), 1, &submit_info, snapshot_ring_.fence(snapshot_slot));
    if (submit_result == VK_ERROR_DEVICE_LOST) {
        SPDLOG_CRITICAL("Vulkan device lost — will recreate swapchain next frame");
        device_lost_ = true;
        if (auto logger = spdlog::default_logger()) {
            logger->flush();
        }
        return;
    }
    VK_CHECK(submit_result);
    snapshot_ring_.mark_submitted(snapshot_slot);
    if (snapshot_slot < snapshot_upload_marks_.size()) {
        snapshot_upload_marks_[snapshot_slot] = mesh_upload_queue_->last_flushed_marks();
    }

    VkSwapchainKHR swapchain = context_.swapchain();
    const VkPresentInfoKHR present_info{
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = static_cast<uint32_t>(signal_semaphores.size()),
        .pWaitSemaphores = signal_semaphores.data(),
        .swapchainCount = 1,
        .pSwapchains = &swapchain,
        .pImageIndices = &image_index,
    };

    const VkResult present_result = vkQueuePresentKHR(context_.present_queue(), &present_info);
    if (present_result == VK_ERROR_OUT_OF_DATE_KHR) {
        recreate_swapchain();
        return;
    }
    if (present_result == VK_ERROR_DEVICE_LOST) {
        SPDLOG_CRITICAL("Vulkan device lost during present");
        device_lost_ = true;
        return;
    }
    if (present_result != VK_SUCCESS && present_result != VK_SUBOPTIMAL_KHR) {
        VK_CHECK(present_result);
    }
}

void Renderer::recreate_swapchain() {
    context_.recreate_swapchain();
    destroy_swapchain_resources();
    destroy_depth_resources();

    if (!context_.has_valid_extent()) {
        return;
    }

    if (!create_render_pass()) {
        SPDLOG_ERROR("Failed to recreate render pass");
        return;
    }

    if (!create_depth_resources()) {
        SPDLOG_ERROR("Failed to recreate depth resources");
        return;
    }

    if (!create_framebuffers()) {
        SPDLOG_ERROR("Failed to recreate swapchain framebuffers");
        return;
    }

    if (!create_command_pool_and_buffers()) {
        SPDLOG_ERROR("Failed to recreate command buffers");
    }

    recreate_render_passes();

    if (ui_host_ != nullptr) {
        ui_host_->on_swapchain_recreated(*this);
    }
}

void Renderer::recreate_render_passes() {
    if (per_frame_writes_ == nullptr || render_pass_ == VK_NULL_HANDLE) {
        return;
    }

    const std::filesystem::path shader_dir = std::filesystem::path(ENGINE_SHADER_DIR);

    sky_pass_.shutdown();
    water_pass_.shutdown();
    impostor_pass_.shutdown();
    terrain_pass_.shutdown();

    if (!terrain_pass_.init(context_, render_pass_, shader_dir, *per_frame_writes_)) {
        SPDLOG_ERROR("Failed to recreate terrain pass");
        return;
    }
    // Re-point the freshly allocated descriptor sets at the existing texture array.
    terrain_pass_.bind_block_texture(block_textures_.image_view(), block_textures_.sampler());
    if (!impostor_pass_.init(context_, render_pass_, shader_dir)) {
        SPDLOG_ERROR("Failed to recreate terrain impostor pass");
        return;
    }
    if (!sky_pass_.init(context_, render_pass_, shader_dir)) {
        SPDLOG_ERROR("Failed to recreate sky pass");
        return;
    }
    if (!water_pass_.init(context_,
                          render_pass_,
                          shader_dir,
                          *per_frame_writes_,
                          terrain_pass_.descriptor_layout())) {
        SPDLOG_ERROR("Failed to recreate water pass");
    }

    for (RegisteredExtension& entry : extensions_) {
        if (entry.initialized && entry.extension != nullptr) {
            entry.extension->on_swapchain_recreate(make_extension_init_context());
        }
    }
}

bool Renderer::load_block_textures() {
    // Layer order must match layer_for() in terrain.frag:
    //   0 = stone, 1 = dirt, 2 = grass top, 3 = grass side.
    const std::filesystem::path tex_dir = std::filesystem::path(ENGINE_ASSET_DIR) / "textures";
    const std::vector<std::filesystem::path> layer_files = {
        tex_dir / "stone.png",
        tex_dir / "dirt.png",
        tex_dir / "grass_top.png",
        tex_dir / "grass_side.png",
    };
    return block_textures_.init(context_, layer_files);
}

bool Renderer::create_render_pass() {
    const VkAttachmentDescription attachments[] = {
        {
            .format = context_.swapchain_format(),
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        },
        {
            .format = VK_FORMAT_D32_SFLOAT,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        },
    };

    const VkAttachmentReference color_attachment_ref{
        .attachment = 0,
        .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    };
    const VkAttachmentReference depth_attachment_ref{
        .attachment = 1,
        .layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
    };

    const VkSubpassDescription subpass{
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount = 1,
        .pColorAttachments = &color_attachment_ref,
        .pDepthStencilAttachment = &depth_attachment_ref,
    };

    const VkSubpassDependency dependency{
        .srcSubpass = VK_SUBPASS_EXTERNAL,
        .dstSubpass = 0,
        .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
        .srcAccessMask = 0,
        .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
    };

    const VkRenderPassCreateInfo render_pass_info{
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = 2,
        .pAttachments = attachments,
        .subpassCount = 1,
        .pSubpasses = &subpass,
        .dependencyCount = 1,
        .pDependencies = &dependency,
    };

    VK_CHECK(vkCreateRenderPass(context_.device(), &render_pass_info, nullptr, &render_pass_));
    return true;
}

bool Renderer::create_depth_resources() {
    if (!context_.has_valid_extent()) {
        return true;
    }

    const VkExtent2D extent = context_.swapchain_extent();
    const VkImageCreateInfo image_info{
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = VK_FORMAT_D32_SFLOAT,
        .extent = {extent.width, extent.height, 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    VK_CHECK(vkCreateImage(context_.device(), &image_info, nullptr, &depth_image_));

    VkMemoryRequirements requirements{};
    vkGetImageMemoryRequirements(context_.device(), depth_image_, &requirements);
    const std::uint32_t memory_type = find_memory_type(
        context_.physical_device(), requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (memory_type == UINT32_MAX) {
        return false;
    }

    const VkMemoryAllocateInfo alloc_info{
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = requirements.size,
        .memoryTypeIndex = memory_type,
    };
    VK_CHECK(vkAllocateMemory(context_.device(), &alloc_info, nullptr, &depth_memory_));
    VK_CHECK(vkBindImageMemory(context_.device(), depth_image_, depth_memory_, 0));

    const VkImageViewCreateInfo view_info{
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = depth_image_,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = VK_FORMAT_D32_SFLOAT,
        .subresourceRange =
            {
                .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
                .levelCount = 1,
                .layerCount = 1,
            },
    };
    VK_CHECK(vkCreateImageView(context_.device(), &view_info, nullptr, &depth_image_view_));
    return true;
}

void Renderer::destroy_depth_resources() {
    if (context_.device() == VK_NULL_HANDLE) {
        return;
    }

    if (depth_image_view_ != VK_NULL_HANDLE) {
        vkDestroyImageView(context_.device(), depth_image_view_, nullptr);
        depth_image_view_ = VK_NULL_HANDLE;
    }
    if (depth_image_ != VK_NULL_HANDLE) {
        vkDestroyImage(context_.device(), depth_image_, nullptr);
        depth_image_ = VK_NULL_HANDLE;
    }
    if (depth_memory_ != VK_NULL_HANDLE) {
        vkFreeMemory(context_.device(), depth_memory_, nullptr);
        depth_memory_ = VK_NULL_HANDLE;
    }
}

bool Renderer::create_framebuffers() {
    framebuffers_.resize(context_.swapchain_image_views().size());
    for (size_t i = 0; i < context_.swapchain_image_views().size(); ++i) {
        const VkImageView attachments[] = {context_.swapchain_image_views()[i], depth_image_view_};
        const VkFramebufferCreateInfo framebuffer_info{
            .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
            .renderPass = render_pass_,
            .attachmentCount = 2,
            .pAttachments = attachments,
            .width = context_.swapchain_extent().width,
            .height = context_.swapchain_extent().height,
            .layers = 1,
        };
        VK_CHECK(vkCreateFramebuffer(context_.device(), &framebuffer_info, nullptr, &framebuffers_[i]));
    }
    return true;
}

bool Renderer::create_command_pool_and_buffers() {
    if (command_pool_ != VK_NULL_HANDLE) {
        vkFreeCommandBuffers(context_.device(),
                             command_pool_,
                             static_cast<uint32_t>(command_buffers_.size()),
                             command_buffers_.data());
        command_buffers_.clear();
        vkDestroyCommandPool(context_.device(), command_pool_, nullptr);
        command_pool_ = VK_NULL_HANDLE;
    }

    const VkCommandPoolCreateInfo pool_info{
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = context_.graphics_queue_family(),
    };
    VK_CHECK(vkCreateCommandPool(context_.device(), &pool_info, nullptr, &command_pool_));

    // One command buffer per snapshot slot so that snapshot_slot can be used
    // directly as the index without a modulo, eliminating the frame_slot vs.
    // snapshot_slot mismatch that caused the semaphore-reuse validation errors.
    const std::uint32_t slot_count = snapshot_ring_.slot_count();
    command_buffers_.resize(slot_count);
    const VkCommandBufferAllocateInfo alloc_info{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = command_pool_,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = slot_count,
    };
    VK_CHECK(vkAllocateCommandBuffers(context_.device(), &alloc_info, command_buffers_.data()));
    return true;
}

bool Renderer::create_sync_objects() {
    // image_available: one per snapshot slot — indexed by snapshot_slot in begin_frame.
    // pick_write_slot() waits for the slot's fence before reuse, guaranteeing the
    // previous submit that waited on this semaphore has completed.
    const std::uint32_t slot_count = snapshot_ring_.slot_count();
    image_available_semaphores_.resize(slot_count);

    // render_finished: one per swapchain image — indexed by image_index in end_frame.
    // This prevents re-signalling the semaphore while the presentation engine may
    // still be consuming it from a previous present of the same swapchain image.
    const std::uint32_t image_count =
        static_cast<std::uint32_t>(context_.swapchain_images().size());
    render_finished_semaphores_.resize(image_count);

    const VkSemaphoreCreateInfo semaphore_info{.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};

    for (std::uint32_t i = 0; i < slot_count; ++i) {
        VK_CHECK(vkCreateSemaphore(context_.device(), &semaphore_info, nullptr,
                                   &image_available_semaphores_[i]));
    }
    for (std::uint32_t i = 0; i < image_count; ++i) {
        VK_CHECK(vkCreateSemaphore(context_.device(), &semaphore_info, nullptr,
                                   &render_finished_semaphores_[i]));
    }

    return true;
}

void Renderer::destroy_frame_resources() {
    if (context_.device() == VK_NULL_HANDLE) {
        return;
    }

    for (VkSemaphore semaphore : render_finished_semaphores_) {
        vkDestroySemaphore(context_.device(), semaphore, nullptr);
    }
    render_finished_semaphores_.clear();

    for (VkSemaphore semaphore : image_available_semaphores_) {
        vkDestroySemaphore(context_.device(), semaphore, nullptr);
    }
    image_available_semaphores_.clear();

    if (command_pool_ != VK_NULL_HANDLE) {
        vkDestroyCommandPool(context_.device(), command_pool_, nullptr);
        command_pool_ = VK_NULL_HANDLE;
    }
    command_buffers_.clear();
}

void Renderer::destroy_swapchain_resources() {
    if (context_.device() == VK_NULL_HANDLE) {
        return;
    }

    for (VkFramebuffer framebuffer : framebuffers_) {
        vkDestroyFramebuffer(context_.device(), framebuffer, nullptr);
    }
    framebuffers_.clear();

    if (render_pass_ != VK_NULL_HANDLE) {
        vkDestroyRenderPass(context_.device(), render_pass_, nullptr);
        render_pass_ = VK_NULL_HANDLE;
    }
}

} // namespace engine
