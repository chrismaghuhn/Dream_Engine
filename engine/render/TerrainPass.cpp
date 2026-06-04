#include "engine/render/TerrainPass.hpp"

#include "engine/render/HostMemory.hpp"
#include "engine/render/Renderer.hpp"
#include "engine/render/VkCheck.hpp"
#include "engine/world/SectionIndexing.hpp"

#include <fstream>
#include <span>
#include <vector>

namespace engine {

namespace {

[[nodiscard]] std::vector<std::uint32_t> load_spirv(const std::filesystem::path& path) {
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

} // namespace

TerrainPass::~TerrainPass() {
    shutdown();
}

bool TerrainPass::load_shader_module(const std::filesystem::path& path, VkShaderModule& out_module) {
    const std::vector<std::uint32_t> code = load_spirv(path);
    if (code.empty()) {
        return false;
    }

    const VkShaderModuleCreateInfo create_info{
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = code.size() * sizeof(std::uint32_t),
        .pCode = code.data(),
    };
    VK_CHECK(vkCreateShaderModule(context_->device(), &create_info, nullptr, &out_module));
    shader_modules_.push_back(out_module);
    return true;
}

bool TerrainPass::create_descriptor_layout() {
    const VkDescriptorSetLayoutBinding bindings[] = {
        {
            .binding = 0,
            .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
        },
        {
            // Block texture array sampled by terrain.frag (also part of the
            // water pass pipeline layout for compatibility; water.frag ignores it).
            .binding = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
        },
    };

    const VkDescriptorSetLayoutCreateInfo layout_info{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 2,
        .pBindings = bindings,
    };
    VK_CHECK(vkCreateDescriptorSetLayout(context_->device(), &layout_info, nullptr, &descriptor_layout_));

    const VkDescriptorPoolSize pool_sizes[] = {
        {
            .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .descriptorCount = per_frame_writes_->frames_in_flight(),
        },
        {
            .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = per_frame_writes_->frames_in_flight(),
        },
    };
    const VkDescriptorPoolCreateInfo pool_info{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = per_frame_writes_->frames_in_flight(),
        .poolSizeCount = 2,
        .pPoolSizes = pool_sizes,
    };
    VK_CHECK(vkCreateDescriptorPool(context_->device(), &pool_info, nullptr, &descriptor_pool_));

    descriptor_sets_.resize(per_frame_writes_->frames_in_flight());
    std::vector<VkDescriptorSetLayout> layouts(descriptor_sets_.size(), descriptor_layout_);
    const VkDescriptorSetAllocateInfo alloc_info{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = descriptor_pool_,
        .descriptorSetCount = static_cast<std::uint32_t>(descriptor_sets_.size()),
        .pSetLayouts = layouts.data(),
    };
    VK_CHECK(vkAllocateDescriptorSets(context_->device(), &alloc_info, descriptor_sets_.data()));

    for (std::uint32_t i = 0; i < descriptor_sets_.size(); ++i) {
        const PerFrameGpuWrites& slot = per_frame_writes_->slot(i);
        const VkDescriptorBufferInfo buffer_info{
            .buffer = slot.frame_ubo,
            .offset = 0,
            .range = slot.ubo_size,
        };
        const VkWriteDescriptorSet write{
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = descriptor_sets_[i],
            .dstBinding = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .pBufferInfo = &buffer_info,
        };
        vkUpdateDescriptorSets(context_->device(), 1, &write, 0, nullptr);
    }

    return true;
}

void TerrainPass::bind_block_texture(VkImageView image_view, VkSampler sampler) {
    if (image_view == VK_NULL_HANDLE || sampler == VK_NULL_HANDLE) {
        return;
    }
    const VkDescriptorImageInfo image_info{
        .sampler = sampler,
        .imageView = image_view,
        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
    };
    for (VkDescriptorSet set : descriptor_sets_) {
        const VkWriteDescriptorSet write{
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = set,
            .dstBinding = 1,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .pImageInfo = &image_info,
        };
        vkUpdateDescriptorSets(context_->device(), 1, &write, 0, nullptr);
    }
}

bool TerrainPass::create_pipeline(VkRenderPass render_pass, const std::filesystem::path& shader_dir) {
    VkShaderModule vert_module = VK_NULL_HANDLE;
    VkShaderModule frag_module = VK_NULL_HANDLE;
    if (!load_shader_module(shader_dir / "terrain.vert.spv", vert_module) ||
        !load_shader_module(shader_dir / "terrain.frag.spv", frag_module)) {
        return false;
    }

    const VkPipelineShaderStageCreateInfo stages[] = {
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_VERTEX_BIT,
            .module = vert_module,
            .pName = "main",
        },
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
            .module = frag_module,
            .pName = "main",
        },
    };

