#include <cubey/vulkan/frame_resources.h>

#include <cubey/vulkan/vk_check.h>

#include <stdexcept>

namespace cubey::vulkan {

FrameResources::FrameResources(const Device& device, std::size_t present_ready_count)
    : device_(device.handle()),
      command_pool_(device, {.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT}),
      present_ready_(present_ready_count, VK_NULL_HANDLE) {
    if (device_ == VK_NULL_HANDLE) {
        throw std::runtime_error("frame resources require a valid Vulkan device");
    }
    if (present_ready_.empty()) {
        throw std::runtime_error("frame resources require at least one present-ready semaphore");
    }

    try {
        create();
    } catch (...) {
        destroy();
        throw;
    }
}

FrameResources::~FrameResources() {
    destroy();
}

void FrameResources::wait_for_frame() const {
    check(vkWaitForFences(device_, 1, &fence_, VK_TRUE, UINT64_MAX), "vkWaitForFences frame");
}

void FrameResources::reset_fence() const {
    check(vkResetFences(device_, 1, &fence_), "vkResetFences frame");
}

void FrameResources::reset_command_buffer() const {
    check(vkResetCommandBuffer(command_buffer_, 0), "vkResetCommandBuffer frame");
}

void FrameResources::create() {
    command_buffer_ = command_pool_.allocate_primary();

    auto semaphore_info = vk_struct<VkSemaphoreCreateInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO);
    check(vkCreateSemaphore(device_, &semaphore_info, nullptr, &image_available_),
          "vkCreateSemaphore image_available");
    for (VkSemaphore& semaphore : present_ready_) {
        check(vkCreateSemaphore(device_, &semaphore_info, nullptr, &semaphore),
              "vkCreateSemaphore present_ready");
    }

    auto fence_info = vk_struct<VkFenceCreateInfo>(VK_STRUCTURE_TYPE_FENCE_CREATE_INFO);
    fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    check(vkCreateFence(device_, &fence_info, nullptr, &fence_), "vkCreateFence frame");
}

void FrameResources::destroy() {
    if (fence_ != VK_NULL_HANDLE) {
        vkDestroyFence(device_, fence_, nullptr);
        fence_ = VK_NULL_HANDLE;
    }
    for (VkSemaphore& semaphore : present_ready_) {
        if (semaphore != VK_NULL_HANDLE) {
            vkDestroySemaphore(device_, semaphore, nullptr);
            semaphore = VK_NULL_HANDLE;
        }
    }
    present_ready_.clear();
    if (image_available_ != VK_NULL_HANDLE) {
        vkDestroySemaphore(device_, image_available_, nullptr);
        image_available_ = VK_NULL_HANDLE;
    }
    command_buffer_ = VK_NULL_HANDLE;
}

} // namespace cubey::vulkan
