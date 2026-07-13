#include <cubey/vulkan/pipeline.h>

#include <cubey/vulkan/vk_check.h>

#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace cubey::vulkan {
namespace {

void validate_dynamic_graphics_pipeline_config(const DynamicGraphicsPipelineConfig& config) {
    if (config.layout == VK_NULL_HANDLE) {
        throw std::runtime_error("dynamic graphics pipeline requires a pipeline layout");
    }
    if (config.color_format == VK_FORMAT_UNDEFINED && config.color_formats.empty() &&
        config.depth_format == VK_FORMAT_UNDEFINED) {
        throw std::runtime_error(
            "dynamic graphics pipeline requires at least one attachment format");
    }
    for (const VkFormat color_format : config.color_formats) {
        if (color_format == VK_FORMAT_UNDEFINED) {
            throw std::runtime_error("dynamic graphics pipeline color formats must be defined");
        }
    }
    if (config.shader_stages.empty()) {
        throw std::runtime_error("dynamic graphics pipeline requires at least one shader stage");
    }
    const bool patch_pipeline = config.topology == VK_PRIMITIVE_TOPOLOGY_PATCH_LIST;
    const auto has_stage = [&config](VkShaderStageFlagBits stage) {
        return std::any_of(config.shader_stages.begin(), config.shader_stages.end(),
                           [stage](const VkPipelineShaderStageCreateInfo& candidate) {
                               return candidate.stage == stage;
                           });
    };
    const bool has_control = has_stage(VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT);
    const bool has_evaluation = has_stage(VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT);
    if (patch_pipeline && config.patch_control_points == 0) {
        throw std::runtime_error("patch graphics pipeline requires control points");
    }
    if (patch_pipeline && (!has_control || !has_evaluation)) {
        throw std::runtime_error(
            "patch graphics pipeline requires tessellation control and evaluation stages");
    }
    if (!patch_pipeline && config.patch_control_points != 0) {
        throw std::runtime_error("non-patch graphics pipeline cannot declare control points");
    }
    if (!patch_pipeline && (has_control || has_evaluation)) {
        throw std::runtime_error("tessellation stages require patch-list topology");
    }
    if ((config.depth_test || config.depth_write) && config.depth_format == VK_FORMAT_UNDEFINED) {
        throw std::runtime_error(
            "dynamic graphics pipeline depth state requires a depth attachment format");
    }
}

void validate_compute_pipeline_config(const ComputePipelineConfig& config) {
    if (config.layout == VK_NULL_HANDLE) {
        throw std::runtime_error("compute pipeline requires a pipeline layout");
    }
    if (config.shader_stage.stage != VK_SHADER_STAGE_COMPUTE_BIT) {
        throw std::runtime_error("compute pipeline requires a compute shader stage");
    }
    if (config.shader_stage.module == VK_NULL_HANDLE) {
        throw std::runtime_error("compute pipeline requires a shader module");
    }
    if (config.shader_stage.pName == nullptr) {
        throw std::runtime_error("compute pipeline requires a shader entry point");
    }
}

std::vector<VkFormat> effective_color_formats(const DynamicGraphicsPipelineConfig& config) {
    if (!config.color_formats.empty()) {
        return config.color_formats;
    }
    if (config.color_format != VK_FORMAT_UNDEFINED) {
        return {config.color_format};
    }
    return {};
}

} // namespace

VkPipelineShaderStageCreateInfo shader_stage(VkShaderStageFlagBits stage, VkShaderModule module,
                                             const char* entry_point) {
    auto info = vk_struct<VkPipelineShaderStageCreateInfo>(
        VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO);
    info.stage = stage;
    info.module = module;
    info.pName = entry_point;
    return info;
}

