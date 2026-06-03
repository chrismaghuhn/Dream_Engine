#include "engine/render/BlockTextureArray.hpp"

#include "engine/render/VkCheck.hpp"
#include "engine/render/VulkanContext.hpp"

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <spdlog/spdlog.h>

#include <array>
#include <cstring>
#include <vector>

namespace engine {

namespace {

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

// RGBA pixels for a single layer; falls back to a solid magenta tile of the
// target size when the source image is missing or has a mismatched size.
std::vector<unsigned char> load_layer_rgba(const std::filesystem::path& path,
                                           std::uint32_t target_w,
                                           std::uint32_t target_h) {
    const std::size_t expected = static_cast<std::size_t>(target_w) * target_h * 4u;
    int w = 0;
    int h = 0;
    int channels = 0;
    unsigned char* pixels = stbi_load(path.string().c_str(), &w, &h, &channels, STBI_rgb_alpha);
    if (pixels == nullptr) {
        SPDLOG_WARN("BlockTextureArray: failed to load '{}' ({}), using magenta fallback",
                    path.string(),
                    stbi_failure_reason() != nullptr ? stbi_failure_reason() : "unknown");
    } else if (static_cast<std::uint32_t>(w) != target_w ||
               static_cast<std::uint32_t>(h) != target_h) {
        SPDLOG_WARN("BlockTextureArray: '{}' is {}x{}, expected {}x{}, using magenta fallback",
                    path.string(),
                    w,
                    h,
                    target_w,
                    target_h);
        stbi_image_free(pixels);
        pixels = nullptr;
    }

    std::vector<unsigned char> out(expected);
    if (pixels != nullptr) {
        std::memcpy(out.data(), pixels, expected);
        stbi_image_free(pixels);
    } else {
        for (std::size_t i = 0; i < expected; i += 4) {
            out[i + 0] = 255;
            out[i + 1] = 0;
            out[i + 2] = 255;
            out[i + 3] = 255;
        }
    }
    return out;
}

} // namespace

BlockTextureArray::~BlockTextureArray() {
    shutdown();
}

bool BlockTextureArray::init(VulkanContext& context,
                             const std::vector<std::filesystem::path>& layer_files) {
    if (layer_files.empty()) {
        SPDLOG_ERROR("BlockTextureArray: no layer files provided");
        return false;
    }

    context_ = &context;
    const VkDevice device = context.device();

    // Probe the first readable image to fix the array dimensions.
    width_ = 16;
    height_ = 16;
    for (const std::filesystem::path& path : layer_files) {
        int w = 0;
        int h = 0;
        int c = 0;
        if (stbi_info(path.string().c_str(), &w, &h, &c) != 0) {
            width_ = static_cast<std::uint32_t>(w);
            height_ = static_cast<std::uint32_t>(h);
            break;
        }
    }
    layer_count_ = static_cast<std::uint32_t>(layer_files.size());

    const std::size_t layer_bytes = static_cast<std::size_t>(width_) * height_ * 4u;
    const std::size_t total_bytes = layer_bytes * layer_count_;

    std::vector<unsigned char> staging_pixels;
    staging_pixels.reserve(total_bytes);
    for (const std::filesystem::path& path : layer_files) {
        std::vector<unsigned char> layer = load_layer_rgba(path, width_, height_);
        staging_pixels.insert(staging_pixels.end(), layer.begin(), layer.end());
    }

    // --- Staging buffer (host visible) ---
    VkBuffer staging_buffer = VK_NULL_HANDLE;
    VkDeviceMemory staging_memory = VK_NULL_HANDLE;
    const VkBufferCreateInfo buffer_info{
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = total_bytes,
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    VK_CHECK(vkCreateBuffer(device, &buffer_info, nullptr, &staging_buffer));

    VkMemoryRequirements staging_req{};
    vkGetBufferMemoryRequirements(device, staging_buffer, &staging_req);
    const std::uint32_t staging_type = find_memory_type(
        context.physical_device(),
        staging_req.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (staging_type == UINT32_MAX) {
        SPDLOG_ERROR("BlockTextureArray: no host-visible memory type");
        vkDestroyBuffer(device, staging_buffer, nullptr);
        return false;
    }
    const VkMemoryAllocateInfo staging_alloc{
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = staging_req.size,
        .memoryTypeIndex = staging_type,
    };
    VK_CHECK(vkAllocateMemory(device, &staging_alloc, nullptr, &staging_memory));
    VK_CHECK(vkBindBufferMemory(device, staging_buffer, staging_memory, 0));

    void* mapped = nullptr;
    VK_CHECK(vkMapMemory(device, staging_memory, 0, total_bytes, 0, &mapped));
    std::memcpy(mapped, staging_pixels.data(), total_bytes);
    vkUnmapMemory(device, staging_memory);

    // --- Device-local array image ---
    const VkImageCreateInfo image_info{
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .extent = {width_, height_, 1},
        .mipLevels = 1,
        .arrayLayers = layer_count_,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    VK_CHECK(vkCreateImage(device, &image_info, nullptr, &image_));

    VkMemoryRequirements image_req{};
    vkGetImageMemoryRequirements(device, image_, &image_req);
    const std::uint32_t image_type = find_memory_type(
        context.physical_device(), image_req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (image_type == UINT32_MAX) {
        SPDLOG_ERROR("BlockTextureArray: no device-local memory type");
        vkDestroyBuffer(device, staging_buffer, nullptr);
        vkFreeMemory(device, staging_memory, nullptr);
        return false;
    }
    const VkMemoryAllocateInfo image_alloc{
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = image_req.size,
        .memoryTypeIndex = image_type,
    };
    VK_CHECK(vkAllocateMemory(device, &image_alloc, nullptr, &memory_));
    VK_CHECK(vkBindImageMemory(device, image_, memory_, 0));

    // --- One-time upload: transition, copy, transition ---
    VkCommandPool pool = VK_NULL_HANDLE;
    const VkCommandPoolCreateInfo pool_info{
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
        .queueFamilyIndex = context.graphics_queue_family(),
    };
    VK_CHECK(vkCreateCommandPool(device, &pool_info, nullptr, &pool));

    VkCommandBuffer cmd = VK_NULL_HANDLE;
    const VkCommandBufferAllocateInfo cmd_alloc{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    VK_CHECK(vkAllocateCommandBuffers(device, &cmd_alloc, &cmd));

    const VkCommandBufferBeginInfo begin_info{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    VK_CHECK(vkBeginCommandBuffer(cmd, &begin_info));

    VkImageMemoryBarrier to_transfer{
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = 0,
        .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image_,
        .subresourceRange =
            {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .levelCount = 1,
                .layerCount = layer_count_,
            },
    };
    vkCmdPipelineBarrier(cmd,
                         VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0,
                         0,
                         nullptr,
                         0,
                         nullptr,
                         1,
                         &to_transfer);

    std::vector<VkBufferImageCopy> regions(layer_count_);
    for (std::uint32_t layer = 0; layer < layer_count_; ++layer) {
        regions[layer] = VkBufferImageCopy{
            .bufferOffset = static_cast<VkDeviceSize>(layer) * layer_bytes,
            .bufferRowLength = 0,
            .bufferImageHeight = 0,
            .imageSubresource =
                {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .mipLevel = 0,
                    .baseArrayLayer = layer,
                    .layerCount = 1,
                },
            .imageOffset = {0, 0, 0},
            .imageExtent = {width_, height_, 1},
        };
    }
    vkCmdCopyBufferToImage(cmd,
                           staging_buffer,
                           image_,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           layer_count_,
                           regions.data());

    VkImageMemoryBarrier to_shader{
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image_,
        .subresourceRange =
            {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .levelCount = 1,
                .layerCount = layer_count_,
            },
    };
    vkCmdPipelineBarrier(cmd,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0,
                         0,
                         nullptr,
                         0,
                         nullptr,
                         1,
                         &to_shader);

    VK_CHECK(vkEndCommandBuffer(cmd));

    const VkSubmitInfo submit{
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &cmd,
    };
    VK_CHECK(vkQueueSubmit(context.graphics_queue(), 1, &submit, VK_NULL_HANDLE));
    VK_CHECK(vkQueueWaitIdle(context.graphics_queue()));

    vkDestroyCommandPool(device, pool, nullptr);
    vkDestroyBuffer(device, staging_buffer, nullptr);
    vkFreeMemory(device, staging_memory, nullptr);

    // --- View + sampler ---
    const VkImageViewCreateInfo view_info{
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = image_,
        .viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY,
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .subresourceRange =
            {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .levelCount = 1,
                .layerCount = layer_count_,
            },
    };
    VK_CHECK(vkCreateImageView(device, &view_info, nullptr, &image_view_));

    const VkSamplerCreateInfo sampler_info{
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_NEAREST,
        .minFilter = VK_FILTER_NEAREST,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .maxLod = VK_LOD_CLAMP_NONE,
        .borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK,
    };
    VK_CHECK(vkCreateSampler(device, &sampler_info, nullptr, &sampler_));

    SPDLOG_INFO("BlockTextureArray: {} layers at {}x{} uploaded", layer_count_, width_, height_);
    return true;
}

void BlockTextureArray::shutdown() {
    if (context_ == nullptr || context_->device() == VK_NULL_HANDLE) {
        return;
    }
    const VkDevice device = context_->device();
    if (sampler_ != VK_NULL_HANDLE) {
        vkDestroySampler(device, sampler_, nullptr);
        sampler_ = VK_NULL_HANDLE;
    }
    if (image_view_ != VK_NULL_HANDLE) {
        vkDestroyImageView(device, image_view_, nullptr);
        image_view_ = VK_NULL_HANDLE;
    }
    if (image_ != VK_NULL_HANDLE) {
        vkDestroyImage(device, image_, nullptr);
        image_ = VK_NULL_HANDLE;
    }
    if (memory_ != VK_NULL_HANDLE) {
        vkFreeMemory(device, memory_, nullptr);
        memory_ = VK_NULL_HANDLE;
    }
    context_ = nullptr;
}

} // namespace engine
