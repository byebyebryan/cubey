#pragma once

#include <cubey/vulkan/device.h>

#include <vulkan/vulkan.h>

#include <span>
#include <vector>

namespace cubey::vulkan {

struct DynamicGraphicsPipelineConfig {
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkExtent2D extent{};
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
};

[[nodiscard]] VkPipelineShaderStageCreateInfo
shader_stage(VkShaderStageFlagBits stage, VkShaderModule module, const char* entry_point = "main");

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
    VkViewport viewport_{};
    VkRect2D scissor_{};
    VkPipelineViewportStateCreateInfo viewport_state_{};
    VkPipelineRasterizationStateCreateInfo rasterizer_{};
    VkPipelineMultisampleStateCreateInfo multisample_{};
    VkPipelineDepthStencilStateCreateInfo depth_stencil_{};
    VkPipelineColorBlendAttachmentState color_blend_attachment_{};
    VkPipelineColorBlendStateCreateInfo color_blend_{};
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
