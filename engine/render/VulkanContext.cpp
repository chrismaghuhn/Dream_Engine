#include "engine/render/VulkanContext.hpp"

#include "engine/platform/Platform.hpp"
#include "engine/render/VkCheck.hpp"
#include "engine/render/VulkanDebug.hpp"

#include <GLFW/glfw3.h>

#include <spdlog/spdlog.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <set>
#include <string_view>
#include <vector>

#define VOLK_IMPLEMENTATION
#include <volk.h>

namespace engine {

namespace {

constexpr const char* kValidationLayerName = "VK_LAYER_KHRONOS_validation";

bool has_layer(const char* layer_name, const std::vector<VkLayerProperties>& available) {
    return std::any_of(available.begin(), available.end(), [layer_name](const VkLayerProperties& layer) {
        return std::string_view(layer.layerName) == layer_name;
    });
}

bool has_extension(const char* extension_name, const std::vector<VkExtensionProperties>& available) {
    return std::any_of(available.begin(), available.end(), [extension_name](const VkExtensionProperties& ext) {
        return std::string_view(ext.extensionName) == extension_name;
    });
}

} // namespace

VulkanContext::~VulkanContext() {
    shutdown();
}

bool VulkanContext::init(Platform& platform, bool enable_validation) {
    if (initialized_) {
        return true;
    }

    validation_enabled_ = enable_validation && check_validation_layer_support();

    VK_CHECK(volkInitialize());

    if (!create_instance(validation_enabled_)) {
        return false;
    }
    volkLoadInstance(instance_);

    if (validation_enabled_ && !setup_debug_messenger()) {
        SPDLOG_WARN("Validation layers enabled but debug messenger setup failed");
    }

    if (!create_surface(platform)) {
        shutdown();
        return false;
    }
    window_ = platform.window();

    if (!pick_physical_device()) {
        shutdown();
        return false;
    }

    if (!create_logical_device()) {
        shutdown();
        return false;
    }
    volkLoadDevice(device_);

    if (!create_swapchain()) {
        shutdown();
        return false;
    }

    initialized_ = true;
    return true;
}

void VulkanContext::shutdown() {
    if (device_ != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(device_);
    }

    destroy_swapchain();

    if (device_ != VK_NULL_HANDLE) {
        vkDestroyDevice(device_, nullptr);
        device_ = VK_NULL_HANDLE;
    }

    if (surface_ != VK_NULL_HANDLE && instance_ != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(instance_, surface_, nullptr);
        surface_ = VK_NULL_HANDLE;
    }

    destroy_debug_messenger(instance_, debug_messenger_);
    debug_messenger_ = VK_NULL_HANDLE;

    if (instance_ != VK_NULL_HANDLE) {
        vkDestroyInstance(instance_, nullptr);
        instance_ = VK_NULL_HANDLE;
    }

    window_ = nullptr;
    initialized_ = false;
}

void VulkanContext::recreate_swapchain() {
    if (device_ == VK_NULL_HANDLE) {
        return;
    }

    vkDeviceWaitIdle(device_);
    destroy_swapchain();
    create_swapchain();
}

GpuCaps VulkanContext::query_gpu_caps() const {
    GpuCaps caps{};
    if (physical_device_ == VK_NULL_HANDLE) {
        return caps;
    }

    VkPhysicalDeviceProperties properties{};
    VkPhysicalDeviceFeatures features{};
    VkPhysicalDeviceMemoryProperties memory_properties{};
    vkGetPhysicalDeviceProperties(physical_device_, &properties);
    vkGetPhysicalDeviceFeatures(physical_device_, &features);
    vkGetPhysicalDeviceMemoryProperties(physical_device_, &memory_properties);

    for (uint32_t i = 0; i < memory_properties.memoryHeapCount; ++i) {
        if ((memory_properties.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) != 0) {
            caps.vram_bytes += static_cast<size_t>(memory_properties.memoryHeaps[i].size);
        }
    }

    caps.multi_draw_indirect = features.multiDrawIndirect == VK_TRUE;
    caps.discrete_gpu = properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;
    caps.descriptor_indexing = false;
    caps.graphics_queue_family = graphics_queue_family_;
    caps.max_memory_allocation_count = properties.limits.maxMemoryAllocationCount;
    caps.max_storage_buffer_range = static_cast<size_t>(properties.limits.maxStorageBufferRange);
    caps.min_uniform_buffer_offset_alignment =
        static_cast<size_t>(properties.limits.minUniformBufferOffsetAlignment);
    caps.non_coherent_atom_size = static_cast<size_t>(properties.limits.nonCoherentAtomSize);

    return caps;
}

bool VulkanContext::check_validation_layer_support() const {
    uint32_t layer_count = 0;
    vkEnumerateInstanceLayerProperties(&layer_count, nullptr);
    std::vector<VkLayerProperties> available(layer_count);
    vkEnumerateInstanceLayerProperties(&layer_count, available.data());
    return has_layer(kValidationLayerName, available);
}

bool VulkanContext::create_instance(bool enable_validation) {
    uint32_t glfw_extension_count = 0;
    const char** glfw_extensions = glfwGetRequiredInstanceExtensions(&glfw_extension_count);
    if (glfw_extensions == nullptr) {
        SPDLOG_ERROR("Failed to query GLFW Vulkan extensions");
        return false;
    }

    std::vector<const char*> extensions(glfw_extensions, glfw_extensions + glfw_extension_count);
    std::vector<const char*> layers;

    if (enable_validation) {
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        layers.push_back(kValidationLayerName);
    }

    const VkApplicationInfo app_info{
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "VoxelEngine",
        .applicationVersion = VK_MAKE_VERSION(0, 1, 0),
        .pEngineName = "VoxelEngine",
        .engineVersion = VK_MAKE_VERSION(0, 1, 0),
        .apiVersion = VK_API_VERSION_1_2,
    };

    const VkInstanceCreateInfo create_info{
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &app_info,
        .enabledLayerCount = static_cast<uint32_t>(layers.size()),
        .ppEnabledLayerNames = layers.empty() ? nullptr : layers.data(),
        .enabledExtensionCount = static_cast<uint32_t>(extensions.size()),
        .ppEnabledExtensionNames = extensions.data(),
    };

    if (vkCreateInstance(&create_info, nullptr, &instance_) != VK_SUCCESS) {
        SPDLOG_ERROR("Failed to create Vulkan instance");
        return false;
    }

    return true;
}

bool VulkanContext::setup_debug_messenger() {
    if (create_debug_messenger(instance_, &debug_messenger_) != VK_SUCCESS) {
        return false;
    }
    return true;
}

bool VulkanContext::create_surface(Platform& platform) {
    if (glfwCreateWindowSurface(instance_, platform.window(), nullptr, &surface_) != VK_SUCCESS) {
        SPDLOG_ERROR("Failed to create window surface");
        return false;
    }
    return true;
}

bool VulkanContext::is_device_suitable(VkPhysicalDevice device) const {
    uint32_t queue_family_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queue_family_count, nullptr);
    std::vector<VkQueueFamilyProperties> queue_families(queue_family_count);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queue_family_count, queue_families.data());

    uint32_t graphics_family = UINT32_MAX;
    uint32_t present_family = UINT32_MAX;
    for (uint32_t i = 0; i < queue_family_count; ++i) {
        if ((queue_families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0) {
            graphics_family = i;
        }

        VkBool32 present_support = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface_, &present_support);
        if (present_support == VK_TRUE) {
            present_family = i;
        }
    }

    if (graphics_family == UINT32_MAX || present_family == UINT32_MAX) {
        return false;
    }

    uint32_t extension_count = 0;
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extension_count, nullptr);
    std::vector<VkExtensionProperties> available_extensions(extension_count);
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extension_count, available_extensions.data());

    const std::set<std::string> required_extensions = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    for (const auto& required : required_extensions) {
        if (!has_extension(required.c_str(), available_extensions)) {
            return false;
        }
    }

    uint32_t format_count = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface_, &format_count, nullptr);
    uint32_t present_mode_count = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface_, &present_mode_count, nullptr);
    return format_count > 0 && present_mode_count > 0;
}

