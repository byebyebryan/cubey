#pragma once

#include <cubey/vulkan/command_pool.h>
#include <cubey/vulkan/device.h>

#include <vulkan/vulkan.h>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace cubey::vulkan {

class FrameResources {
  public:
    explicit FrameResources(const Device& device, std::size_t present_ready_count);
    ~FrameResources();

    FrameResources(const FrameResources&) = delete;
    FrameResources& operator=(const FrameResources&) = delete;

    VkCommandPool command_pool() const {
        return command_pool_.handle();
    }
    VkCommandBuffer command_buffer() const {
        return command_buffer_;
    }
    VkSemaphore image_available() const {
        return image_available_;
    }
    VkSemaphore present_ready(std::size_t image_index) const {
        return present_ready_.at(image_index);
    }
    std::size_t present_ready_count() const {
        return present_ready_.size();
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
    CommandPool command_pool_;
    VkCommandBuffer command_buffer_ = VK_NULL_HANDLE;
    VkSemaphore image_available_ = VK_NULL_HANDLE;
    std::vector<VkSemaphore> present_ready_;
    VkFence fence_ = VK_NULL_HANDLE;
};

} // namespace cubey::vulkan
