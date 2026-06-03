#include "engine/movement/debug_render/ImGuiOverlay.hpp"

#include "engine/render/Renderer.hpp"
#include "engine/render/VkCheck.hpp"
#include "engine/render/VulkanContext.hpp"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>

#include <spdlog/spdlog.h>

#include <array>
#include <cmath>

#include <glm/gtc/constants.hpp>

namespace engine::movement {

namespace {

VkDescriptorPool create_imgui_descriptor_pool(VkDevice device) {
    constexpr std::uint32_t kPoolSize = 256;
    const std::array<VkDescriptorPoolSize, 11> pool_sizes = {{
        {VK_DESCRIPTOR_TYPE_SAMPLER, kPoolSize},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, kPoolSize},
        {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, kPoolSize},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, kPoolSize},
        {VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, kPoolSize},
        {VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, kPoolSize},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, kPoolSize},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, kPoolSize},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, kPoolSize},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, kPoolSize},
        {VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, kPoolSize},
    }};
    const VkDescriptorPoolCreateInfo pool_info{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
        .maxSets = kPoolSize,
        .poolSizeCount = static_cast<std::uint32_t>(pool_sizes.size()),
        .pPoolSizes = pool_sizes.data(),
    };
    VkDescriptorPool pool = VK_NULL_HANDLE;
    if (vkCreateDescriptorPool(device, &pool_info, nullptr, &pool) != VK_SUCCESS) {
        return VK_NULL_HANDLE;
    }
    return pool;
}

ImGui_ImplVulkan_InitInfo make_init_info(Renderer& renderer, VkDescriptorPool pool) {
    const VulkanContext& context = renderer.vulkan_context();
    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(context.physical_device(), &props);

    ImGui_ImplVulkan_InitInfo info{};
    info.ApiVersion = props.apiVersion;
    info.Instance = context.instance();
    info.PhysicalDevice = context.physical_device();
    info.Device = context.device();
    info.QueueFamily = context.graphics_queue_family();
    info.Queue = context.graphics_queue();
    info.DescriptorPool = pool;
    info.MinImageCount = Renderer::kFramesInFlight;
    info.ImageCount = static_cast<std::uint32_t>(context.swapchain_images().size());
    info.RenderPass = renderer.render_pass();
    info.Subpass = 0;
    info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    return info;
}

} // namespace

void ImGuiOverlay::on_renderer_init(const PassExtensionInitContext& ctx) {
    context_ = ctx.context;
    if (window_ == nullptr || renderer_ == nullptr) {
        SPDLOG_WARN("ImGuiOverlay: window/renderer not set; overlay disabled");
        return;
    }

    descriptor_pool_ = create_imgui_descriptor_pool(context_->device());
    if (descriptor_pool_ == VK_NULL_HANDLE) {
        SPDLOG_ERROR("ImGuiOverlay: descriptor pool creation failed");
        return;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForVulkan(window_, true);

    ImGui_ImplVulkan_InitInfo init_info = make_init_info(*renderer_, descriptor_pool_);
    if (!ImGui_ImplVulkan_Init(&init_info)) {
        SPDLOG_ERROR("ImGuiOverlay: ImGui_ImplVulkan_Init failed");
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        return;
    }
    if (!ImGui_ImplVulkan_CreateFontsTexture()) {
        SPDLOG_ERROR("ImGuiOverlay: CreateFontsTexture failed");
    }

    initialized_ = true;
    SPDLOG_INFO("ImGuiOverlay initialized");
}

void ImGuiOverlay::on_renderer_shutdown(VulkanContext& context) {
    if (!initialized_) {
        return;
    }
    vkDeviceWaitIdle(context.device());
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    if (descriptor_pool_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(context.device(), descriptor_pool_, nullptr);
        descriptor_pool_ = VK_NULL_HANDLE;
    }
    initialized_ = false;
    frame_started_ = false;
}

void ImGuiOverlay::on_swapchain_recreate(const PassExtensionInitContext& ctx) {
    if (!initialized_ || renderer_ == nullptr) {
        return;
    }
    vkDeviceWaitIdle(ctx.context->device());
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplVulkan_InitInfo init_info = make_init_info(*renderer_, descriptor_pool_);
    if (!ImGui_ImplVulkan_Init(&init_info)) {
        SPDLOG_ERROR("ImGuiOverlay: re-init after swapchain recreate failed");
        initialized_ = false;
        return;
    }
    ImGui_ImplVulkan_CreateFontsTexture();
}

void ImGuiOverlay::new_frame(const MovementOverlayState& state) {
    if (!initialized_) {
        return;
    }

    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    frame_started_ = true;

    ImGui::SetNextWindowPos(ImVec2(10.f, 10.f), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.4f);
    if (ImGui::Begin("Movement",
                     nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                         ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                         ImGuiWindowFlags_NoNav)) {
        ImGui::Text("Entity: %s", state.persistent_id);
        ImGui::Text("FPS: %.1f", state.fps);
        ImGui::Text("Grounded: %s", state.grounded ? "yes" : "no");
        ImGui::Text("Wall contact: %s", state.wall_contact ? "yes" : "no");
        const float speed = std::sqrt(state.velocity.x * state.velocity.x +
                                      state.velocity.z * state.velocity.z);
        ImGui::Text("Velocity: %.2f %.2f %.2f", state.velocity.x, state.velocity.y,
                    state.velocity.z);
        ImGui::Text("Horizontal speed: %.2f", speed);
        ImGui::Text("Position: %.2f %.2f %.2f", state.position.x, state.position.y,
                    state.position.z);
        ImGui::Text("Yaw: %.1f deg", glm::degrees(state.yaw));
        ImGui::Text("Pitch: %.1f deg", glm::degrees(state.pitch));
        ImGui::Separator();
        ImGui::Text("Sim steps/frame: %d", state.sim_steps_last_frame);
        ImGui::Text("Accumulator alpha: %.3f", state.accumulator_alpha);
        ImGui::Separator();
        ImGui::Text("Clip: %s  t=%.2f", state.active_clip, state.normalized_clip_time);
        ImGui::Text("Combat: %s  combo[%d]", state.combat_phase, state.combo_index);
        ImGui::Text("Attack yaw: %.1f deg", state.attack_yaw_deg);
        if (state.hit_window_end > 0.f) {
            const ImVec4 win_col = state.in_hit_window
                ? ImVec4(1.f, 0.3f, 0.3f, 1.f)
                : ImVec4(0.8f, 0.8f, 0.8f, 1.f);
            ImGui::TextColored(win_col,
                               "Hit window: [%.2f, %.2f]%s",
                               state.hit_window_start,
                               state.hit_window_end,
                               state.in_hit_window ? " <<ACTIVE>>" : "");
            ImGui::Text("Hit consumed: %s", state.hit_consumed ? "yes" : "no");
        }
        ImGui::Separator();
        ImGui::Text("Depth test: %s (T to toggle)", state.depth_test ? "on" : "off");
        ImGui::Text("F5 save  F9 load  LMB attack");
    }
    ImGui::End();
}

void ImGuiOverlay::record(const PassExtensionRecordContext& ctx) {
    if (!initialized_ || !frame_started_) {
        return;
    }
    ImGui::Render();
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), ctx.command_buffer);
    frame_started_ = false;
}

} // namespace engine::movement
