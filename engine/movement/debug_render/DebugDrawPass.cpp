#include "engine/movement/debug_render/DebugDrawPass.hpp"

#include "engine/render/VkCheck.hpp"

#include <array>
#include <cmath>
#include <cstring>
#include <fstream>
#include <vector>

#ifndef ENGINE_SHADER_DIR
#define ENGINE_SHADER_DIR "."
#endif

#include <filesystem>

namespace engine::movement {

namespace {

constexpr float kPi = 3.14159265358979323846f;

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

std::uint32_t find_memory_type(VkPhysicalDevice physical_device,
                               std::uint32_t type_filter,
                               VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties mem_props{};
    vkGetPhysicalDeviceMemoryProperties(physical_device, &mem_props);
    for (std::uint32_t i = 0; i < mem_props.memoryTypeCount; ++i) {
        if ((type_filter & (1u << i)) != 0 &&
            (mem_props.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    return UINT32_MAX;
}

} // namespace

std::uint32_t DebugDrawPass::rgba(std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a) {
    return (static_cast<std::uint32_t>(r)) | (static_cast<std::uint32_t>(g) << 8) |
           (static_cast<std::uint32_t>(b) << 16) | (static_cast<std::uint32_t>(a) << 24);
}

void DebugDrawPass::push_line(const glm::vec3& a, const glm::vec3& b, std::uint32_t color) {
    if (vertices_.size() + 2 > kMaxVertices) {
        return;
    }
    vertices_.push_back(LineVertex{a, color});
    vertices_.push_back(LineVertex{b, color});
}

void DebugDrawPass::push_box(const glm::vec3& center,
                             const glm::vec3& half_extents,
                             std::uint32_t color) {
    const glm::vec3 mn = center - half_extents;
    const glm::vec3 mx = center + half_extents;
    const std::array<glm::vec3, 8> c = {
        glm::vec3(mn.x, mn.y, mn.z), glm::vec3(mx.x, mn.y, mn.z),
        glm::vec3(mx.x, mn.y, mx.z), glm::vec3(mn.x, mn.y, mx.z),
        glm::vec3(mn.x, mx.y, mn.z), glm::vec3(mx.x, mx.y, mn.z),
        glm::vec3(mx.x, mx.y, mx.z), glm::vec3(mn.x, mx.y, mx.z),
    };
    // bottom ring, top ring, vertical edges
    const std::array<std::pair<int, int>, 12> edges = {{{0, 1}, {1, 2}, {2, 3}, {3, 0},
                                                        {4, 5}, {5, 6}, {6, 7}, {7, 4},
                                                        {0, 4}, {1, 5}, {2, 6}, {3, 7}}};
    for (const auto& [i, j] : edges) {
        push_line(c[i], c[j], color);
    }
}

void DebugDrawPass::push_circle_xz(const glm::vec3& center,
                                   float radius,
                                   int segments,
                                   std::uint32_t color) {
    glm::vec3 prev = center + glm::vec3(radius, 0.f, 0.f);
    for (int i = 1; i <= segments; ++i) {
        const float t = (static_cast<float>(i) / static_cast<float>(segments)) * 2.f * kPi;
        const glm::vec3 cur = center + glm::vec3(std::cos(t) * radius, 0.f, std::sin(t) * radius);
        push_line(prev, cur, color);
        prev = cur;
    }
}

void DebugDrawPass::push_vertical_arc(const glm::vec3& center,
                                      float radius,
                                      const glm::vec3& plane_dir,
                                      bool upper,
                                      std::uint32_t color) {
    constexpr int kArcSegments = 8;
    glm::vec3 prev = center + plane_dir * radius;
    for (int i = 1; i <= kArcSegments; ++i) {
        const float t = (static_cast<float>(i) / static_cast<float>(kArcSegments)) * (kPi * 0.5f);
        const float horizontal = std::cos(t) * radius;
        const float vertical = std::sin(t) * radius * (upper ? 1.f : -1.f);
        const glm::vec3 cur = center + plane_dir * horizontal + glm::vec3(0.f, vertical, 0.f);
        push_line(prev, cur, color);
        prev = cur;
    }
}

void DebugDrawPass::push_capsule(const glm::vec3& center,
                                 float radius,
                                 float half_height,
                                 std::uint32_t color) {
    const glm::vec3 top_ring = center + glm::vec3(0.f, half_height, 0.f);
    const glm::vec3 bottom_ring = center - glm::vec3(0.f, half_height, 0.f);

    push_circle_xz(top_ring, radius, 16, color);
    push_circle_xz(bottom_ring, radius, 16, color);

    // Vertical connectors at four cardinal angles.
    const std::array<glm::vec3, 4> dirs = {glm::vec3(1, 0, 0), glm::vec3(-1, 0, 0),
                                           glm::vec3(0, 0, 1), glm::vec3(0, 0, -1)};
    for (const glm::vec3& d : dirs) {
        push_line(bottom_ring + d * radius, top_ring + d * radius, color);
    }

    // Hemispherical caps: two arcs each (along X and Z).
    push_vertical_arc(top_ring, radius, glm::vec3(1, 0, 0), true, color);
    push_vertical_arc(top_ring, radius, glm::vec3(-1, 0, 0), true, color);
    push_vertical_arc(top_ring, radius, glm::vec3(0, 0, 1), true, color);
    push_vertical_arc(top_ring, radius, glm::vec3(0, 0, -1), true, color);
    push_vertical_arc(bottom_ring, radius, glm::vec3(1, 0, 0), false, color);
    push_vertical_arc(bottom_ring, radius, glm::vec3(-1, 0, 0), false, color);
    push_vertical_arc(bottom_ring, radius, glm::vec3(0, 0, 1), false, color);
    push_vertical_arc(bottom_ring, radius, glm::vec3(0, 0, -1), false, color);
}

void DebugDrawPass::push_arrow(const glm::vec3& origin,
                               const glm::vec3& dir,
                               float length,
                               std::uint32_t color) {
    const float len = glm::length(dir);
    if (len < 1e-5f) {
        return;
    }
    const glm::vec3 unit = dir / len;
    const glm::vec3 tip = origin + unit * length;
    push_line(origin, tip, color);

    // Arrowhead: two short back-pointing segments in a stable plane.
    glm::vec3 ref(0.f, 1.f, 0.f);
    if (std::abs(glm::dot(unit, ref)) > 0.95f) {
        ref = glm::vec3(1.f, 0.f, 0.f);
    }
    const glm::vec3 side = glm::normalize(glm::cross(unit, ref));
    const float head = length * 0.2f;
    push_line(tip, tip - unit * head + side * head * 0.5f, color);
    push_line(tip, tip - unit * head - side * head * 0.5f, color);
}

void DebugDrawPass::push_cross(const glm::vec3& center, float size, std::uint32_t color) {
    const float h = size * 0.5f;
    push_line(center - glm::vec3(h, 0.f, 0.f), center + glm::vec3(h, 0.f, 0.f), color);
    push_line(center - glm::vec3(0.f, h, 0.f), center + glm::vec3(0.f, h, 0.f), color);
    push_line(center - glm::vec3(0.f, 0.f, h), center + glm::vec3(0.f, 0.f, h), color);
}

bool DebugDrawPass::create_vertex_buffers() {
    vertex_buffers_.resize(frames_in_flight_);
    const VkDeviceSize buffer_size = sizeof(LineVertex) * kMaxVertices;

    for (FrameBuffer& fb : vertex_buffers_) {
        const VkBufferCreateInfo buffer_info{
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size = buffer_size,
            .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        };
        VK_CHECK(vkCreateBuffer(context_->device(), &buffer_info, nullptr, &fb.buffer));

        VkMemoryRequirements requirements{};
        vkGetBufferMemoryRequirements(context_->device(), fb.buffer, &requirements);

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
        VK_CHECK(vkAllocateMemory(context_->device(), &alloc_info, nullptr, &fb.memory));
        VK_CHECK(vkBindBufferMemory(context_->device(), fb.buffer, fb.memory, 0));
        VK_CHECK(vkMapMemory(context_->device(), fb.memory, 0, buffer_size, 0, &fb.mapped));
    }
    return true;
}

void DebugDrawPass::destroy_vertex_buffers() {
    if (context_ == nullptr) {
        return;
    }
    for (FrameBuffer& fb : vertex_buffers_) {
        if (fb.mapped != nullptr) {
            vkUnmapMemory(context_->device(), fb.memory);
            fb.mapped = nullptr;
        }
        if (fb.buffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(context_->device(), fb.buffer, nullptr);
            fb.buffer = VK_NULL_HANDLE;
        }
        if (fb.memory != VK_NULL_HANDLE) {
            vkFreeMemory(context_->device(), fb.memory, nullptr);
            fb.memory = VK_NULL_HANDLE;
        }
    }
    vertex_buffers_.clear();
}

bool DebugDrawPass::create_pipelines(VkRenderPass render_pass) {
    const std::filesystem::path shader_dir(ENGINE_SHADER_DIR);

    const std::vector<std::uint32_t> vert_code = load_spirv(shader_dir / "debug_lines.vert.spv");
    const std::vector<std::uint32_t> frag_code = load_spirv(shader_dir / "debug_lines.frag.spv");
    if (vert_code.empty() || frag_code.empty()) {
        SPDLOG_ERROR("DebugDrawPass: failed to load debug_lines SPIR-V");
        return false;
    }

    auto make_module = [&](const std::vector<std::uint32_t>& code) {
        const VkShaderModuleCreateInfo info{
            .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
            .codeSize = code.size() * sizeof(std::uint32_t),
            .pCode = code.data(),
        };
        VkShaderModule module = VK_NULL_HANDLE;
        VK_CHECK(vkCreateShaderModule(context_->device(), &info, nullptr, &module));
        shader_modules_.push_back(module);
        return module;
    };

    const VkShaderModule vert_module = make_module(vert_code);
    const VkShaderModule frag_module = make_module(frag_code);

    const VkPipelineShaderStageCreateInfo stages[] = {
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
         .stage = VK_SHADER_STAGE_VERTEX_BIT,
         .module = vert_module,
         .pName = "main"},
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
         .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
         .module = frag_module,
         .pName = "main"},
    };

    const VkVertexInputBindingDescription binding{
        .binding = 0,
        .stride = sizeof(LineVertex),
        .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
    };
    const VkVertexInputAttributeDescription attributes[] = {
        {.location = 0, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = offsetof(LineVertex, pos)},
        {.location = 1, .binding = 0, .format = VK_FORMAT_R32_UINT, .offset = offsetof(LineVertex, color)},
    };
    const VkPipelineVertexInputStateCreateInfo vertex_input{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = 1,
        .pVertexBindingDescriptions = &binding,
        .vertexAttributeDescriptionCount = 2,
        .pVertexAttributeDescriptions = attributes,
    };

    const VkPipelineInputAssemblyStateCreateInfo input_assembly{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST,
    };

    const VkPipelineViewportStateCreateInfo viewport_state{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .scissorCount = 1,
    };

    const VkPipelineRasterizationStateCreateInfo rasterization{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode = VK_CULL_MODE_NONE,
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .lineWidth = 1.f,
    };

    const VkPipelineMultisampleStateCreateInfo multisample{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
    };

    const VkPipelineColorBlendAttachmentState blend_attachment{
        .blendEnable = VK_TRUE,
        .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
        .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .colorBlendOp = VK_BLEND_OP_ADD,
        .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
        .alphaBlendOp = VK_BLEND_OP_ADD,
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
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
        .size = sizeof(glm::mat4),
    };
    const VkPipelineLayoutCreateInfo layout_info{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &push_constant,
    };
    VK_CHECK(vkCreatePipelineLayout(context_->device(), &layout_info, nullptr, &pipeline_layout_));

    auto build_pipeline = [&](bool depth_test, VkPipeline& out) {
        const VkPipelineDepthStencilStateCreateInfo depth_stencil{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
            .depthTestEnable = depth_test ? VK_TRUE : VK_FALSE,
            .depthWriteEnable = VK_FALSE,
            .depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL,
        };
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
            context_->device(), VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &out));
    };

