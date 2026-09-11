#pragma once

#include <cubey/vulkan/command_pool.h>

#include <vulkan/vulkan.h>

namespace cubey::vulkan {

// Reusable owner-thread-only backing for one upload step. Runtime owns idle
// instances; a submitted step exclusively owns one until its ticket retires.
class GpuUploadStepResources {
  public:
    explicit GpuUploadStepResources(const Device& device);
    ~GpuUploadStepResources();

    GpuUploadStepResources(const GpuUploadStepResources&) = delete;
    GpuUploadStepResources& operator=(const GpuUploadStepResources&) = delete;
    GpuUploadStepResources(GpuUploadStepResources&&) = delete;
    GpuUploadStepResources& operator=(GpuUploadStepResources&&) = delete;

    void begin_recording();
    [[nodiscard]] VkCommandBuffer command_buffer() const noexcept;
    [[nodiscard]] VkFence fence() const noexcept;

  private:
    VkDevice device_ = VK_NULL_HANDLE;
    CommandPool command_pool_;
    VkCommandBuffer command_buffer_ = VK_NULL_HANDLE;
    VkFence fence_ = VK_NULL_HANDLE;
};

} // namespace cubey::vulkan
