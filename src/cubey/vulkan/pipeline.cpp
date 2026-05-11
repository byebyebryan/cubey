#include <cubey/vulkan/pipeline.h>

#include <cubey/vulkan/vk_check.h>

#include <cstdint>
#include <stdexcept>

namespace cubey::vulkan {
namespace {

void validate_dynamic_graphics_pipeline_config(const DynamicGraphicsPipelineConfig& config) {
    if (config.layout == VK_NULL_HANDLE) {
        throw std::runtime_error("dynamic graphics pipeline requires a pipeline layout");
    }
    if (config.extent.width == 0 || config.extent.height == 0) {
        throw std::runtime_error("dynamic graphics pipeline requires a non-empty extent");
    }
    if (config.color_format == VK_FORMAT_UNDEFINED && config.depth_format == VK_FORMAT_UNDEFINED) {
        throw std::runtime_error(
            "dynamic graphics pipeline requires at least one attachment format");
    }
    if (config.shader_stages.empty()) {
        throw std::runtime_error("dynamic graphics pipeline requires at least one shader stage");
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
    const bool has_color_attachment = config.color_format != VK_FORMAT_UNDEFINED;
    color_format_ = config.color_format;

    rendering_info_ =
        vk_struct<VkPipelineRenderingCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO);
    rendering_info_.colorAttachmentCount = has_color_attachment ? 1U : 0U;
    rendering_info_.pColorAttachmentFormats = has_color_attachment ? &color_format_ : nullptr;
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

    viewport_.x = 0.0F;
    viewport_.y = 0.0F;
    viewport_.width = static_cast<float>(config.extent.width);
    viewport_.height = static_cast<float>(config.extent.height);
    viewport_.minDepth = 0.0F;
    viewport_.maxDepth = 1.0F;

    scissor_.offset = {0, 0};
    scissor_.extent = config.extent;

    viewport_state_ = vk_struct<VkPipelineViewportStateCreateInfo>(
        VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO);
    viewport_state_.viewportCount = 1;
    viewport_state_.pViewports = &viewport_;
    viewport_state_.scissorCount = 1;
    viewport_state_.pScissors = &scissor_;

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

    color_blend_attachment_.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                             VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    color_blend_attachment_.blendEnable = config.blend_enable ? VK_TRUE : VK_FALSE;
    color_blend_attachment_.srcColorBlendFactor = config.src_color_blend_factor;
    color_blend_attachment_.dstColorBlendFactor = config.dst_color_blend_factor;
    color_blend_attachment_.colorBlendOp = config.color_blend_op;
    color_blend_attachment_.srcAlphaBlendFactor = config.src_alpha_blend_factor;
    color_blend_attachment_.dstAlphaBlendFactor = config.dst_alpha_blend_factor;
    color_blend_attachment_.alphaBlendOp = config.alpha_blend_op;

    color_blend_ = vk_struct<VkPipelineColorBlendStateCreateInfo>(
        VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO);
    color_blend_.attachmentCount = has_color_attachment ? 1U : 0U;
    color_blend_.pAttachments = has_color_attachment ? &color_blend_attachment_ : nullptr;

    create_info_ =
        vk_struct<VkGraphicsPipelineCreateInfo>(VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO);
    create_info_.pNext = &rendering_info_;
    create_info_.stageCount = static_cast<std::uint32_t>(shader_stages_.size());
    create_info_.pStages = shader_stages_.data();
    create_info_.pVertexInputState = &vertex_input_;
    create_info_.pInputAssemblyState = &input_assembly_;
    create_info_.pViewportState = &viewport_state_;
    create_info_.pRasterizationState = &rasterizer_;
    create_info_.pMultisampleState = &multisample_;
    create_info_.pDepthStencilState =
        config.depth_format == VK_FORMAT_UNDEFINED ? nullptr : &depth_stencil_;
    create_info_.pColorBlendState = &color_blend_;
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