    build_pipeline(true, pipeline_depth_);
    build_pipeline(false, pipeline_no_depth_);
    return true;
}

void DebugDrawPass::destroy_pipelines() {
    if (context_ == nullptr) {
        return;
    }
    if (pipeline_depth_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(context_->device(), pipeline_depth_, nullptr);
        pipeline_depth_ = VK_NULL_HANDLE;
    }
    if (pipeline_no_depth_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(context_->device(), pipeline_no_depth_, nullptr);
        pipeline_no_depth_ = VK_NULL_HANDLE;
    }
    if (pipeline_layout_ != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(context_->device(), pipeline_layout_, nullptr);
        pipeline_layout_ = VK_NULL_HANDLE;
    }
    for (VkShaderModule module : shader_modules_) {
        vkDestroyShaderModule(context_->device(), module, nullptr);
    }
    shader_modules_.clear();
}

void DebugDrawPass::on_renderer_init(const PassExtensionInitContext& ctx) {
    context_ = ctx.context;
    frames_in_flight_ = ctx.frames_in_flight > 0 ? ctx.frames_in_flight : 2;
    if (!create_pipelines(ctx.render_pass)) {
        SPDLOG_ERROR("DebugDrawPass: pipeline creation failed");
        return;
    }
    if (!create_vertex_buffers()) {
        SPDLOG_ERROR("DebugDrawPass: vertex buffer creation failed");
    }
}