bool VulkanContext::pick_physical_device() {
    uint32_t device_count = 0;
    vkEnumeratePhysicalDevices(instance_, &device_count, nullptr);
    if (device_count == 0) {
        SPDLOG_ERROR("No Vulkan-capable GPUs found");
        return false;
    }

    std::vector<VkPhysicalDevice> devices(device_count);
    vkEnumeratePhysicalDevices(instance_, &device_count, devices.data());

    VkPhysicalDevice best_device = VK_NULL_HANDLE;
    int best_score = -1;

    for (VkPhysicalDevice device : devices) {
        if (!is_device_suitable(device)) {
            continue;
        }

        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(device, &properties);

        int score = 0;
        if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
            score += 1000;
        }
        score += static_cast<int>(properties.limits.maxImageDimension2D);

        if (score > best_score) {
            best_score = score;
            best_device = device;
        }
    }

    if (best_device == VK_NULL_HANDLE) {
        SPDLOG_ERROR("No suitable Vulkan GPU found");
        return false;
    }

    physical_device_ = best_device;
    return true;
}

bool VulkanContext::create_logical_device() {
    uint32_t queue_family_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physical_device_, &queue_family_count, nullptr);
    std::vector<VkQueueFamilyProperties> queue_families(queue_family_count);
    vkGetPhysicalDeviceQueueFamilyProperties(physical_device_, &queue_family_count, queue_families.data());

    graphics_queue_family_ = UINT32_MAX;
    present_queue_family_ = UINT32_MAX;
    for (uint32_t i = 0; i < queue_family_count; ++i) {
        if ((queue_families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0) {
            graphics_queue_family_ = i;
        }

        VkBool32 present_support = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(physical_device_, i, surface_, &present_support);
        if (present_support == VK_TRUE) {
            present_queue_family_ = i;
        }
    }

    if (graphics_queue_family_ == UINT32_MAX || present_queue_family_ == UINT32_MAX) {
        SPDLOG_ERROR("Failed to find required queue families");
        return false;
    }

    std::set<uint32_t> unique_families = {graphics_queue_family_, present_queue_family_};
    std::vector<VkDeviceQueueCreateInfo> queue_create_infos;
    const float queue_priority = 1.0f;
    for (uint32_t family : unique_families) {
        queue_create_infos.push_back(VkDeviceQueueCreateInfo{
            .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
            .queueFamilyIndex = family,
            .queueCount = 1,
            .pQueuePriorities = &queue_priority,
        });
    }

    VkPhysicalDeviceFeatures supported_features{};
    vkGetPhysicalDeviceFeatures(physical_device_, &supported_features);

    VkPhysicalDeviceFeatures device_features{};
    device_features.multiDrawIndirect = supported_features.multiDrawIndirect;
    device_features.fillModeNonSolid = supported_features.fillModeNonSolid;
    device_features.samplerAnisotropy = supported_features.samplerAnisotropy;

    if (device_features.multiDrawIndirect != VK_TRUE || device_features.fillModeNonSolid != VK_TRUE ||
        device_features.samplerAnisotropy != VK_TRUE) {
        SPDLOG_ERROR("GPU missing required Vulkan features");
        return false;
    }

    const std::array<const char*, 1> device_extensions = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};

    const VkDeviceCreateInfo create_info{
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount = static_cast<uint32_t>(queue_create_infos.size()),
        .pQueueCreateInfos = queue_create_infos.data(),
        .enabledExtensionCount = static_cast<uint32_t>(device_extensions.size()),
        .ppEnabledExtensionNames = device_extensions.data(),
        .pEnabledFeatures = &device_features,
    };

    if (vkCreateDevice(physical_device_, &create_info, nullptr, &device_) != VK_SUCCESS) {
        SPDLOG_ERROR("Failed to create logical device");
        return false;
    }

    vkGetDeviceQueue(device_, graphics_queue_family_, 0, &graphics_queue_);
    vkGetDeviceQueue(device_, present_queue_family_, 0, &present_queue_);
    return true;
}

