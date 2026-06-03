#include "engine/character/render/CharacterPass.hpp"

#include "engine/render/VkCheck.hpp"
#include "engine/render/VulkanContext.hpp"

#include <spdlog/spdlog.h>
#include <stb_image.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

#ifndef ENGINE_SHADER_DIR
#define ENGINE_SHADER_DIR "."
#endif

namespace engine::character {

namespace {

std::vector<std::uint32_t> load_spirv(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        return {};
    }
    const std::streamsize size = file.tellg();
    if (size <= 0 || (size % 4) != 0) {
        return {};
    }
    std::vector<std::uint32_t> code(static_cast<std::size_t>(size / 4));
    file.seekg(0);
    file.read(reinterpret_cast<char*>(code.data()), size);
    return code;
}

// Vertex layout that mirrors CharacterAsset's SkinnedMeshData.
struct GpuVertex {
    glm::vec3 position{};
    glm::vec3 normal{};
    glm::vec2 uv{};
    glm::uvec4 joints{};
    glm::vec4 weights{};
};

} // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

int CharacterPass::add_character(const CharacterAsset& asset) {
    const int handle = static_cast<int>(pending_.size());
    pending_.push_back({asset});
    return handle;
}

void CharacterPass::set_pose(int handle,
                             std::uint32_t frame_slot,
                             const glm::mat4& model,
                             const std::vector<glm::mat4>& bone_matrices) {
    if (handle < 0 || handle >= static_cast<int>(gpu_chars_.size())) {
        return;
    }
    GpuCharacter& ch = gpu_chars_[static_cast<std::size_t>(handle)];
    if (!ch.initialized) {
        return;
    }
    if (frame_slot >= ch.frame_ubos.size()) {
        return;
    }

    ch.model = model;

    // Write view/proj into frame UBO — written in record() from PassExtensionRecordContext.
    // Bone matrices written here.
    if (frame_slot < ch.bone_buffers.size() && ch.bone_buffers[frame_slot].mapped) {
        const std::size_t count = std::min(bone_matrices.size(),
                                           static_cast<std::size_t>(kMaxBones));
        const std::size_t bytes = count * sizeof(glm::mat4);
        std::memcpy(ch.bone_buffers[frame_slot].mapped, bone_matrices.data(), bytes);

        // Zero remaining slots if we have fewer bones than kMaxBones.
        if (count < static_cast<std::size_t>(kMaxBones)) {
            glm::mat4 identity(1.f);
            auto* dst = static_cast<glm::mat4*>(ch.bone_buffers[frame_slot].mapped);
            for (std::size_t i = count; i < static_cast<std::size_t>(kMaxBones); ++i) {
                dst[i] = identity;
            }
        }
    }
}

// ---------------------------------------------------------------------------
// IPassExtension
// ---------------------------------------------------------------------------

void CharacterPass::on_renderer_init(const PassExtensionInitContext& ctx) {
    context_          = ctx.context;
    render_pass_      = ctx.render_pass;
    frames_in_flight_ = ctx.frames_in_flight;

    // One-shot command pool for mesh / texture uploads.
    const VkCommandPoolCreateInfo pool_info{
        .sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags            = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
        .queueFamilyIndex = context_->graphics_queue_family(),
    };
    if (vkCreateCommandPool(context_->device(), &pool_info, nullptr, &one_shot_pool_) !=
        VK_SUCCESS) {
        SPDLOG_ERROR("CharacterPass: failed to create one-shot command pool");
        return;
    }

    if (!create_pipeline(render_pass_)) {
        SPDLOG_ERROR("CharacterPass: pipeline creation failed");
        return;
    }

    // Upload any characters registered before init.
    gpu_chars_.resize(pending_.size());
    for (int i = 0; i < static_cast<int>(pending_.size()); ++i) {
        gpu_chars_[static_cast<std::size_t>(i)].initialized = false;
        if (!init_character_gpu(i)) {
            SPDLOG_ERROR("CharacterPass: failed to init character {}", i);
        }
    }

    SPDLOG_INFO("CharacterPass: initialized ({} characters)", gpu_chars_.size());
}