void DebugDrawPass::on_renderer_shutdown(VulkanContext& context) {
    context_ = &context;
    destroy_vertex_buffers();
    destroy_pipelines();
    context_ = nullptr;
}

void DebugDrawPass::on_swapchain_recreate(const PassExtensionInitContext& ctx) {
    // Pipelines bind to the render pass; rebuild them in case it changed.
    destroy_pipelines();
    context_ = ctx.context;
    create_pipelines(ctx.render_pass);
}

void DebugDrawPass::record(const PassExtensionRecordContext& ctx) {
    if (vertices_.empty() || pipeline_layout_ == VK_NULL_HANDLE) {
        return;
    }
    if (ctx.frame_slot >= vertex_buffers_.size()) {
        return;
    }

    FrameBuffer& fb = vertex_buffers_[ctx.frame_slot];
    if (fb.mapped == nullptr) {
        return;
    }

    const std::size_t count = std::min<std::size_t>(vertices_.size(), kMaxVertices);
    std::memcpy(fb.mapped, vertices_.data(), count * sizeof(LineVertex));

    VkViewport viewport{
        .width = static_cast<float>(ctx.extent.width),
        .height = static_cast<float>(ctx.extent.height),
        .minDepth = 0.f,
        .maxDepth = 1.f,
    };
    VkRect2D scissor{.extent = ctx.extent};

    const VkPipeline pipeline = depth_test_ ? pipeline_depth_ : pipeline_no_depth_;
    vkCmdBindPipeline(ctx.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    vkCmdSetViewport(ctx.command_buffer, 0, 1, &viewport);
    vkCmdSetScissor(ctx.command_buffer, 0, 1, &scissor);

    const glm::mat4 view_proj = ctx.proj * ctx.view;
    vkCmdPushConstants(ctx.command_buffer, pipeline_layout_, VK_SHADER_STAGE_VERTEX_BIT, 0,
                       sizeof(glm::mat4), &view_proj);

    const VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(ctx.command_buffer, 0, 1, &fb.buffer, &offset);
    vkCmdDraw(ctx.command_buffer, static_cast<std::uint32_t>(count), 1, 0, 0);
}

} // namespace engine::movement
