#include "engine/render/WaterPass.hpp"

#include "engine/render/HostMemory.hpp"
#include "engine/render/Renderer.hpp"
#include "engine/render/VkCheck.hpp"
#include "engine/world/SectionIndexing.hpp"

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

} // namespace

WaterPass::~WaterPass() {
    shutdown();
}

bool WaterPass::load_shader_module(const std::filesystem::path& path, VkShaderModule& out_module) {
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

bool WaterPass::create_pipeline(VkRenderPass render_pass,
                                const std::filesystem::path& shader_dir,
                                VkDescriptorSetLayout frame_descriptor_layout) {
    VkShaderModule vert_module = VK_NULL_HANDLE;
    VkShaderModule frag_module = VK_NULL_HANDLE;
    if (!load_shader_module(shader_dir / "water.vert.spv", vert_module) ||
        !load_shader_module(shader_dir / "water.frag.spv", frag_module)) {
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
        .depthWriteEnable = VK_FALSE,
        .depthCompareOp = VK_COMPARE_OP_LESS,
    };

    const VkPipelineColorBlendAttachmentState blend_attachment{
        .blendEnable = VK_TRUE,
        .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
        .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .colorBlendOp = VK_BLEND_OP_ADD,
        .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
        .alphaBlendOp = VK_BLEND_OP_ADD,
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
        .setLayoutCount = frame_descriptor_layout != VK_NULL_HANDLE ? 1u : 0u,
        .pSetLayouts = frame_descriptor_layout != VK_NULL_HANDLE ? &frame_descriptor_layout : nullptr,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &push_constant,
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

bool WaterPass::init(VulkanContext& context,
                     VkRenderPass render_pass,
                     const std::filesystem::path& shader_dir,
                     PerFrameGpuWriteRing& per_frame_writes,
                     VkDescriptorSetLayout frame_descriptor_layout) {
    context_ = &context;
    per_frame_writes_ = &per_frame_writes;

    if (!create_pipeline(render_pass, shader_dir, frame_descriptor_layout)) {
        shutdown();
        return false;
    }

    return true;
}

void WaterPass::shutdown() {
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

    for (VkShaderModule module : shader_modules_) {
        vkDestroyShaderModule(context_->device(), module, nullptr);
    }
    shader_modules_.clear();

    context_ = nullptr;
    per_frame_writes_ = nullptr;
}

void WaterPass::write_indirect_commands(const std::uint64_t frame_index,
                                        const WorldRenderSnapshot& snapshot,
                                        const GpuMeshPool& mesh_pool,
                                        const std::size_t opaque_draw_count) {
    if (per_frame_writes_ == nullptr || snapshot.water_sections.empty()) {
        return;
    }

    PerFrameGpuWrites& slot = per_frame_writes_->slot(frame_index);
    std::vector<VkDrawIndexedIndirectCommand> commands;
    commands.reserve(snapshot.water_sections.size());

    const std::size_t max_opaque_draws = std::min(opaque_draw_count, Renderer::kMaxIndirectDraws);
    const std::size_t max_water_draws = Renderer::kMaxWaterIndirectDraws;
    const std::size_t water_draw_count = std::min(snapshot.water_sections.size(), max_water_draws);
    commands.reserve(water_draw_count);
    for (std::size_t i = 0; i < water_draw_count; ++i) {
        const DrawSection& section = snapshot.water_sections[i];
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

    const std::size_t indirect_base = per_frame_writes_->aligned_dynamic_offset(slot.ubo_size);
    const std::size_t water_indirect_offset =
        indirect_base + max_opaque_draws * sizeof(VkDrawIndexedIndirectCommand);
    const std::size_t write_bytes = commands.size() * sizeof(VkDrawIndexedIndirectCommand);
    const std::size_t indirect_limit =
        indirect_base + static_cast<std::size_t>(slot.indirect_size);
    if (water_indirect_offset + write_bytes > indirect_limit) {
        return;
    }

    void* mapped = nullptr;
    VK_CHECK(vkMapMemory(context_->device(), slot.memory, 0, slot.memory_size, 0, &mapped));
    host_write(context_->device(),
               mapped,
               water_indirect_offset,
               write_bytes,
               commands.data(),
               slot.memory,
               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
               context_->query_gpu_caps());
    vkUnmapMemory(context_->device(), slot.memory);
}

void WaterPass::record(VkCommandBuffer command_buffer,
                       const std::uint64_t frame_index,
                       const WorldRenderSnapshot& snapshot,
                       const GpuMeshPool& mesh_pool,
                       const VkDescriptorSet frame_descriptor_set,
                       const VkExtent2D extent,
                       const std::size_t opaque_draw_count) {
    if (pipeline_ == VK_NULL_HANDLE || snapshot.water_sections.empty()) {
        return;
    }

    const PerFrameGpuWrites& slot = per_frame_writes_->slot(frame_index);
    const std::size_t indirect_base = per_frame_writes_->aligned_dynamic_offset(slot.ubo_size);
    const std::size_t max_opaque_draws = std::min(opaque_draw_count, Renderer::kMaxIndirectDraws);
    const std::size_t water_indirect_offset =
        indirect_base + max_opaque_draws * sizeof(VkDrawIndexedIndirectCommand);

    VkViewport viewport{
        .width = static_cast<float>(extent.width),
        .height = static_cast<float>(extent.height),
        .minDepth = 0.f,
        .maxDepth = 1.f,
    };
    VkRect2D scissor{.extent = extent};

    vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
    if (frame_descriptor_set != VK_NULL_HANDLE) {
        vkCmdBindDescriptorSets(command_buffer,
                                VK_PIPELINE_BIND_POINT_GRAPHICS,
                                pipeline_layout_,
                                0,
                                1,
                                &frame_descriptor_set,
                                0,
                                nullptr);
    }
    vkCmdSetViewport(command_buffer, 0, 1, &viewport);
    vkCmdSetScissor(command_buffer, 0, 1, &scissor);

    const std::size_t max_water_draws = Renderer::kMaxWaterIndirectDraws;
    const std::size_t draw_count = std::min(snapshot.water_sections.size(), max_water_draws);
    const std::size_t indirect_limit =
        indirect_base + static_cast<std::size_t>(slot.indirect_size);
    for (std::size_t draw_index = 0; draw_index < draw_count; ++draw_index) {
        const DrawSection& section = snapshot.water_sections[draw_index];
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

        const VkDeviceSize indirect_byte_offset = static_cast<VkDeviceSize>(
            water_indirect_offset + draw_index * sizeof(VkDrawIndexedIndirectCommand));
        if (indirect_byte_offset + sizeof(VkDrawIndexedIndirectCommand) > indirect_limit) {
            break;
        }
        vkCmdDrawIndexedIndirect(
            command_buffer, slot.indirect_draw_buffer, indirect_byte_offset, 1, sizeof(VkDrawIndexedIndirectCommand));
    }
}

} // namespace engine
