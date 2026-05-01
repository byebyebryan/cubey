#include <cubey/vulkan/frame_resources.h>

#include <cubey/vulkan/vk_check.h>

#include <stdexcept>

namespace cubey::vulkan {

FrameResources::FrameResources(const Device& device)
    : device_(device.handle()), queue_family_(device.queue_family()) {
    if (device_ == VK_NULL_HANDLE) {
        throw std::runtime_error("frame resources require a valid Vulkan device");
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
    auto pool_info = vk_struct<VkCommandPoolCreateInfo>(VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO);
    pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool_info.queueFamilyIndex = queue_family_;
    check(vkCreateCommandPool(device_, &pool_info, nullptr, &command_pool_), "vkCreateCommandPool");

    auto alloc =
        vk_struct<VkCommandBufferAllocateInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO);
    alloc.commandPool = command_pool_;
    alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc.commandBufferCount = 1;
    check(vkAllocateCommandBuffers(device_, &alloc, &command_buffer_), "vkAllocateCommandBuffers");

    auto semaphore_info = vk_struct<VkSemaphoreCreateInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO);
    check(vkCreateSemaphore(device_, &semaphore_info, nullptr, &image_available_),
          "vkCreateSemaphore image_available");
    check(vkCreateSemaphore(device_, &semaphore_info, nullptr, &present_ready_),
          "vkCreateSemaphore present_ready");

    auto fence_info = vk_struct<VkFenceCreateInfo>(VK_STRUCTURE_TYPE_FENCE_CREATE_INFO);
    fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    check(vkCreateFence(device_, &fence_info, nullptr, &fence_), "vkCreateFence frame");
}

void FrameResources::destroy() {
    if (fence_ != VK_NULL_HANDLE) {
        vkDestroyFence(device_, fence_, nullptr);
        fence_ = VK_NULL_HANDLE;
    }
    if (present_ready_ != VK_NULL_HANDLE) {
        vkDestroySemaphore(device_, present_ready_, nullptr);
        present_ready_ = VK_NULL_HANDLE;
    }
    if (image_available_ != VK_NULL_HANDLE) {
        vkDestroySemaphore(device_, image_available_, nullptr);
        image_available_ = VK_NULL_HANDLE;
    }
    if (command_pool_ != VK_NULL_HANDLE) {
        vkDestroyCommandPool(device_, command_pool_, nullptr);
        command_pool_ = VK_NULL_HANDLE;
        command_buffer_ = VK_NULL_HANDLE;
    }
}

} // namespace cubey::vulkan
