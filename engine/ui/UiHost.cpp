#include "engine/ui/UiHost.hpp"

#include "engine/platform/Platform.hpp"
#include "engine/render/Renderer.hpp"
#include "engine/render/VkCheck.hpp"
#include "engine/render/VulkanContext.hpp"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>

#include <spdlog/spdlog.h>

#include <cstdint>

namespace engine {

namespace {

[[nodiscard]] VkDescriptorPool create_imgui_descriptor_pool(VkDevice device) {
    constexpr std::uint32_t kPoolSize = 1000;
    const VkDescriptorPoolSize pool_sizes[] = {
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
    };

    const VkDescriptorPoolCreateInfo pool_info{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
        .maxSets = kPoolSize,
        .poolSizeCount = static_cast<uint32_t>(std::size(pool_sizes)),
        .pPoolSizes = pool_sizes,
    };

    VkDescriptorPool pool = VK_NULL_HANDLE;
    if (vkCreateDescriptorPool(device, &pool_info, nullptr, &pool) != VK_SUCCESS) {
        return VK_NULL_HANDLE;
    }
    return pool;
}

[[nodiscard]] ImGui_ImplVulkan_InitInfo make_vulkan_init_info(Renderer& renderer,
                                                              VkDescriptorPool descriptor_pool) {
    const VulkanContext& context = renderer.vulkan_context();
    VkPhysicalDeviceProperties device_properties{};
    vkGetPhysicalDeviceProperties(context.physical_device(), &device_properties);

    ImGui_ImplVulkan_InitInfo info{};
    info.ApiVersion = device_properties.apiVersion;
    info.Instance = context.instance();
    info.PhysicalDevice = context.physical_device();
    info.Device = context.device();
    info.QueueFamily = context.graphics_queue_family();
    info.Queue = context.graphics_queue();
    info.DescriptorPool = descriptor_pool;
    info.MinImageCount = Renderer::kFramesInFlight;
    info.ImageCount = static_cast<uint32_t>(context.swapchain_images().size());
    info.Allocator = nullptr;
    info.PipelineCache = VK_NULL_HANDLE;
    info.RenderPass = renderer.render_pass();
    info.Subpass = 0;
    info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    info.CheckVkResultFn = [](VkResult result) { VK_CHECK(result); };
    return info;
}

} // namespace

bool UiHost::init(Platform& platform, Renderer& renderer) {
    if (initialized_) {
        return true;
    }

    if (!renderer.initialized()) {
        SPDLOG_ERROR("UiHost requires an initialized Renderer");
        return false;
    }

    window_ = platform.window();
    if (window_ == nullptr) {
        SPDLOG_ERROR("UiHost requires a valid GLFW window");
        return false;
    }

    const VulkanContext& context = renderer.vulkan_context();
    device_ = context.device();
    descriptor_pool_ = create_imgui_descriptor_pool(device_);
    if (descriptor_pool_ == VK_NULL_HANDLE) {
        SPDLOG_ERROR("Failed to create ImGui descriptor pool");
        return false;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();

    ImGui_ImplGlfw_InitForVulkan(window_, true);

    ImGui_ImplVulkan_InitInfo init_info =
        make_vulkan_init_info(renderer, descriptor_pool_);
    if (!ImGui_ImplVulkan_Init(&init_info)) {
        SPDLOG_ERROR("ImGui_ImplVulkan_Init failed");
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        destroy_descriptor_pool(context.device());
        return false;
    }

    if (!ImGui_ImplVulkan_CreateFontsTexture()) {
        SPDLOG_ERROR("ImGui_ImplVulkan_CreateFontsTexture failed");
        ImGui_ImplVulkan_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        destroy_descriptor_pool(context.device());
        return false;
    }

    initialized_ = true;
    SPDLOG_INFO("UiHost initialized (ImGui + GLFW + Vulkan)");
    return true;
}

void UiHost::shutdown() {
    if (!initialized_) {
        return;
    }

    if (device_ != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(device_);
    }

    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    destroy_descriptor_pool(device_);

    window_ = nullptr;
    device_ = VK_NULL_HANDLE;
    initialized_ = false;
}

void UiHost::destroy_descriptor_pool(VkDevice device) {
    if (descriptor_pool_ != VK_NULL_HANDLE && device != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device, descriptor_pool_, nullptr);
    }
    descriptor_pool_ = VK_NULL_HANDLE;
}

void UiHost::new_frame(const UiOverlayStats& stats) {
    if (!initialized_) {
        return;
    }

    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    ImGui::SetNextWindowPos(ImVec2(10.f, 10.f), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.35f);
    if (ImGui::Begin("Debug",
                      nullptr,
                      ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                          ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                          ImGuiWindowFlags_NoNav)) {
        ImGui::Text("FPS: %.1f", stats.fps);
        ImGui::Text("Sim tick: %llu", static_cast<unsigned long long>(stats.sim_tick));
        ImGui::Text("Loaded chunks: %u", stats.loaded_chunks);
    }
    ImGui::End();
}

void UiHost::render(VkCommandBuffer command_buffer) {
    if (!initialized_) {
        return;
    }

    ImGui::Render();
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), command_buffer);
}

void UiHost::on_swapchain_recreated(Renderer& renderer) {
    if (!initialized_) {
        return;
    }

    const VulkanContext& context = renderer.vulkan_context();
    vkDeviceWaitIdle(context.device());

    ImGui_ImplVulkan_Shutdown();

    ImGui_ImplVulkan_InitInfo init_info =
        make_vulkan_init_info(renderer, descriptor_pool_);
    if (!ImGui_ImplVulkan_Init(&init_info)) {
        SPDLOG_ERROR("ImGui_ImplVulkan_Init failed after swapchain recreate");
        return;
    }

    if (!ImGui_ImplVulkan_CreateFontsTexture()) {
        SPDLOG_ERROR("ImGui_ImplVulkan_CreateFontsTexture failed after swapchain recreate");
    }
}

} // namespace engine
