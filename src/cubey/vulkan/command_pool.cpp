#include <cubey/vulkan/command_pool.h>

#include <cubey/vulkan/vk_check.h>

#include <stdexcept>

namespace cubey::vulkan {

CommandPool::CommandPool(const Device& device, const CommandPoolConfig& config)
    : device_(device.handle()) {
    if (device_ == VK_NULL_HANDLE) {
        throw std::runtime_error("command pool creation requires a valid Vulkan device");
    }

    auto pool_info = vk_struct<VkCommandPoolCreateInfo>(VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO);
    pool_info.flags = config.flags;
    pool_info.queueFamilyIndex = device.queue_family();
    check(vkCreateCommandPool(device_, &pool_info, nullptr, &command_pool_), "vkCreateCommandPool");
}

CommandPool::~CommandPool() {
    if (command_pool_ != VK_NULL_HANDLE) {
        vkDestroyCommandPool(device_, command_pool_, nullptr);
    }
}

VkCommandBuffer CommandPool::allocate_primary() const {
    auto alloc =
        vk_struct<VkCommandBufferAllocateInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO);
    alloc.commandPool = command_pool_;
    alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc.commandBufferCount = 1;

    VkCommandBuffer command_buffer = VK_NULL_HANDLE;
    check(vkAllocateCommandBuffers(device_, &alloc, &command_buffer), "vkAllocateCommandBuffers");
    return command_buffer;
}

void begin_command_buffer(VkCommandBuffer command_buffer, VkCommandBufferUsageFlags flags) {
    if (command_buffer == VK_NULL_HANDLE) {
        throw std::runtime_error("begin command buffer requires a command buffer");
    }

    auto begin = vk_struct<VkCommandBufferBeginInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO);
    begin.flags = flags;
    check(vkBeginCommandBuffer(command_buffer, &begin), "vkBeginCommandBuffer");
}

} // namespace cubey::vulkan
