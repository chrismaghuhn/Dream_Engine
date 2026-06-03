#include "engine/render/VulkanDebug.hpp"

#include "engine/render/VkCheck.hpp"

#include <spdlog/spdlog.h>

namespace engine {

namespace {

VKAPI_ATTR VkBool32 VKAPI_CALL debug_callback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                              VkDebugUtilsMessageTypeFlagsEXT /*type*/,
                                              const VkDebugUtilsMessengerCallbackDataEXT* callback_data,
                                              void* /*user_data*/) {
    if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
        SPDLOG_ERROR("[Vulkan validation] {}", callback_data->pMessage);
    } else if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        SPDLOG_WARN("[Vulkan validation] {}", callback_data->pMessage);
    }
    return VK_FALSE;
}

} // namespace

VkResult create_debug_messenger(VkInstance instance, VkDebugUtilsMessengerEXT* out_messenger) {
    const VkDebugUtilsMessengerCreateInfoEXT create_info{
        .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
        .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
        .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                       VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                       VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
        .pfnUserCallback = debug_callback,
    };

    const auto create_fn = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT"));
    if (create_fn == nullptr) {
        return VK_ERROR_EXTENSION_NOT_PRESENT;
    }
    return create_fn(instance, &create_info, nullptr, out_messenger);
}

void destroy_debug_messenger(VkInstance instance, VkDebugUtilsMessengerEXT messenger) {
    if (messenger == VK_NULL_HANDLE) {
        return;
    }

    const auto destroy_fn = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT"));
    if (destroy_fn != nullptr) {
        destroy_fn(instance, messenger, nullptr);
    }
}

} // namespace engine
