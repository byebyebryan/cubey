#include <cubey/vulkan/pipeline.h>

#include <cubey/vulkan/vk_check.h>

#include <stdexcept>

namespace cubey::vulkan {

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
