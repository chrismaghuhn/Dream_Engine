#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

#include <volk.h>

namespace engine {

class VulkanContext;

/// GPU 2D texture array holding one pixel-art tile per block face-category.
/// Layers are uploaded once at startup and sampled by the terrain fragment
/// shader via `sampler2DArray` (binding 1). Layer order is defined by the
/// caller and must match `layer_for()` in terrain.frag.
class BlockTextureArray {
public:
    BlockTextureArray() = default;
    ~BlockTextureArray();

    BlockTextureArray(const BlockTextureArray&) = delete;
    BlockTextureArray& operator=(const BlockTextureArray&) = delete;

    /// Loads `layer_files` (RGBA, all the same size) into a device-local 2D
    /// array. Missing/unreadable files fall back to a solid magenta tile so the
    /// renderer still runs. Returns false only on unrecoverable Vulkan errors.
    bool init(VulkanContext& context, const std::vector<std::filesystem::path>& layer_files);
    void shutdown();

    [[nodiscard]] VkImageView image_view() const { return image_view_; }
    [[nodiscard]] VkSampler sampler() const { return sampler_; }
    [[nodiscard]] bool valid() const {
        return image_view_ != VK_NULL_HANDLE && sampler_ != VK_NULL_HANDLE;
    }

private:
    VulkanContext* context_ = nullptr;
    VkImage image_ = VK_NULL_HANDLE;
    VkDeviceMemory memory_ = VK_NULL_HANDLE;
    VkImageView image_view_ = VK_NULL_HANDLE;
    VkSampler sampler_ = VK_NULL_HANDLE;
    std::uint32_t width_ = 0;
    std::uint32_t height_ = 0;
    std::uint32_t layer_count_ = 0;
};

} // namespace engine
