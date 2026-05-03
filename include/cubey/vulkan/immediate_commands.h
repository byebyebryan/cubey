#pragma once

#include <cubey/vulkan/command_pool.h>
#include <cubey/vulkan/device.h>

#include <vulkan/vulkan.h>

#include <cstdint>

namespace cubey::vulkan {

class ImmediateCommands {
  public:
    explicit ImmediateCommands(const Device& device);
    ~ImmediateCommands();

    ImmediateCommands(const ImmediateCommands&) = delete;
    ImmediateCommands& operator=(const ImmediateCommands&) = delete;

    VkCommandBuffer command_buffer() const {
        return command_buffer_;
    }

    void submit_and_wait();

  private:
    void create();
    void destroy();

    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue queue_ = VK_NULL_HANDLE;
    CommandPool command_pool_;
    VkCommandBuffer command_buffer_ = VK_NULL_HANDLE;
    bool submitted_ = false;
};

} // namespace cubey::vulkan