PipelineLayoutInfo::PipelineLayoutInfo(const PipelineLayoutConfig& config) {
    set_layouts_.assign(config.set_layouts.begin(), config.set_layouts.end());
    push_constants_.assign(config.push_constants.begin(), config.push_constants.end());

    create_info_ =
        vk_struct<VkPipelineLayoutCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO);
    create_info_.setLayoutCount = static_cast<std::uint32_t>(set_layouts_.size());
    create_info_.pSetLayouts = set_layouts_.data();
    create_info_.pushConstantRangeCount = static_cast<std::uint32_t>(push_constants_.size());
    create_info_.pPushConstantRanges = push_constants_.data();
}

ComputePipelineInfo::ComputePipelineInfo(const ComputePipelineConfig& config) {
    validate_compute_pipeline_config(config);

    shader_stage_ = config.shader_stage;

    create_info_ =
        vk_struct<VkComputePipelineCreateInfo>(VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO);
    create_info_.stage = shader_stage_;
    create_info_.layout = config.layout;
}

DynamicGraphicsPipelineInfo::DynamicGraphicsPipelineInfo(
    const DynamicGraphicsPipelineConfig& config) {
    validate_dynamic_graphics_pipeline_config(config);

    shader_stages_.assign(config.shader_stages.begin(), config.shader_stages.end());
    vertex_bindings_.assign(config.vertex_bindings.begin(), config.vertex_bindings.end());
    vertex_attributes_.assign(config.vertex_attributes.begin(), config.vertex_attributes.end());
    color_formats_ = effective_color_formats(config);
    const bool has_color_attachment = !color_formats_.empty();

    rendering_info_ =
        vk_struct<VkPipelineRenderingCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO);
    rendering_info_.colorAttachmentCount = static_cast<std::uint32_t>(color_formats_.size());
    rendering_info_.pColorAttachmentFormats =
        has_color_attachment ? color_formats_.data() : nullptr;
    rendering_info_.depthAttachmentFormat = config.depth_format;

    vertex_input_ = vk_struct<VkPipelineVertexInputStateCreateInfo>(
        VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO);
    vertex_input_.vertexBindingDescriptionCount =
        static_cast<std::uint32_t>(vertex_bindings_.size());
    vertex_input_.pVertexBindingDescriptions = vertex_bindings_.data();
    vertex_input_.vertexAttributeDescriptionCount =
        static_cast<std::uint32_t>(vertex_attributes_.size());
    vertex_input_.pVertexAttributeDescriptions = vertex_attributes_.data();

    input_assembly_ = vk_struct<VkPipelineInputAssemblyStateCreateInfo>(
        VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO);
    input_assembly_.topology = config.topology;

    tessellation_ = vk_struct<VkPipelineTessellationStateCreateInfo>(
        VK_STRUCTURE_TYPE_PIPELINE_TESSELLATION_STATE_CREATE_INFO);
    tessellation_.patchControlPoints = config.patch_control_points;

    viewport_state_ = vk_struct<VkPipelineViewportStateCreateInfo>(
        VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO);
    viewport_state_.viewportCount = 1;
    viewport_state_.pViewports = nullptr;
    viewport_state_.scissorCount = 1;
    viewport_state_.pScissors = nullptr;

    dynamic_states_ = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR,
    };
    dynamic_state_ = vk_struct<VkPipelineDynamicStateCreateInfo>(
        VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO);
    dynamic_state_.dynamicStateCount = static_cast<std::uint32_t>(dynamic_states_.size());
    dynamic_state_.pDynamicStates = dynamic_states_.data();

    rasterizer_ = vk_struct<VkPipelineRasterizationStateCreateInfo>(
        VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO);
    rasterizer_.polygonMode = config.polygon_mode;
    rasterizer_.cullMode = config.cull_mode;
    rasterizer_.frontFace = config.front_face;
    rasterizer_.lineWidth = 1.0F;

    multisample_ = vk_struct<VkPipelineMultisampleStateCreateInfo>(
        VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO);
    multisample_.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    depth_stencil_ = vk_struct<VkPipelineDepthStencilStateCreateInfo>(
        VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO);
    depth_stencil_.depthTestEnable = config.depth_test ? VK_TRUE : VK_FALSE;
    depth_stencil_.depthWriteEnable = config.depth_write ? VK_TRUE : VK_FALSE;
    depth_stencil_.depthCompareOp = config.depth_compare_op;

    color_blend_attachments_.resize(color_formats_.size());
    for (VkPipelineColorBlendAttachmentState& color_blend_attachment :
         color_blend_attachments_) {
        color_blend_attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
                                                VK_COLOR_COMPONENT_G_BIT |
                                                VK_COLOR_COMPONENT_B_BIT |
                                                VK_COLOR_COMPONENT_A_BIT;
        color_blend_attachment.blendEnable = config.blend_enable ? VK_TRUE : VK_FALSE;
        color_blend_attachment.srcColorBlendFactor = config.src_color_blend_factor;
        color_blend_attachment.dstColorBlendFactor = config.dst_color_blend_factor;
        color_blend_attachment.colorBlendOp = config.color_blend_op;
        color_blend_attachment.srcAlphaBlendFactor = config.src_alpha_blend_factor;
        color_blend_attachment.dstAlphaBlendFactor = config.dst_alpha_blend_factor;
        color_blend_attachment.alphaBlendOp = config.alpha_blend_op;
    }

    color_blend_ = vk_struct<VkPipelineColorBlendStateCreateInfo>(
        VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO);
    color_blend_.attachmentCount = static_cast<std::uint32_t>(color_blend_attachments_.size());
    color_blend_.pAttachments =
        has_color_attachment ? color_blend_attachments_.data() : nullptr;

    create_info_ =
        vk_struct<VkGraphicsPipelineCreateInfo>(VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO);
    create_info_.pNext = &rendering_info_;
    create_info_.stageCount = static_cast<std::uint32_t>(shader_stages_.size());
    create_info_.pStages = shader_stages_.data();
    create_info_.pVertexInputState = &vertex_input_;
    create_info_.pInputAssemblyState = &input_assembly_;
    create_info_.pTessellationState =
        config.topology == VK_PRIMITIVE_TOPOLOGY_PATCH_LIST ? &tessellation_ : nullptr;
    create_info_.pViewportState = &viewport_state_;
    create_info_.pRasterizationState = &rasterizer_;
    create_info_.pMultisampleState = &multisample_;
    create_info_.pDepthStencilState =
        config.depth_format == VK_FORMAT_UNDEFINED ? nullptr : &depth_stencil_;
    create_info_.pColorBlendState = &color_blend_;
    create_info_.pDynamicState = &dynamic_state_;
    create_info_.layout = config.layout;
}

