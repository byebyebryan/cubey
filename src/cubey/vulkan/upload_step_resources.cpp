#include <cubey/vulkan/detail/upload_step_resources.h>

#include <cubey/vulkan/vk_check.h>

#include <stdexcept>

namespace cubey::vulkan {

GpuUploadStepResources::GpuUploadStepResources(const Device& device)
    : device_(device.handle()),
      command_pool_(device, CommandPoolConfig{.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT}) {
    if (device_ == VK_NULL_HANDLE) {
        throw std::runtime_error("GPU upload step resources require a valid Vulkan device");
    }
    command_buffer_ = command_pool_.allocate_primary();
    VkFenceCreateInfo fence_info{};
    fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    check(vkCreateFence(device_, &fence_info, nullptr, &fence_), "vkCreateFence upload step");
}

GpuUploadStepResources::~GpuUploadStepResources() {
    if (fence_ != VK_NULL_HANDLE) {
        vkDestroyFence(device_, fence_, nullptr);
    }
}

void GpuUploadStepResources::begin_recording() {
    check(vkResetFences(device_, 1, &fence_), "vkResetFences upload step");
    check(vkResetCommandPool(device_, command_pool_.handle(), 0U),
          "vkResetCommandPool upload step");
    begin_command_buffer(command_buffer_, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
}

VkCommandBuffer GpuUploadStepResources::command_buffer() const noexcept {
    return command_buffer_;
}

VkFence GpuUploadStepResources::fence() const noexcept {
    return fence_;
}

} // namespace cubey::vulkan