    const VkVertexInputBindingDescription binding{
        .binding = 0,
        .stride = sizeof(TerrainVertex),
        .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
    };
    const VkVertexInputAttributeDescription attributes[] = {
        {
            .location = 0,
            .binding = 0,
            .format = VK_FORMAT_R32_UINT,
            .offset = offsetof(TerrainVertex, packed_position_normal),
        },
        {
            .location = 1,
            .binding = 0,
            .format = VK_FORMAT_R16_UINT,
            .offset = offsetof(TerrainVertex, material_id),
        },
        {
            .location = 2,
            .binding = 0,
            .format = VK_FORMAT_R8_UINT,
            .offset = offsetof(TerrainVertex, ao),
        },
        {
            .location = 3,
            .binding = 0,
            .format = VK_FORMAT_R8_UINT,
            .offset = offsetof(TerrainVertex, light),
        },
    };

    const VkPipelineVertexInputStateCreateInfo vertex_input{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = 1,
        .pVertexBindingDescriptions = &binding,
        .vertexAttributeDescriptionCount = 4,
        .pVertexAttributeDescriptions = attributes,
    };

    const VkPipelineInputAssemblyStateCreateInfo input_assembly{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
    };

    const VkPipelineViewportStateCreateInfo viewport_state{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .scissorCount = 1,
    };

    const VkPipelineRasterizationStateCreateInfo rasterization{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode = VK_CULL_MODE_BACK_BIT,
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .lineWidth = 1.f,
    };

    const VkPipelineMultisampleStateCreateInfo multisample{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
    };

    const VkPipelineDepthStencilStateCreateInfo depth_stencil{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable = VK_TRUE,
        .depthWriteEnable = VK_TRUE,
        .depthCompareOp = VK_COMPARE_OP_LESS,
    };

    const VkPipelineColorBlendAttachmentState blend_attachment{
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
                          VK_COLOR_COMPONENT_A_BIT,
    };
    const VkPipelineColorBlendStateCreateInfo color_blend{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments = &blend_attachment,
    };

    const VkDynamicState dynamic_states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    const VkPipelineDynamicStateCreateInfo dynamic_state{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = 2,
        .pDynamicStates = dynamic_states,
    };

    const VkPushConstantRange push_constant{
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
        .offset = 0,
        .size = sizeof(DrawPushConstants),
    };

    const VkPipelineLayoutCreateInfo layout_info{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts = &descriptor_layout_,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &push_constant,
    };
    VK_CHECK(vkCreatePipelineLayout(context_->device(), &layout_info, nullptr, &pipeline_layout_));

    const VkPipelineRenderingCreateInfo rendering_info{}; // unused for render pass pipeline

    const VkGraphicsPipelineCreateInfo pipeline_info{
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount = 2,
        .pStages = stages,
        .pVertexInputState = &vertex_input,
        .pInputAssemblyState = &input_assembly,
        .pViewportState = &viewport_state,
        .pRasterizationState = &rasterization,
        .pMultisampleState = &multisample,
        .pDepthStencilState = &depth_stencil,
        .pColorBlendState = &color_blend,
        .pDynamicState = &dynamic_state,
        .layout = pipeline_layout_,
        .renderPass = render_pass,
        .subpass = 0,
    };
    (void)rendering_info;
    VK_CHECK(vkCreateGraphicsPipelines(
        context_->device(), VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &pipeline_));

    return true;
}

bool TerrainPass::init(VulkanContext& context,
                       VkRenderPass render_pass,
                       const std::filesystem::path& shader_dir,
                       PerFrameGpuWriteRing& per_frame_writes) {
    context_ = &context;
    per_frame_writes_ = &per_frame_writes;

    if (!create_descriptor_layout()) {
        shutdown();
        return false;
    }

    if (!create_pipeline(render_pass, shader_dir)) {
        shutdown();
        return false;
    }

    return true;
}

void TerrainPass::shutdown() {
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
    if (descriptor_pool_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(context_->device(), descriptor_pool_, nullptr);
        descriptor_pool_ = VK_NULL_HANDLE;
    }
    if (descriptor_layout_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(context_->device(), descriptor_layout_, nullptr);
        descriptor_layout_ = VK_NULL_HANDLE;
    }

    for (VkShaderModule module : shader_modules_) {
        vkDestroyShaderModule(context_->device(), module, nullptr);
    }
    shader_modules_.clear();
    descriptor_sets_.clear();

    context_ = nullptr;
    per_frame_writes_ = nullptr;
}

void TerrainPass::write_frame_uniforms(const std::uint64_t frame_index, const FrameUniformGpu& uniforms) {
    if (per_frame_writes_ == nullptr) {
        return;
    }

    PerFrameGpuWrites& slot = per_frame_writes_->slot(frame_index);
    void* mapped = nullptr;
    VK_CHECK(vkMapMemory(context_->device(), slot.memory, 0, slot.ubo_size, 0, &mapped));
    host_write(context_->device(),
               mapped,
               0,
               sizeof(FrameUniformGpu),
               &uniforms,
               slot.memory,
               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
               context_->query_gpu_caps());
    vkUnmapMemory(context_->device(), slot.memory);
}

