#pragma once

#include "engine/core/EngineConfig.hpp"

#include <cstdint>
#include <vector>

#include <vulkan/vulkan.h>

struct GLFWwindow;

namespace engine {

class Platform;

class VulkanContext {
public:
    VulkanContext() = default;
    ~VulkanContext();

    VulkanContext(const VulkanContext&) = delete;
    VulkanContext& operator=(const VulkanContext&) = delete;

    bool init(Platform& platform, bool enable_validation);
    void shutdown();

    void recreate_swapchain();
    [[nodiscard]] bool has_valid_extent() const { return swapchain_extent_.width > 0 && swapchain_extent_.height > 0; }

    [[nodiscard]] VkInstance instance() const { return instance_; }
    [[nodiscard]] VkPhysicalDevice physical_device() const { return physical_device_; }
    [[nodiscard]] VkDevice device() const { return device_; }
    [[nodiscard]] VkSurfaceKHR surface() const { return surface_; }
    [[nodiscard]] VkSwapchainKHR swapchain() const { return swapchain_; }
    [[nodiscard]] VkFormat swapchain_format() const { return swapchain_format_; }
    [[nodiscard]] VkExtent2D swapchain_extent() const { return swapchain_extent_; }
    [[nodiscard]] uint32_t graphics_queue_family() const { return graphics_queue_family_; }
    [[nodiscard]] VkQueue graphics_queue() const { return graphics_queue_; }
    [[nodiscard]] VkQueue present_queue() const { return present_queue_; }
    [[nodiscard]] const std::vector<VkImage>& swapchain_images() const { return swapchain_images_; }
    [[nodiscard]] const std::vector<VkImageView>& swapchain_image_views() const { return swapchain_image_views_; }

    [[nodiscard]] GpuCaps query_gpu_caps() const;

private:
    bool create_instance(bool enable_validation);
    bool setup_debug_messenger();
    bool create_surface(Platform& platform);
    bool pick_physical_device();
    bool create_logical_device();
    bool create_swapchain();
    void destroy_swapchain();

    bool check_validation_layer_support() const;
    [[nodiscard]] bool is_device_suitable(VkPhysicalDevice device) const;

    VkInstance instance_ = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT debug_messenger_ = VK_NULL_HANDLE;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkPhysicalDevice physical_device_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;

    VkQueue graphics_queue_ = VK_NULL_HANDLE;
    VkQueue present_queue_ = VK_NULL_HANDLE;
    uint32_t graphics_queue_family_ = 0;
    uint32_t present_queue_family_ = 0;

    VkFormat swapchain_format_ = VK_FORMAT_UNDEFINED;
    VkExtent2D swapchain_extent_{};
    std::vector<VkImage> swapchain_images_;
    std::vector<VkImageView> swapchain_image_views_;

    bool validation_enabled_ = false;
    bool initialized_ = false;
    GLFWwindow* window_ = nullptr;
};

} // namespace engine
