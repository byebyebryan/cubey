#pragma once

#include <cubey/vulkan/device.h>

#include <vulkan/vulkan.h>

namespace cubey::vulkan {

struct CommandPoolConfig {
    VkCommandPoolCreateFlags flags = 0;
};

class CommandPool {
  public:
    CommandPool(const Device& device, const CommandPoolConfig& config);
    ~CommandPool();

    CommandPool(const CommandPool&) = delete;
    CommandPool& operator=(const CommandPool&) = delete;

    VkCommandPool handle() const {
        return command_pool_;
    }

    VkCommandBuffer allocate_primary() const;

  private:
    VkDevice device_ = VK_NULL_HANDLE;
    VkCommandPool command_pool_ = VK_NULL_HANDLE;
};

void begin_command_buffer(VkCommandBuffer command_buffer, VkCommandBufferUsageFlags flags);
void end_command_buffer(VkCommandBuffer command_buffer, const char* label);

} // namespace cubey::vulkan