void TerrainPass::write_indirect_commands(const std::uint64_t frame_index,
                                          const WorldRenderSnapshot& snapshot,
                                          const GpuMeshPool& mesh_pool) {
    if (per_frame_writes_ == nullptr) {
        return;
    }

    PerFrameGpuWrites& slot = per_frame_writes_->slot(frame_index);
    std::vector<VkDrawIndexedIndirectCommand> commands;
    commands.reserve(snapshot.opaque_sections.size());

    const std::size_t max_commands =
        std::min(snapshot.opaque_sections.size(), Renderer::kMaxIndirectDraws);
    commands.reserve(max_commands);
    for (std::size_t i = 0; i < max_commands; ++i) {
        const DrawSection& section = snapshot.opaque_sections[i];
        const GpuMeshSlot* mesh_slot = mesh_pool.slot(section.vertex_buffer_id);
        const std::uint32_t index_count = clamp_index_count(mesh_slot, section.index_count);
        commands.push_back(VkDrawIndexedIndirectCommand{
            .indexCount = index_count,
            .instanceCount = 1,
            .firstIndex = 0,
            .vertexOffset = 0,
            .firstInstance = 0,
        });
    }

    const std::size_t indirect_offset = per_frame_writes_->aligned_dynamic_offset(slot.ubo_size);
    void* mapped = nullptr;
    VK_CHECK(vkMapMemory(context_->device(), slot.memory, 0, slot.memory_size, 0, &mapped));
    if (!commands.empty()) {
        host_write(context_->device(),
                   mapped,
                   indirect_offset,
                   commands.size() * sizeof(VkDrawIndexedIndirectCommand),
                   commands.data(),
                   slot.memory,
                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                   context_->query_gpu_caps());
    }
    vkUnmapMemory(context_->device(), slot.memory);
}

VkDescriptorSet TerrainPass::frame_descriptor_set(const std::uint64_t frame_index) const {
    if (per_frame_writes_ == nullptr || descriptor_sets_.empty()) {
        return VK_NULL_HANDLE;
    }
    return descriptor_sets_[per_frame_writes_->ring_index(frame_index)];
}

void TerrainPass::record(VkCommandBuffer command_buffer,
                           const std::uint64_t frame_index,
                           const WorldRenderSnapshot& snapshot,
                           const GpuMeshPool& mesh_pool,
                           const VkExtent2D extent) {
    if (pipeline_ == VK_NULL_HANDLE || snapshot.opaque_sections.empty()) {
        return;
    }

    const std::uint32_t ring = per_frame_writes_->ring_index(frame_index);
    const PerFrameGpuWrites& slot = per_frame_writes_->slot(frame_index);
    const std::size_t indirect_offset = per_frame_writes_->aligned_dynamic_offset(slot.ubo_size);

    VkViewport viewport{
        .width = static_cast<float>(extent.width),
        .height = static_cast<float>(extent.height),
        .minDepth = 0.f,
        .maxDepth = 1.f,
    };
    VkRect2D scissor{.extent = extent};

    vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
    vkCmdBindDescriptorSets(command_buffer,
                            VK_PIPELINE_BIND_POINT_GRAPHICS,
                            pipeline_layout_,
                            0,
                            1,
                            &descriptor_sets_[ring],
                            0,
                            nullptr);
    vkCmdSetViewport(command_buffer, 0, 1, &viewport);
    vkCmdSetScissor(command_buffer, 0, 1, &scissor);

    const std::size_t draw_count =
        std::min(snapshot.opaque_sections.size(), Renderer::kMaxIndirectDraws);
    for (std::size_t draw_index = 0; draw_index < draw_count; ++draw_index) {
        const DrawSection& section = snapshot.opaque_sections[draw_index];
        const GpuMeshSlot* mesh_slot = mesh_pool.slot(section.vertex_buffer_id);
        if (mesh_slot == nullptr || mesh_slot->vertex_buffer == VK_NULL_HANDLE ||
            mesh_slot->index_buffer == VK_NULL_HANDLE) {
            continue;
        }

        if (clamp_index_count(mesh_slot, section.index_count) == 0) {
            continue;
        }

        const VkDeviceSize offsets[] = {0};
        vkCmdBindVertexBuffers(command_buffer, 0, 1, &mesh_slot->vertex_buffer, offsets);
        vkCmdBindIndexBuffer(command_buffer, mesh_slot->index_buffer, 0, VK_INDEX_TYPE_UINT32);

        const DrawPushConstants push{
            .model_translation = section.model_translation,
        };
        vkCmdPushConstants(command_buffer,
                           pipeline_layout_,
                           VK_SHADER_STAGE_VERTEX_BIT,
                           0,
                           sizeof(DrawPushConstants),
                           &push);

        const VkDeviceSize indirect_byte_offset =
            static_cast<VkDeviceSize>(indirect_offset + draw_index * sizeof(VkDrawIndexedIndirectCommand));
        vkCmdDrawIndexedIndirect(
            command_buffer, slot.indirect_draw_buffer, indirect_byte_offset, 1, sizeof(VkDrawIndexedIndirectCommand));
    }
}

} // namespace engine
