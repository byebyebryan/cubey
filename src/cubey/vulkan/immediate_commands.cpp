#include <cubey/vulkan/immediate_commands.h>

#include <cubey/vulkan/vk_check.h>

#include <stdexcept>

namespace cubey::vulkan {

ImmediateCommands::ImmediateCommands(const Device& device)
    : device_(device.handle()), queue_(device.queue()), queue_family_(device.queue_family()) {
    if (device_ == VK_NULL_HANDLE || queue_ == VK_NULL_HANDLE) {
        throw std::runtime_error("immediate commands require a valid Vulkan device and queue");
    }

    try {
        create();
    } catch (...) {
        destroy();
        throw;
    }
}

ImmediateCommands::~ImmediateCommands() {
    destroy();
}

void ImmediateCommands::submit_and_wait() {
    if (submitted_) {
        throw std::runtime_error("immediate commands were already submitted");
    }

    check(vkEndCommandBuffer(command_buffer_), "vkEndCommandBuffer immediate");

    auto submit = vk_struct<VkSubmitInfo>(VK_STRUCTURE_TYPE_SUBMIT_INFO);
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &command_buffer_;
    check(vkQueueSubmit(queue_, 1, &submit, VK_NULL_HANDLE), "vkQueueSubmit immediate");
    check(vkQueueWaitIdle(queue_), "vkQueueWaitIdle immediate");
    submitted_ = true;
}

void ImmediateCommands::create() {
    auto pool_info = vk_struct<VkCommandPoolCreateInfo>(VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO);
    pool_info.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    pool_info.queueFamilyIndex = queue_family_;
    check(vkCreateCommandPool(device_, &pool_info, nullptr, &command_pool_),
          "vkCreateCommandPool immediate");

    auto alloc =
        vk_struct<VkCommandBufferAllocateInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO);
    alloc.commandPool = command_pool_;
    alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc.commandBufferCount = 1;
    check(vkAllocateCommandBuffers(device_, &alloc, &command_buffer_),
          "vkAllocateCommandBuffers immediate");

    auto begin = vk_struct<VkCommandBufferBeginInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO);
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    check(vkBeginCommandBuffer(command_buffer_, &begin), "vkBeginCommandBuffer immediate");
}

void ImmediateCommands::destroy() {
    if (command_pool_ != VK_NULL_HANDLE) {
        vkDestroyCommandPool(device_, command_pool_, nullptr);
        command_pool_ = VK_NULL_HANDLE;
        command_buffer_ = VK_NULL_HANDLE;
    }
}

} // namespace cubey::vulkan
