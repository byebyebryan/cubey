#pragma once

#include <cubey/vulkan/device.h>

#include <vulkan/vulkan.h>

#include <array>
#include <span>
#include <vector>

namespace cubey::vulkan {

struct DynamicGraphicsPipelineConfig {
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkFormat color_format = VK_FORMAT_UNDEFINED;
    VkFormat depth_format = VK_FORMAT_UNDEFINED;
    std::span<const VkPipelineShaderStageCreateInfo> shader_stages;
    std::span<const VkVertexInputBindingDescription> vertex_bindings;
    std::span<const VkVertexInputAttributeDescription> vertex_attributes;
    VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPolygonMode polygon_mode = VK_POLYGON_MODE_FILL;
    VkCullModeFlags cull_mode = VK_CULL_MODE_NONE;
    VkFrontFace front_face = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    bool depth_test = false;
    bool depth_write = false;
    VkCompareOp depth_compare_op = VK_COMPARE_OP_LESS;
    bool blend_enable = false;
    VkBlendFactor src_color_blend_factor = VK_BLEND_FACTOR_ONE;
    VkBlendFactor dst_color_blend_factor = VK_BLEND_FACTOR_ZERO;
    VkBlendOp color_blend_op = VK_BLEND_OP_ADD;
    VkBlendFactor src_alpha_blend_factor = VK_BLEND_FACTOR_ONE;
    VkBlendFactor dst_alpha_blend_factor = VK_BLEND_FACTOR_ZERO;
    VkBlendOp alpha_blend_op = VK_BLEND_OP_ADD;
};

struct PipelineLayoutConfig {
    std::span<const VkDescriptorSetLayout> set_layouts;
    std::span<const VkPushConstantRange> push_constants;
};

class PipelineLayoutInfo {
  public:
    explicit PipelineLayoutInfo(const PipelineLayoutConfig& config);

    PipelineLayoutInfo(const PipelineLayoutInfo&) = delete;
    PipelineLayoutInfo& operator=(const PipelineLayoutInfo&) = delete;
    PipelineLayoutInfo(PipelineLayoutInfo&&) = delete;
    PipelineLayoutInfo& operator=(PipelineLayoutInfo&&) = delete;

    [[nodiscard]] const VkPipelineLayoutCreateInfo& create_info() const {
        return create_info_;
    }

  private:
    std::vector<VkDescriptorSetLayout> set_layouts_;
    std::vector<VkPushConstantRange> push_constants_;
    VkPipelineLayoutCreateInfo create_info_{};
};

struct ComputePipelineConfig {
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkPipelineShaderStageCreateInfo shader_stage{};
};

[[nodiscard]] VkPipelineShaderStageCreateInfo
shader_stage(VkShaderStageFlagBits stage, VkShaderModule module, const char* entry_point = "main");

class ComputePipelineInfo {
  public:
    explicit ComputePipelineInfo(const ComputePipelineConfig& config);

    ComputePipelineInfo(const ComputePipelineInfo&) = delete;
    ComputePipelineInfo& operator=(const ComputePipelineInfo&) = delete;
    ComputePipelineInfo(ComputePipelineInfo&&) = delete;
    ComputePipelineInfo& operator=(ComputePipelineInfo&&) = delete;

    [[nodiscard]] const VkComputePipelineCreateInfo& create_info() const {
        return create_info_;
    }

  private:
    VkPipelineShaderStageCreateInfo shader_stage_{};
    VkComputePipelineCreateInfo create_info_{};
};

class DynamicGraphicsPipelineInfo {
  public:
    explicit DynamicGraphicsPipelineInfo(const DynamicGraphicsPipelineConfig& config);

    DynamicGraphicsPipelineInfo(const DynamicGraphicsPipelineInfo&) = delete;
    DynamicGraphicsPipelineInfo& operator=(const DynamicGraphicsPipelineInfo&) = delete;
    DynamicGraphicsPipelineInfo(DynamicGraphicsPipelineInfo&&) = delete;
    DynamicGraphicsPipelineInfo& operator=(DynamicGraphicsPipelineInfo&&) = delete;

    [[nodiscard]] const VkGraphicsPipelineCreateInfo& create_info() const {
        return create_info_;
    }

  private:
    std::vector<VkPipelineShaderStageCreateInfo> shader_stages_;
    std::vector<VkVertexInputBindingDescription> vertex_bindings_;
    std::vector<VkVertexInputAttributeDescription> vertex_attributes_;
    VkFormat color_format_ = VK_FORMAT_UNDEFINED;
    VkPipelineRenderingCreateInfo rendering_info_{};
    VkPipelineVertexInputStateCreateInfo vertex_input_{};
    VkPipelineInputAssemblyStateCreateInfo input_assembly_{};
    VkPipelineViewportStateCreateInfo viewport_state_{};
    VkPipelineRasterizationStateCreateInfo rasterizer_{};
    VkPipelineMultisampleStateCreateInfo multisample_{};
    VkPipelineDepthStencilStateCreateInfo depth_stencil_{};
    VkPipelineColorBlendAttachmentState color_blend_attachment_{};
    VkPipelineColorBlendStateCreateInfo color_blend_{};
    std::array<VkDynamicState, 2> dynamic_states_{};
    VkPipelineDynamicStateCreateInfo dynamic_state_{};
    VkGraphicsPipelineCreateInfo create_info_{};
};

class PipelineLayout {
  public:
    PipelineLayout(const Device& device, const VkPipelineLayoutCreateInfo& info);
    ~PipelineLayout();

    PipelineLayout(const PipelineLayout&) = delete;
    PipelineLayout& operator=(const PipelineLayout&) = delete;

    VkPipelineLayout handle() const {
        return layout_;
    }

  private:
    VkDevice device_ = VK_NULL_HANDLE;
    VkPipelineLayout layout_ = VK_NULL_HANDLE;
};

class GraphicsPipeline {
  public:
    GraphicsPipeline(const Device& device, const VkGraphicsPipelineCreateInfo& info);
    ~GraphicsPipeline();

    GraphicsPipeline(const GraphicsPipeline&) = delete;
    GraphicsPipeline& operator=(const GraphicsPipeline&) = delete;

    VkPipeline handle() const {
        return pipeline_;
    }

  private:
    VkDevice device_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
};

class ComputePipeline {
  public:
    ComputePipeline(const Device& device, const VkComputePipelineCreateInfo& info);
    ~ComputePipeline();

    ComputePipeline(const ComputePipeline&) = delete;
    ComputePipeline& operator=(const ComputePipeline&) = delete;

    VkPipeline handle() const {
        return pipeline_;
    }

  private:
    VkDevice device_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
};

} // namespace cubey::vulkan