bool VulkanContext::create_swapchain() {
    if (window_ == nullptr) {
        return false;
    }

    VkSurfaceCapabilitiesKHR capabilities{};
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_device_, surface_, &capabilities);

    uint32_t format_count = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device_, surface_, &format_count, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(format_count);
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device_, surface_, &format_count, formats.data());

    uint32_t present_mode_count = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device_, surface_, &present_mode_count, nullptr);
    std::vector<VkPresentModeKHR> present_modes(present_mode_count);
    vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device_, surface_, &present_mode_count, present_modes.data());

    VkSurfaceFormatKHR surface_format = formats[0];
    for (const auto& format : formats) {
        if (format.format == VK_FORMAT_B8G8R8A8_SRGB &&
            format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            surface_format = format;
            break;
        }
    }

    VkPresentModeKHR present_mode = VK_PRESENT_MODE_FIFO_KHR;
    for (const auto mode : present_modes) {
        if (mode == VK_PRESENT_MODE_MAILBOX_KHR) {
            present_mode = mode;
            break;
        }
    }

    VkExtent2D extent = capabilities.currentExtent;
    if (extent.width == UINT32_MAX) {
        int width = 0;
        int height = 0;
        glfwGetFramebufferSize(window_, &width, &height);
        extent.width = static_cast<uint32_t>(std::max(width, 0));
        extent.height = static_cast<uint32_t>(std::max(height, 0));

        extent.width = std::clamp(extent.width,
                                  capabilities.minImageExtent.width,
                                  capabilities.maxImageExtent.width);
        extent.height = std::clamp(extent.height,
                                   capabilities.minImageExtent.height,
                                   capabilities.maxImageExtent.height);
    }

    if (extent.width == 0 || extent.height == 0) {
        swapchain_extent_ = {0, 0};
        return true;
    }

    uint32_t image_count = capabilities.minImageCount + 1;
    if (capabilities.maxImageCount > 0 && image_count > capabilities.maxImageCount) {
        image_count = capabilities.maxImageCount;
    }

    VkSwapchainCreateInfoKHR create_info{
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = surface_,
        .minImageCount = image_count,
        .imageFormat = surface_format.format,
        .imageColorSpace = surface_format.colorSpace,
        .imageExtent = extent,
        .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .preTransform = capabilities.currentTransform,
        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode = present_mode,
        .clipped = VK_TRUE,
    };

    const std::array<uint32_t, 2> queue_family_indices = {graphics_queue_family_, present_queue_family_};
    if (graphics_queue_family_ != present_queue_family_) {
        create_info.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        create_info.queueFamilyIndexCount = static_cast<uint32_t>(queue_family_indices.size());
        create_info.pQueueFamilyIndices = queue_family_indices.data();
    } else {
        create_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }

    if (vkCreateSwapchainKHR(device_, &create_info, nullptr, &swapchain_) != VK_SUCCESS) {
        SPDLOG_ERROR("Failed to create swapchain");
        return false;
    }

    swapchain_format_ = surface_format.format;
    swapchain_extent_ = extent;

    uint32_t swapchain_image_count = 0;
    vkGetSwapchainImagesKHR(device_, swapchain_, &swapchain_image_count, nullptr);
    swapchain_images_.resize(swapchain_image_count);
    vkGetSwapchainImagesKHR(device_, swapchain_, &swapchain_image_count, swapchain_images_.data());

    swapchain_image_views_.resize(swapchain_images_.size());
    for (size_t i = 0; i < swapchain_images_.size(); ++i) {
        const VkImageViewCreateInfo view_info{
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = swapchain_images_[i],
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = swapchain_format_,
            .subresourceRange =
                {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .levelCount = 1,
                    .layerCount = 1,
                },
        };
        VK_CHECK(vkCreateImageView(device_, &view_info, nullptr, &swapchain_image_views_[i]));
    }

    return true;
}

void VulkanContext::destroy_swapchain() {
    for (VkImageView view : swapchain_image_views_) {
        vkDestroyImageView(device_, view, nullptr);
    }
    swapchain_image_views_.clear();
    swapchain_images_.clear();

    if (swapchain_ != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(device_, swapchain_, nullptr);
        swapchain_ = VK_NULL_HANDLE;
    }

    swapchain_extent_ = {};
}

} // namespace engine
