#pragma once

#include <cubey/vulkan/device.h>

#include <vulkan/vulkan.h>

namespace cubey::vulkan {

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
