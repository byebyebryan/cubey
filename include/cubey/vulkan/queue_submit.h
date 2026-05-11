#pragma once

#include <cubey/vulkan/device.h>

#include <vulkan/vulkan.h>

#include <vector>

namespace cubey::vulkan {

struct QueueWait {
    VkSemaphore semaphore = VK_NULL_HANDLE;
    VkPipelineStageFlags stage_mask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
};

struct QueueSignal {
    VkSemaphore semaphore = VK_NULL_HANDLE;
};

struct QueueSubmitInfo {
    std::vector<QueueWait> waits{};
    std::vector<VkCommandBuffer> command_buffers{};
    std::vector<QueueSignal> signals{};
    VkFence fence = VK_NULL_HANDLE;
};

class QueueSubmit {
  public:
    explicit QueueSubmit(const QueueSubmitInfo& config);

    [[nodiscard]] const VkSubmitInfo& info() const noexcept {
        return info_;
    }

    [[nodiscard]] VkFence fence() const noexcept {
        return fence_;
    }

  private:
    std::vector<VkSemaphore> wait_semaphores_{};
    std::vector<VkPipelineStageFlags> wait_stages_{};
    std::vector<VkCommandBuffer> command_buffers_{};
    std::vector<VkSemaphore> signal_semaphores_{};
    VkFence fence_ = VK_NULL_HANDLE;
    VkSubmitInfo info_{};
};

void submit_to_queue(VkQueue queue, const QueueSubmitInfo& submit_info, const char* label);
void submit_to_device_queue(const Device& device, const QueueSubmitInfo& submit_info,
                            const char* label);
void wait_for_queue_idle(VkQueue queue, const char* label);

} // namespace cubey::vulkan
