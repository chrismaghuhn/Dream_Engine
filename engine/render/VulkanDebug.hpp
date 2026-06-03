#pragma once

#include <vulkan/vulkan.h>

namespace engine {

VkResult create_debug_messenger(VkInstance instance, VkDebugUtilsMessengerEXT* out_messenger);
void destroy_debug_messenger(VkInstance instance, VkDebugUtilsMessengerEXT messenger);

} // namespace engine
