#pragma once

#include <cubey/vulkan/device.h>

#include <vulkan/vulkan.h>

#include <cstdint>

namespace cubey::vulkan {

class FrameResources {
  public:
    explicit FrameResources(const Device& device);
    ~FrameResources();

    FrameResources(const FrameResources&) = delete;
    FrameResources& operator=(const FrameResources&) = delete;

    VkCommandPool command_pool() const {
        return command_pool_;
    }
    VkCommandBuffer command_buffer() const {
        return command_buffer_;
    }
    VkSemaphore image_available() const {
        return image_available_;
    }
    VkSemaphore present_ready() const {
        return present_ready_;
    }
    VkFence fence() const {
        return fence_;
    }

    void wait_for_frame() const;
    void reset_fence() const;
    void reset_command_buffer() const;

  private:
    void create();
    void destroy();

    VkDevice device_ = VK_NULL_HANDLE;
    std::uint32_t queue_family_ = 0;
    VkCommandPool command_pool_ = VK_NULL_HANDLE;
    VkCommandBuffer command_buffer_ = VK_NULL_HANDLE;
    VkSemaphore image_available_ = VK_NULL_HANDLE;
    VkSemaphore present_ready_ = VK_NULL_HANDLE;
    VkFence fence_ = VK_NULL_HANDLE;
};

} // namespace cubey::vulkan