void CharacterPass::on_renderer_shutdown(VulkanContext& /*context*/) {
    if (context_ == nullptr || context_->device() == VK_NULL_HANDLE) {
        return;
    }
    vkDeviceWaitIdle(context_->device());

    for (int i = 0; i < static_cast<int>(gpu_chars_.size()); ++i) {
        destroy_character_gpu(i);
    }
    gpu_chars_.clear();

    destroy_pipeline();

    if (descriptor_pool_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(context_->device(), descriptor_pool_, nullptr);
        descriptor_pool_ = VK_NULL_HANDLE;
    }
    if (descriptor_set_layout_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(context_->device(), descriptor_set_layout_, nullptr);
        descriptor_set_layout_ = VK_NULL_HANDLE;
    }
    if (one_shot_pool_ != VK_NULL_HANDLE) {
        vkDestroyCommandPool(context_->device(), one_shot_pool_, nullptr);
        one_shot_pool_ = VK_NULL_HANDLE;
    }

    context_ = nullptr;
}

void CharacterPass::on_swapchain_recreate(const PassExtensionInitContext& ctx) {
    render_pass_ = ctx.render_pass;
    destroy_pipeline();
    create_pipeline(render_pass_);
}

void CharacterPass::record(const PassExtensionRecordContext& ctx) {
    if (pipeline_ == VK_NULL_HANDLE) {
        return;
    }

    vkCmdBindPipeline(ctx.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);

    VkViewport viewport{
        .x        = 0.f,
        .y        = 0.f,
        .width    = static_cast<float>(ctx.extent.width),
        .height   = static_cast<float>(ctx.extent.height),
        .minDepth = 0.f,
        .maxDepth = 1.f,
    };
    VkRect2D scissor{{0, 0}, ctx.extent};
    vkCmdSetViewport(ctx.command_buffer, 0, 1, &viewport);
    vkCmdSetScissor(ctx.command_buffer, 0, 1, &scissor);

    for (GpuCharacter& ch : gpu_chars_) {
        if (!ch.initialized) {
            continue;
        }
        if (ctx.frame_slot >= ch.frame_ubos.size()) {
            continue;
        }

        // Update frame UBO (view / proj) for this slot.
        if (ch.frame_mapped[ctx.frame_slot]) {
            struct FrameUniforms { glm::mat4 view, proj; };
            const FrameUniforms fu{ctx.view, ctx.proj};
            std::memcpy(ch.frame_mapped[ctx.frame_slot], &fu, sizeof(fu));
        }

        // Bind descriptor set (frame UBO + bone UBO + texture).
        vkCmdBindDescriptorSets(
            ctx.command_buffer,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            pipeline_layout_,
            0, 1,
            &ch.descriptor_sets[ctx.frame_slot],
            0, nullptr);

        // Push model matrix.
        vkCmdPushConstants(
            ctx.command_buffer,
            pipeline_layout_,
            VK_SHADER_STAGE_VERTEX_BIT,
            0,
            sizeof(glm::mat4),
            &ch.model);

        // Bind mesh buffers and draw.
        const VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(ctx.command_buffer, 0, 1, &ch.mesh.vertex_buffer, &offset);
        vkCmdBindIndexBuffer(ctx.command_buffer, ch.mesh.index_buffer, 0,
                             VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(ctx.command_buffer, ch.mesh.index_count, 1, 0, 0, 0);
    }
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

bool CharacterPass::init_character_gpu(int idx) {
    GpuCharacter& ch = gpu_chars_[static_cast<std::size_t>(idx)];
    const CharacterAsset& asset = pending_[static_cast<std::size_t>(idx)].asset;

    if (!upload_mesh(ch.mesh, asset.mesh)) {
        return false;
    }
    if (!upload_texture(ch.texture, asset.mesh)) {
        return false;
    }
    if (!create_frame_ubos(ch)) {
        return false;
    }
    if (!create_bone_buffers(ch)) {
        return false;
    }
    if (!alloc_descriptor_sets(ch, idx)) {
        return false;
    }

    // Initialize bone buffers with identity matrices.
    const glm::mat4 identity(1.f);
    for (std::size_t slot = 0; slot < frames_in_flight_; ++slot) {
        if (ch.bone_buffers[slot].mapped) {
            auto* dst = static_cast<glm::mat4*>(ch.bone_buffers[slot].mapped);
            for (int b = 0; b < kMaxBones; ++b) {
                dst[b] = identity;
            }
        }
    }

    ch.initialized = true;
    return true;
}

void CharacterPass::destroy_character_gpu(int idx) {
    if (context_ == nullptr || context_->device() == VK_NULL_HANDLE) {
        return;
    }
    GpuCharacter& ch = gpu_chars_[static_cast<std::size_t>(idx)];

    for (auto& bb : ch.bone_buffers) {
        if (bb.mapped) {
            vkUnmapMemory(context_->device(), bb.memory);
            bb.mapped = nullptr;
        }
        if (bb.buffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(context_->device(), bb.buffer, nullptr);
            bb.buffer = VK_NULL_HANDLE;
        }
        if (bb.memory != VK_NULL_HANDLE) {
            vkFreeMemory(context_->device(), bb.memory, nullptr);
            bb.memory = VK_NULL_HANDLE;
        }
    }
    ch.bone_buffers.clear();

    for (std::size_t i = 0; i < ch.frame_ubos.size(); ++i) {
        if (ch.frame_mapped[i]) {
            vkUnmapMemory(context_->device(), ch.frame_mems[i]);
        }
        if (ch.frame_ubos[i] != VK_NULL_HANDLE) {
            vkDestroyBuffer(context_->device(), ch.frame_ubos[i], nullptr);
        }
        if (ch.frame_mems[i] != VK_NULL_HANDLE) {
            vkFreeMemory(context_->device(), ch.frame_mems[i], nullptr);
        }
    }
    ch.frame_ubos.clear();
    ch.frame_mems.clear();
    ch.frame_mapped.clear();

    if (ch.mesh.vertex_buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(context_->device(), ch.mesh.vertex_buffer, nullptr);
    }
    if (ch.mesh.vertex_memory != VK_NULL_HANDLE) {
        vkFreeMemory(context_->device(), ch.mesh.vertex_memory, nullptr);
    }
    if (ch.mesh.index_buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(context_->device(), ch.mesh.index_buffer, nullptr);
    }
    if (ch.mesh.index_memory != VK_NULL_HANDLE) {
        vkFreeMemory(context_->device(), ch.mesh.index_memory, nullptr);
    }
    ch.mesh = {};

    if (ch.texture.sampler != VK_NULL_HANDLE) {
        vkDestroySampler(context_->device(), ch.texture.sampler, nullptr);
    }
    if (ch.texture.view != VK_NULL_HANDLE) {
        vkDestroyImageView(context_->device(), ch.texture.view, nullptr);
    }
    if (ch.texture.image != VK_NULL_HANDLE) {
        vkDestroyImage(context_->device(), ch.texture.image, nullptr);
    }
    if (ch.texture.memory != VK_NULL_HANDLE) {
        vkFreeMemory(context_->device(), ch.texture.memory, nullptr);
    }
    ch.texture = {};
    ch.initialized = false;
}

// ---------------------------------------------------------------------------

bool CharacterPass::upload_mesh(GpuMesh& gpu, const SkinnedMeshData& mesh) {
    const std::size_t vert_count = mesh.positions.size();
    if (vert_count == 0) {
        return false;
    }

    // Interleave vertex data.
    std::vector<GpuVertex> verts(vert_count);
    for (std::size_t i = 0; i < vert_count; ++i) {
        verts[i].position = mesh.positions[i];
        verts[i].normal   = i < mesh.normals.size() ? mesh.normals[i] : glm::vec3(0.f, 1.f, 0.f);
        verts[i].uv       = i < mesh.uvs.size()     ? mesh.uvs[i]     : glm::vec2(0.f);
        verts[i].joints   = i < mesh.joint_indices.size() ? mesh.joint_indices[i] : glm::uvec4(0);
        verts[i].weights  = i < mesh.joint_weights.size() ? mesh.joint_weights[i] : glm::vec4(1.f, 0.f, 0.f, 0.f);
    }

    const VkDeviceSize vb_size = verts.size() * sizeof(GpuVertex);
    const VkDeviceSize ib_size = mesh.indices.size() * sizeof(std::uint32_t);

    // Staging buffers.
    VkDeviceMemory staging_vm = VK_NULL_HANDLE;
    VkBuffer staging_vb = create_buffer(vb_size,
                                        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                        staging_vm);

    VkDeviceMemory staging_im = VK_NULL_HANDLE;
    VkBuffer staging_ib = create_buffer(ib_size,
                                        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                        staging_im);

    void* mapped = nullptr;
    vkMapMemory(context_->device(), staging_vm, 0, vb_size, 0, &mapped);
    std::memcpy(mapped, verts.data(), vb_size);
    vkUnmapMemory(context_->device(), staging_vm);

    vkMapMemory(context_->device(), staging_im, 0, ib_size, 0, &mapped);
    std::memcpy(mapped, mesh.indices.data(), ib_size);
    vkUnmapMemory(context_->device(), staging_im);

    // Device-local buffers.
    gpu.vertex_buffer = create_buffer(vb_size,
                                      VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
                                          VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                      gpu.vertex_memory);
    gpu.index_buffer = create_buffer(ib_size,
                                     VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
                                         VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                     VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                     gpu.index_memory);
    gpu.index_count = static_cast<std::uint32_t>(mesh.indices.size());

    // Copy via one-shot command buffer.
    const VkCommandBuffer cmd = begin_one_shot();
    const VkBufferCopy vb_copy{.size = vb_size};
    const VkBufferCopy ib_copy{.size = ib_size};
    vkCmdCopyBuffer(cmd, staging_vb, gpu.vertex_buffer, 1, &vb_copy);
    vkCmdCopyBuffer(cmd, staging_ib, gpu.index_buffer, 1, &ib_copy);
    end_one_shot(cmd);

    vkDestroyBuffer(context_->device(), staging_vb, nullptr);
    vkFreeMemory(context_->device(), staging_vm, nullptr);
    vkDestroyBuffer(context_->device(), staging_ib, nullptr);
    vkFreeMemory(context_->device(), staging_im, nullptr);

    return true;
}

bool CharacterPass::upload_texture(GpuTexture& gpu, const SkinnedMeshData& mesh) {
    int width = mesh.base_color_width;
    int height = mesh.base_color_height;
    std::vector<std::uint8_t> rgba_data;

    if (width < 0) {
        // Raw encoded bytes — decode with stb_image.
        int channels = 0;
        stbi_uc* decoded = stbi_load_from_memory(
            reinterpret_cast<const stbi_uc*>(mesh.base_color_rgba.data()),
            static_cast<int>(mesh.base_color_rgba.size()),
            &width, &height, &channels, 4);
        if (!decoded) {
            SPDLOG_WARN("CharacterPass: stb_image decode failed; using 2x2 white fallback");
            width = 2; height = 2;
            rgba_data = {255,255,255,255, 255,255,255,255,
                         255,255,255,255, 255,255,255,255};
        } else {
            rgba_data.assign(decoded, decoded + width * height * 4);
            stbi_image_free(decoded);
        }
    } else {
        rgba_data = mesh.base_color_rgba;
    }

    const VkExtent2D extent{static_cast<std::uint32_t>(width),
                            static_cast<std::uint32_t>(height)};
    const VkDeviceSize image_size = static_cast<VkDeviceSize>(width * height * 4);

    // Staging.
    VkDeviceMemory staging_mem = VK_NULL_HANDLE;
    VkBuffer staging_buf = create_buffer(image_size,
                                         VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                             VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                         staging_mem);
    void* mapped = nullptr;
    vkMapMemory(context_->device(), staging_mem, 0, image_size, 0, &mapped);
    std::memcpy(mapped, rgba_data.data(), image_size);
    vkUnmapMemory(context_->device(), staging_mem);

    // Create VkImage.
    const VkImageCreateInfo image_info{
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = VK_FORMAT_R8G8B8A8_SRGB,
        .extent = {extent.width, extent.height, 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    VK_CHECK(vkCreateImage(context_->device(), &image_info, nullptr, &gpu.image));

    VkMemoryRequirements req{};
    vkGetImageMemoryRequirements(context_->device(), gpu.image, &req);
    const VkMemoryAllocateInfo alloc{
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = req.size,
        .memoryTypeIndex = find_memory_type(req.memoryTypeBits,
                                            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT),
    };
    VK_CHECK(vkAllocateMemory(context_->device(), &alloc, nullptr, &gpu.memory));
    VK_CHECK(vkBindImageMemory(context_->device(), gpu.image, gpu.memory, 0));

    // Transition + copy + transition.
    const VkCommandBuffer cmd = begin_one_shot();

    const VkImageMemoryBarrier to_dst{
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = 0,
        .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = gpu.image,
        .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
    };
    vkCmdPipelineBarrier(cmd,
                         VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &to_dst);

    const VkBufferImageCopy copy{
        .bufferOffset = 0,
        .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
        .imageExtent = {extent.width, extent.height, 1},
    };
    vkCmdCopyBufferToImage(cmd, staging_buf, gpu.image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);

    const VkImageMemoryBarrier to_read{
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = gpu.image,
        .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
    };
    vkCmdPipelineBarrier(cmd,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &to_read);
    end_one_shot(cmd);

    vkDestroyBuffer(context_->device(), staging_buf, nullptr);
    vkFreeMemory(context_->device(), staging_mem, nullptr);

    // Image view.
    const VkImageViewCreateInfo view_info{
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = gpu.image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = VK_FORMAT_R8G8B8A8_SRGB,
        .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
    };
    VK_CHECK(vkCreateImageView(context_->device(), &view_info, nullptr, &gpu.view));

    // Sampler.
    const VkSamplerCreateInfo sampler_info{
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_LINEAR,
        .minFilter = VK_FILTER_LINEAR,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .maxAnisotropy = 1.f,
    };
    VK_CHECK(vkCreateSampler(context_->device(), &sampler_info, nullptr, &gpu.sampler));

    return true;
}

bool CharacterPass::create_frame_ubos(GpuCharacter& ch) {
    const VkDeviceSize size = sizeof(glm::mat4) * 2; // view + proj
    ch.frame_ubos.resize(frames_in_flight_, VK_NULL_HANDLE);
    ch.frame_mems.resize(frames_in_flight_, VK_NULL_HANDLE);
    ch.frame_mapped.resize(frames_in_flight_, nullptr);

    for (std::uint32_t i = 0; i < frames_in_flight_; ++i) {
        ch.frame_ubos[i] = create_buffer(size,
                                          VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                              VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                          ch.frame_mems[i]);
        VK_CHECK(vkMapMemory(context_->device(), ch.frame_mems[i], 0,
                             size, 0, &ch.frame_mapped[i]));
    }
    return true;
}

bool CharacterPass::create_bone_buffers(GpuCharacter& ch) {
    const VkDeviceSize size = sizeof(glm::mat4) * kMaxBones;
    ch.bone_buffers.resize(frames_in_flight_);

    for (std::uint32_t i = 0; i < frames_in_flight_; ++i) {
        BoneBuffer& bb = ch.bone_buffers[i];
        bb.buffer = create_buffer(size,
                                  VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                      VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                  bb.memory);
        VK_CHECK(vkMapMemory(context_->device(), bb.memory, 0, size, 0, &bb.mapped));
    }
    return true;
}

bool CharacterPass::alloc_descriptor_sets(GpuCharacter& ch, int /*char_idx*/) {
    const std::size_t count = frames_in_flight_;
    std::vector<VkDescriptorSetLayout> layouts(count, descriptor_set_layout_);

    const VkDescriptorSetAllocateInfo alloc_info{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = descriptor_pool_,
        .descriptorSetCount = static_cast<std::uint32_t>(count),
        .pSetLayouts = layouts.data(),
    };
    ch.descriptor_sets.resize(count, VK_NULL_HANDLE);
    VK_CHECK(vkAllocateDescriptorSets(context_->device(), &alloc_info,
                                       ch.descriptor_sets.data()));

    for (std::uint32_t slot = 0; slot < frames_in_flight_; ++slot) {
        const VkDescriptorBufferInfo frame_buf_info{
            .buffer = ch.frame_ubos[slot],
            .offset = 0,
            .range = sizeof(glm::mat4) * 2,
        };
        const VkDescriptorBufferInfo bone_buf_info{
            .buffer = ch.bone_buffers[slot].buffer,
            .offset = 0,
            .range = sizeof(glm::mat4) * kMaxBones,
        };
        const VkDescriptorImageInfo image_info{
            .sampler     = ch.texture.sampler,
            .imageView   = ch.texture.view,
            .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        };

        const VkWriteDescriptorSet writes[] = {
            {
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet = ch.descriptor_sets[slot],
                .dstBinding = 0,
                .descriptorCount = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                .pBufferInfo = &frame_buf_info,
            },
            {
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet = ch.descriptor_sets[slot],
                .dstBinding = 1,
                .descriptorCount = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                .pBufferInfo = &bone_buf_info,
            },
            {
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet = ch.descriptor_sets[slot],
                .dstBinding = 2,
                .descriptorCount = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .pImageInfo = &image_info,
            },
        };
        vkUpdateDescriptorSets(context_->device(), 3, writes, 0, nullptr);
    }

    return true;
}

bool CharacterPass::create_pipeline(VkRenderPass render_pass) {
    const std::filesystem::path shader_dir(ENGINE_SHADER_DIR);
    const auto vert_code = load_spirv(shader_dir / "character_skinned.vert.spv");
    const auto frag_code = load_spirv(shader_dir / "character_skinned.frag.spv");
    if (vert_code.empty() || frag_code.empty()) {
        SPDLOG_ERROR("CharacterPass: failed to load character_skinned SPIR-V");
        return false;
    }

    auto make_module = [&](const std::vector<std::uint32_t>& code) {
        const VkShaderModuleCreateInfo info{
            .sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
            .codeSize = code.size() * sizeof(std::uint32_t),
            .pCode    = code.data(),
        };
        VkShaderModule mod = VK_NULL_HANDLE;
        VK_CHECK(vkCreateShaderModule(context_->device(), &info, nullptr, &mod));
        shader_modules_.push_back(mod);
        return mod;
    };

    const VkShaderModule vert = make_module(vert_code);
    const VkShaderModule frag = make_module(frag_code);

    const VkPipelineShaderStageCreateInfo stages[] = {
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
         .stage = VK_SHADER_STAGE_VERTEX_BIT,   .module = vert, .pName = "main"},
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
         .stage = VK_SHADER_STAGE_FRAGMENT_BIT, .module = frag, .pName = "main"},
    };

    // Vertex binding: one interleaved buffer.
    const VkVertexInputBindingDescription binding{
        .binding = 0, .stride = sizeof(GpuVertex), .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
    };
    const VkVertexInputAttributeDescription attrs[] = {
        {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(GpuVertex, position)},
        {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(GpuVertex, normal)},
        {2, 0, VK_FORMAT_R32G32_SFLOAT,    offsetof(GpuVertex, uv)},
        {3, 0, VK_FORMAT_R32G32B32A32_UINT, offsetof(GpuVertex, joints)},
        {4, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(GpuVertex, weights)},
    };
    const VkPipelineVertexInputStateCreateInfo vertex_input{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = 1,
        .pVertexBindingDescriptions = &binding,
        .vertexAttributeDescriptionCount = 5,
        .pVertexAttributeDescriptions = attrs,
    };

    const VkPipelineInputAssemblyStateCreateInfo input_assembly{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
    };

    const VkPipelineViewportStateCreateInfo viewport_state{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1, .scissorCount = 1,
    };

    const VkPipelineRasterizationStateCreateInfo rasterization{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode    = VK_CULL_MODE_BACK_BIT,
        .frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .lineWidth   = 1.f,
    };

    const VkPipelineMultisampleStateCreateInfo multisample{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
    };

    const VkPipelineDepthStencilStateCreateInfo depth_stencil{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable  = VK_TRUE,
        .depthWriteEnable = VK_TRUE,
        .depthCompareOp   = VK_COMPARE_OP_LESS,
    };

    const VkPipelineColorBlendAttachmentState blend_att{
        .blendEnable    = VK_FALSE,
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
    };
    const VkPipelineColorBlendStateCreateInfo color_blend{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 1, .pAttachments = &blend_att,
    };

    const VkDynamicState dynamic_states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    const VkPipelineDynamicStateCreateInfo dynamic_state{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = 2, .pDynamicStates = dynamic_states,
    };

    // Descriptor set layout: binding 0 = frame UBO, 1 = bone UBO, 2 = sampler.
    const VkDescriptorSetLayoutBinding dsl_bindings[] = {
        {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,         1, VK_SHADER_STAGE_VERTEX_BIT, nullptr},
        {1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,         1, VK_SHADER_STAGE_VERTEX_BIT, nullptr},
        {2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
    };
    const VkDescriptorSetLayoutCreateInfo dsl_info{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 3, .pBindings = dsl_bindings,
    };
    VK_CHECK(vkCreateDescriptorSetLayout(context_->device(), &dsl_info,
                                          nullptr, &descriptor_set_layout_));

    // Descriptor pool: enough for kMaxCharacters * frames_in_flight sets.
    const std::uint32_t max_sets = kMaxCharacters * frames_in_flight_;
    const VkDescriptorPoolSize pool_sizes[] = {
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,         max_sets * 2},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, max_sets},
    };
    const VkDescriptorPoolCreateInfo pool_info{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = max_sets,
        .poolSizeCount = 2, .pPoolSizes = pool_sizes,
    };
    VK_CHECK(vkCreateDescriptorPool(context_->device(), &pool_info,
                                     nullptr, &descriptor_pool_));

    // Pipeline layout: descriptor set + push constant for model matrix.
    const VkPushConstantRange push_range{
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
        .offset = 0,
        .size = sizeof(glm::mat4),
    };
    const VkPipelineLayoutCreateInfo layout_info{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts = &descriptor_set_layout_,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &push_range,
    };
    VK_CHECK(vkCreatePipelineLayout(context_->device(), &layout_info,
                                     nullptr, &pipeline_layout_));

    const VkGraphicsPipelineCreateInfo pipeline_info{
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount = 2,
        .pStages = stages,
        .pVertexInputState   = &vertex_input,
        .pInputAssemblyState = &input_assembly,
        .pViewportState      = &viewport_state,
        .pRasterizationState = &rasterization,
        .pMultisampleState   = &multisample,
        .pDepthStencilState  = &depth_stencil,
        .pColorBlendState    = &color_blend,
        .pDynamicState       = &dynamic_state,
        .layout              = pipeline_layout_,
        .renderPass          = render_pass,
        .subpass             = 0,
    };
    VK_CHECK(vkCreateGraphicsPipelines(context_->device(), VK_NULL_HANDLE,
                                        1, &pipeline_info, nullptr, &pipeline_));
    return true;
}

void CharacterPass::destroy_pipeline() {
    if (context_ == nullptr || context_->device() == VK_NULL_HANDLE) {
        return;
    }
    if (pipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(context_->device(), pipeline_, nullptr);
        pipeline_ = VK_NULL_HANDLE;
    }
    if (pipeline_layout_ != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(context_->device(), pipeline_layout_, nullptr);
        pipeline_layout_ = VK_NULL_HANDLE;
    }
    for (VkShaderModule mod : shader_modules_) {
        vkDestroyShaderModule(context_->device(), mod, nullptr);
    }
    shader_modules_.clear();
}

// ---------------------------------------------------------------------------
// Vulkan utilities
// ---------------------------------------------------------------------------

VkBuffer CharacterPass::create_buffer(VkDeviceSize size,
                                      VkBufferUsageFlags usage,
                                      VkMemoryPropertyFlags props,
                                      VkDeviceMemory& out_memory) {
    VkBuffer buffer = VK_NULL_HANDLE;
    const VkBufferCreateInfo info{
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size,
        .usage = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    VK_CHECK(vkCreateBuffer(context_->device(), &info, nullptr, &buffer));

    VkMemoryRequirements req{};
    vkGetBufferMemoryRequirements(context_->device(), buffer, &req);
    const VkMemoryAllocateInfo alloc{
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = req.size,
        .memoryTypeIndex = find_memory_type(req.memoryTypeBits, props),
    };
    VK_CHECK(vkAllocateMemory(context_->device(), &alloc, nullptr, &out_memory));
    VK_CHECK(vkBindBufferMemory(context_->device(), buffer, out_memory, 0));
    return buffer;
}

VkCommandBuffer CharacterPass::begin_one_shot() const {
    const VkCommandBufferAllocateInfo alloc{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = one_shot_pool_,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VK_CHECK(vkAllocateCommandBuffers(context_->device(), &alloc, &cmd));

    const VkCommandBufferBeginInfo begin{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    VK_CHECK(vkBeginCommandBuffer(cmd, &begin));
    return cmd;
}

void CharacterPass::end_one_shot(VkCommandBuffer cmd) const {
    VK_CHECK(vkEndCommandBuffer(cmd));
    const VkSubmitInfo submit{
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &cmd,
    };
    VK_CHECK(vkQueueSubmit(context_->graphics_queue(), 1, &submit, VK_NULL_HANDLE));
    vkQueueWaitIdle(context_->graphics_queue());
    vkFreeCommandBuffers(context_->device(), one_shot_pool_, 1, &cmd);
}

std::uint32_t CharacterPass::find_memory_type(std::uint32_t type_filter,
                                               VkMemoryPropertyFlags props) const {
    VkPhysicalDeviceMemoryProperties mem_props{};
    vkGetPhysicalDeviceMemoryProperties(context_->physical_device(), &mem_props);
    for (std::uint32_t i = 0; i < mem_props.memoryTypeCount; ++i) {
        if ((type_filter & (1u << i)) != 0 &&
            (mem_props.memoryTypes[i].propertyFlags & props) == props) {
            return i;
        }
    }
    return UINT32_MAX;
}

} // namespace engine::character
