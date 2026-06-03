#include "engine/render/Renderer.hpp"

#include "engine/platform/Platform.hpp"
#include "engine/render/VkCheck.hpp"

#include <spdlog/spdlog.h>

#include <array>

#include <glm/glm.hpp>

namespace engine {

namespace {

constexpr std::array<float, 4> kClearColor = {0.08f, 0.10f, 0.14f, 1.0f};

} // namespace

Renderer::~Renderer() {
    shutdown();
}

bool Renderer::init(Platform& platform) {
    if (initialized_) {
        return true;
    }

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
    initialized_ = true;

    SPDLOG_INFO("Renderer initialized: {} MiB VRAM, queue family {}",
                gpu_caps_.vram_bytes / (1024 * 1024),
                gpu_caps_.graphics_queue_family);
    return true;
}

void Renderer::shutdown() {
    if (!initialized_ && context_.device() == VK_NULL_HANDLE) {
        return;
    }

    if (context_.device() != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(context_.device());
    }

    destroy_frame_resources();
    destroy_swapchain_resources();
    snapshot_ring_.shutdown();
    context_.shutdown();

    gpu_caps_ = {};
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

void Renderer::render_frame(std::uint32_t snapshot_slot) {
    if (!initialized_) {
        return;
    }

    if (!context_.has_valid_extent()) {
        return;
    }

    const WorldRenderSnapshot& snapshot = snapshot_ring_.snapshot(snapshot_slot);
    frame_uniforms_.view = snapshot.view;
    frame_uniforms_.proj = snapshot.proj;
    frame_uniforms_.render_origin = snapshot.render_origin;

    std::uint32_t image_index = 0;
    if (!begin_frame(image_index)) {
        return;
    }

    record_clear_pass(image_index);
    end_frame(image_index, snapshot_slot);
    ++frame_index_;
}

bool Renderer::begin_frame(std::uint32_t& image_index) {
    const std::uint32_t frame_slot = frame_index_ % kFramesInFlight;

    const VkResult acquire_result = vkAcquireNextImageKHR(context_.device(),
                                                            context_.swapchain(),
                                                            UINT64_MAX,
                                                            image_available_semaphores_[frame_slot],
                                                            VK_NULL_HANDLE,
                                                            &image_index);

    if (acquire_result == VK_ERROR_OUT_OF_DATE_KHR) {
        recreate_swapchain();
        return false;
    }
    if (acquire_result != VK_SUCCESS && acquire_result != VK_SUBOPTIMAL_KHR) {
        VK_CHECK(acquire_result);
    }

    return true;
}

void Renderer::record_clear_pass(std::uint32_t image_index) {
    const std::uint32_t frame_slot = frame_index_ % kFramesInFlight;
    VkCommandBuffer command_buffer = command_buffers_[frame_slot];

    VK_CHECK(vkResetCommandBuffer(command_buffer, 0));

    const VkCommandBufferBeginInfo begin_info{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    VK_CHECK(vkBeginCommandBuffer(command_buffer, &begin_info));

    const VkClearValue clear_color{.color = {{kClearColor[0], kClearColor[1], kClearColor[2], kClearColor[3]}}};
    const VkRenderPassBeginInfo render_pass_info{
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = render_pass_,
        .framebuffer = framebuffers_[image_index],
        .renderArea = {.extent = context_.swapchain_extent()},
        .clearValueCount = 1,
        .pClearValues = &clear_color,
    };

    vkCmdBeginRenderPass(command_buffer, &render_pass_info, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdEndRenderPass(command_buffer);
    VK_CHECK(vkEndCommandBuffer(command_buffer));
}

void Renderer::end_frame(std::uint32_t image_index, std::uint32_t snapshot_slot) {
    const std::uint32_t frame_slot = frame_index_ % kFramesInFlight;
    VkCommandBuffer command_buffer = command_buffers_[frame_slot];

    const VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    const std::array<VkSemaphore, 1> wait_semaphores = {image_available_semaphores_[frame_slot]};
    const std::array<VkSemaphore, 1> signal_semaphores = {render_finished_semaphores_[frame_slot]};

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

    VK_CHECK(vkQueueSubmit(context_.graphics_queue(), 1, &submit_info, snapshot_ring_.fence(snapshot_slot)));
    snapshot_ring_.mark_submitted(snapshot_slot);

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
    if (present_result != VK_SUCCESS && present_result != VK_SUBOPTIMAL_KHR) {
        VK_CHECK(present_result);
    }
}

void Renderer::recreate_swapchain() {
    context_.recreate_swapchain();
    destroy_swapchain_resources();

    if (!context_.has_valid_extent()) {
        return;
    }

    if (!create_render_pass()) {
        SPDLOG_ERROR("Failed to recreate render pass");
        return;
    }

    if (!create_framebuffers()) {
        SPDLOG_ERROR("Failed to recreate swapchain framebuffers");
        return;
    }

    if (!create_command_pool_and_buffers()) {
        SPDLOG_ERROR("Failed to recreate command buffers");
    }
}

bool Renderer::create_render_pass() {
    const VkAttachmentDescription color_attachment{
        .format = context_.swapchain_format(),
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
    };

    const VkAttachmentReference color_attachment_ref{
        .attachment = 0,
        .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    };

    const VkSubpassDescription subpass{
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount = 1,
        .pColorAttachments = &color_attachment_ref,
    };

    const VkSubpassDependency dependency{
        .srcSubpass = VK_SUBPASS_EXTERNAL,
        .dstSubpass = 0,
        .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        .srcAccessMask = 0,
        .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
    };

    const VkRenderPassCreateInfo render_pass_info{
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments = &color_attachment,
        .subpassCount = 1,
        .pSubpasses = &subpass,
        .dependencyCount = 1,
        .pDependencies = &dependency,
    };

    VK_CHECK(vkCreateRenderPass(context_.device(), &render_pass_info, nullptr, &render_pass_));
    return true;
}

bool Renderer::create_framebuffers() {
    framebuffers_.resize(context_.swapchain_image_views().size());
    for (size_t i = 0; i < context_.swapchain_image_views().size(); ++i) {
        const VkImageView attachments[] = {context_.swapchain_image_views()[i]};
        const VkFramebufferCreateInfo framebuffer_info{
            .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
            .renderPass = render_pass_,
            .attachmentCount = 1,
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

    command_buffers_.resize(kFramesInFlight);
    const VkCommandBufferAllocateInfo alloc_info{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = command_pool_,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = kFramesInFlight,
    };
    VK_CHECK(vkAllocateCommandBuffers(context_.device(), &alloc_info, command_buffers_.data()));
    return true;
}

bool Renderer::create_sync_objects() {
    image_available_semaphores_.resize(kFramesInFlight);
    render_finished_semaphores_.resize(kFramesInFlight);

    const VkSemaphoreCreateInfo semaphore_info{.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};

    for (std::uint32_t i = 0; i < kFramesInFlight; ++i) {
        VK_CHECK(vkCreateSemaphore(context_.device(), &semaphore_info, nullptr, &image_available_semaphores_[i]));
        VK_CHECK(vkCreateSemaphore(context_.device(), &semaphore_info, nullptr, &render_finished_semaphores_[i]));
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
