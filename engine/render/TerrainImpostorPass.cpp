#include "engine/render/TerrainImpostorPass.hpp"

#include "engine/render/HostMemory.hpp"
#include "engine/render/VkCheck.hpp"

#include <array>
#include <fstream>
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

[[nodiscard]] bool create_device_local_buffer(VkDevice device,
                                              VkPhysicalDevice physical_device,
                                              VkDeviceSize size,
                                              VkBufferUsageFlags usage,
                                              VkBuffer& out_buffer,
                                              VkDeviceMemory& out_memory) {
    const VkBufferCreateInfo buffer_info{
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size,
        .usage = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    VK_CHECK(vkCreateBuffer(device, &buffer_info, nullptr, &out_buffer));

    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(device, out_buffer, &requirements);

    const std::uint32_t memory_type = find_memory_type(
        physical_device,
        requirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (memory_type == UINT32_MAX) {
        return false;
    }

    const VkMemoryAllocateInfo alloc_info{
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = requirements.size,
        .memoryTypeIndex = memory_type,
    };
    VK_CHECK(vkAllocateMemory(device, &alloc_info, nullptr, &out_memory));
    VK_CHECK(vkBindBufferMemory(device, out_buffer, out_memory, 0));
    return true;
}

} // namespace

TerrainImpostorPass::~TerrainImpostorPass() {
    shutdown();
}

bool TerrainImpostorPass::load_shader_module(const std::filesystem::path& path, VkShaderModule& out_module) {
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

bool TerrainImpostorPass::create_static_geometry() {
    static constexpr glm::vec3 kUnitCubeVerts[] = {
        {0.f, 0.f, 0.f},
        {1.f, 0.f, 0.f},
        {1.f, 1.f, 0.f},
        {0.f, 1.f, 0.f},
        {0.f, 0.f, 1.f},
        {1.f, 0.f, 1.f},
        {1.f, 1.f, 1.f},
        {0.f, 1.f, 1.f},
    };
    static constexpr std::uint16_t kUnitCubeIndices[] = {
        0, 1, 2, 2, 3, 0, // -Z
        4, 5, 6, 6, 7, 4, // +Z
        0, 4, 7, 7, 3, 0, // -X
        1, 5, 6, 6, 2, 1, // +X
        0, 1, 5, 5, 4, 0, // -Y
        3, 2, 6, 6, 7, 3, // +Y
    };

    index_count_ = static_cast<std::uint32_t>(sizeof(kUnitCubeIndices) / sizeof(kUnitCubeIndices[0]));

    if (!create_device_local_buffer(context_->device(),
                                    context_->physical_device(),
                                    sizeof(kUnitCubeVerts),
                                    VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                    vertex_buffer_,
                                    vertex_memory_)) {
        return false;
    }

    if (!create_device_local_buffer(context_->device(),
                                    context_->physical_device(),
                                    sizeof(kUnitCubeIndices),
                                    VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                                    index_buffer_,
                                    index_memory_)) {
        return false;
    }

    const GpuCaps caps = context_->query_gpu_caps();
    void* mapped = nullptr;
    VK_CHECK(vkMapMemory(context_->device(), vertex_memory_, 0, sizeof(kUnitCubeVerts), 0, &mapped));
    host_write(context_->device(),
               mapped,
               0,
               sizeof(kUnitCubeVerts),
               kUnitCubeVerts,
               vertex_memory_,
               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
               caps);
    vkUnmapMemory(context_->device(), vertex_memory_);

    VK_CHECK(vkMapMemory(context_->device(), index_memory_, 0, sizeof(kUnitCubeIndices), 0, &mapped));
    host_write(context_->device(),
               mapped,
               0,
               sizeof(kUnitCubeIndices),
               kUnitCubeIndices,
               index_memory_,
               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
               caps);
    vkUnmapMemory(context_->device(), index_memory_);

    return true;
}

bool TerrainImpostorPass::create_frame_uniform_buffers() {
    for (std::uint32_t i = 0; i < kFramesInFlight; ++i) {
        const VkBufferCreateInfo buffer_info{
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size = sizeof(FrameUniformGpu),
            .usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        };
        VK_CHECK(vkCreateBuffer(context_->device(), &buffer_info, nullptr, &frame_ubos_[i]));

        VkMemoryRequirements requirements{};
        vkGetBufferMemoryRequirements(context_->device(), frame_ubos_[i], &requirements);

        const std::uint32_t memory_type = find_memory_type(
            context_->physical_device(),
            requirements.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if (memory_type == UINT32_MAX) {
            return false;
        }

        const VkMemoryAllocateInfo alloc_info{
            .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .allocationSize = requirements.size,
            .memoryTypeIndex = memory_type,
        };
        VK_CHECK(vkAllocateMemory(context_->device(), &alloc_info, nullptr, &frame_ubo_memories_[i]));
        VK_CHECK(vkBindBufferMemory(context_->device(), frame_ubos_[i], frame_ubo_memories_[i], 0));
    }
    return true;
}

bool TerrainImpostorPass::create_descriptor_layout() {
    const VkDescriptorSetLayoutBinding binding{
        .binding = 0,
        .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        .descriptorCount = 1,
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
    };

    const VkDescriptorSetLayoutCreateInfo layout_info{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 1,
        .pBindings = &binding,
    };
    VK_CHECK(vkCreateDescriptorSetLayout(context_->device(), &layout_info, nullptr, &descriptor_layout_));

    const VkDescriptorPoolSize pool_size{
        .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        .descriptorCount = kFramesInFlight,
    };
    const VkDescriptorPoolCreateInfo pool_info{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = kFramesInFlight,
        .poolSizeCount = 1,
        .pPoolSizes = &pool_size,
    };
    VK_CHECK(vkCreateDescriptorPool(context_->device(), &pool_info, nullptr, &descriptor_pool_));

    descriptor_sets_.resize(kFramesInFlight);
    std::vector<VkDescriptorSetLayout> layouts(kFramesInFlight, descriptor_layout_);
    const VkDescriptorSetAllocateInfo alloc_info{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = descriptor_pool_,
        .descriptorSetCount = kFramesInFlight,
        .pSetLayouts = layouts.data(),
    };
    VK_CHECK(vkAllocateDescriptorSets(context_->device(), &alloc_info, descriptor_sets_.data()));

    for (std::uint32_t i = 0; i < kFramesInFlight; ++i) {
        const VkDescriptorBufferInfo buffer_info{
            .buffer = frame_ubos_[i],
            .offset = 0,
            .range = sizeof(FrameUniformGpu),
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

bool TerrainImpostorPass::create_pipeline(VkRenderPass render_pass, const std::filesystem::path& shader_dir) {
    VkShaderModule vert_module = VK_NULL_HANDLE;
    VkShaderModule frag_module = VK_NULL_HANDLE;
    if (!load_shader_module(shader_dir / "impostor.vert.spv", vert_module) ||
        !load_shader_module(shader_dir / "impostor.frag.spv", frag_module)) {
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
        .stride = sizeof(glm::vec3),
        .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
    };
    const VkVertexInputAttributeDescription attribute{
        .location = 0,
        .binding = 0,
        .format = VK_FORMAT_R32G32B32_SFLOAT,
        .offset = 0,
    };
    const VkPipelineVertexInputStateCreateInfo vertex_input{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = 1,
        .pVertexBindingDescriptions = &binding,
        .vertexAttributeDescriptionCount = 1,
        .pVertexAttributeDescriptions = &attribute,
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

    const VkPushConstantRange push_range{
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        .offset = 0,
        .size = sizeof(DrawPushConstants),
    };

    const VkPipelineLayoutCreateInfo layout_info{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts = &descriptor_layout_,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &push_range,
    };
    VK_CHECK(vkCreatePipelineLayout(context_->device(), &layout_info, nullptr, &pipeline_layout_));

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
    VK_CHECK(vkCreateGraphicsPipelines(
        context_->device(), VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &pipeline_));

    return true;
}

bool TerrainImpostorPass::init(VulkanContext& context,
                               VkRenderPass render_pass,
                               const std::filesystem::path& shader_dir) {
    context_ = &context;
    if (!create_frame_uniform_buffers() || !create_static_geometry() || !create_descriptor_layout() ||
        !create_pipeline(render_pass, shader_dir)) {
        shutdown();
        return false;
    }
    return true;
}

void TerrainImpostorPass::shutdown() {
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

    if (vertex_buffer_ != VK_NULL_HANDLE) {
        vkDestroyBuffer(context_->device(), vertex_buffer_, nullptr);
        vertex_buffer_ = VK_NULL_HANDLE;
    }
    if (vertex_memory_ != VK_NULL_HANDLE) {
        vkFreeMemory(context_->device(), vertex_memory_, nullptr);
        vertex_memory_ = VK_NULL_HANDLE;
    }
    if (index_buffer_ != VK_NULL_HANDLE) {
        vkDestroyBuffer(context_->device(), index_buffer_, nullptr);
        index_buffer_ = VK_NULL_HANDLE;
    }
    if (index_memory_ != VK_NULL_HANDLE) {
        vkFreeMemory(context_->device(), index_memory_, nullptr);
        index_memory_ = VK_NULL_HANDLE;
    }

    for (std::uint32_t i = 0; i < kFramesInFlight; ++i) {
        if (frame_ubos_[i] != VK_NULL_HANDLE) {
            vkDestroyBuffer(context_->device(), frame_ubos_[i], nullptr);
            frame_ubos_[i] = VK_NULL_HANDLE;
        }
        if (frame_ubo_memories_[i] != VK_NULL_HANDLE) {
            vkFreeMemory(context_->device(), frame_ubo_memories_[i], nullptr);
            frame_ubo_memories_[i] = VK_NULL_HANDLE;
        }
    }

    for (VkShaderModule module : shader_modules_) {
        vkDestroyShaderModule(context_->device(), module, nullptr);
    }
    shader_modules_.clear();
    descriptor_sets_.clear();
    context_ = nullptr;
}

void TerrainImpostorPass::write_frame_uniforms(const std::uint64_t frame_index,
                                               const FrameUniformGpu& uniforms) {
    if (context_ == nullptr) {
        return;
    }

    const std::uint32_t ring_index = static_cast<std::uint32_t>(frame_index % kFramesInFlight);
    void* mapped = nullptr;
    VK_CHECK(vkMapMemory(
        context_->device(), frame_ubo_memories_[ring_index], 0, sizeof(FrameUniformGpu), 0, &mapped));
    host_write(context_->device(),
               mapped,
               0,
               sizeof(FrameUniformGpu),
               &uniforms,
               frame_ubo_memories_[ring_index],
               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
               context_->query_gpu_caps());
    vkUnmapMemory(context_->device(), frame_ubo_memories_[ring_index]);
}

void TerrainImpostorPass::record(VkCommandBuffer command_buffer,
                                 const std::uint64_t frame_index,
                                 const WorldRenderSnapshot& snapshot,
                                 const VkExtent2D extent) {
    if (pipeline_ == VK_NULL_HANDLE || snapshot.impostors.empty()) {
        return;
    }

    const std::uint32_t ring_index = static_cast<std::uint32_t>(frame_index % kFramesInFlight);

    VkViewport viewport{
        .width = static_cast<float>(extent.width),
        .height = static_cast<float>(extent.height),
        .minDepth = 0.f,
        .maxDepth = 1.f,
    };
    VkRect2D scissor{.extent = extent};

    vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
    vkCmdSetViewport(command_buffer, 0, 1, &viewport);
    vkCmdSetScissor(command_buffer, 0, 1, &scissor);
    vkCmdBindDescriptorSets(command_buffer,
                            VK_PIPELINE_BIND_POINT_GRAPHICS,
                            pipeline_layout_,
                            0,
                            1,
                            &descriptor_sets_[ring_index],
                            0,
                            nullptr);

    const VkDeviceSize vertex_offset = 0;
    vkCmdBindVertexBuffers(command_buffer, 0, 1, &vertex_buffer_, &vertex_offset);
    vkCmdBindIndexBuffer(command_buffer, index_buffer_, 0, VK_INDEX_TYPE_UINT16);

    for (const DrawImpostor& impostor : snapshot.impostors) {
        const DrawPushConstants push{
            .model_translation = impostor.model_translation,
            .min_y = impostor.min_y,
            .max_y = impostor.max_y,
            .color = impostor.color,
        };
        vkCmdPushConstants(command_buffer,
                           pipeline_layout_,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0,
                           sizeof(DrawPushConstants),
                           &push);
        vkCmdDrawIndexed(command_buffer, index_count_, 1, 0, 0, 0);
    }
}

} // namespace engine