PipelineLayout::PipelineLayout(const Device& device, const VkPipelineLayoutCreateInfo& info)
    : device_(device.handle()) {
    if (device_ == VK_NULL_HANDLE) {
        throw std::runtime_error("pipeline layout creation requires a valid Vulkan device");
    }

    check(vkCreatePipelineLayout(device_, &info, nullptr, &layout_), "vkCreatePipelineLayout");
}

PipelineLayout::~PipelineLayout() {
    if (layout_ != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device_, layout_, nullptr);
    }
}

GraphicsPipeline::GraphicsPipeline(const Device& device, const VkGraphicsPipelineCreateInfo& info)
    : device_(device.handle()) {
    if (device_ == VK_NULL_HANDLE) {
        throw std::runtime_error("graphics pipeline creation requires a valid Vulkan device");
    }

    check(vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline_),
          "vkCreateGraphicsPipelines");
}

GraphicsPipeline::~GraphicsPipeline() {
    if (pipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(device_, pipeline_, nullptr);
    }
}

ComputePipeline::ComputePipeline(const Device& device, const VkComputePipelineCreateInfo& info)
    : device_(device.handle()) {
    if (device_ == VK_NULL_HANDLE) {
        throw std::runtime_error("compute pipeline creation requires a valid Vulkan device");
    }

    check(vkCreateComputePipelines(device_, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline_),
          "vkCreateComputePipelines");
}

ComputePipeline::~ComputePipeline() {
    if (pipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(device_, pipeline_, nullptr);
    }
}

} // namespace cubey::vulkan
