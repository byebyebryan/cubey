#pragma once

#include <cubey/vulkan/gpu_runtime.h>

#include <vulkan/vulkan.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>

namespace cubey::vulkan {

class GpuUploadStepState;

struct GpuUploadStepConfig {
    VkDeviceSize max_staged_byte_size = kDefaultGpuUploadStepByteCap;
    // Observed by GpuUploadStepMetrics; this is a tuning target for a later
    // scheduler, not a hidden submission budget in this low-level primitive.
    double owner_cpu_target_milliseconds = 2.0;
};

struct GpuUploadStepMetrics {
    VkDeviceSize staged_byte_size = 0;
    std::uint32_t copy_count = 0;
    double owner_submit_milliseconds = 0.0;
    double completion_latency_milliseconds = 0.0;
};

struct GpuUploadStagingSlice {
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceSize offset = 0;
    VkDeviceSize byte_size = 0;
};

class GpuUploadStepTicket {
  public:
    GpuUploadStepTicket() = default;

    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] bool complete() const noexcept;
    [[nodiscard]] bool failed() const noexcept;
    [[nodiscard]] std::string failure_message() const;
    [[nodiscard]] GpuSubmissionTicket submission_ticket() const noexcept;
    [[nodiscard]] GpuUploadStepMetrics metrics() const;
    [[nodiscard]] bool poll(GpuRuntime& gpu) const;
    void wait(GpuRuntime& gpu) const;

  private:
    friend class GpuUploadStep;
    explicit GpuUploadStepTicket(std::shared_ptr<GpuUploadStepState> state);

    std::shared_ptr<GpuUploadStepState> state_{};
};

// One bounded graphics-queue upload submission. It is a non-movable lexical
// GPU-owner object: callers stage up to its byte cap, record copies against its
// command buffer, then submit and retain the ticket. A missing staging slice is
// explicit bounded-pool backpressure, not permission to allocate more memory.
class GpuUploadStep {
  public:
    explicit GpuUploadStep(GpuOwnerContext& context, GpuUploadStepConfig config = {});
    ~GpuUploadStep();

    GpuUploadStep(const GpuUploadStep&) = delete;
    GpuUploadStep& operator=(const GpuUploadStep&) = delete;
    GpuUploadStep(GpuUploadStep&&) = delete;
    GpuUploadStep& operator=(GpuUploadStep&&) = delete;

    [[nodiscard]] VkCommandBuffer command_buffer() const;
    [[nodiscard]] std::optional<GpuUploadStagingSlice>
    try_stage_copy(std::span<const std::byte> bytes, VkDeviceSize alignment = 4U);
    void add_copy_count(std::uint32_t copy_count = 1U);
    [[nodiscard]] GpuUploadStepTicket submit(GpuOwnerContext& context,
                                             std::string label = "vkQueueSubmit upload step");

  private:
    GpuOwnerContext* owner_context_ = nullptr;
    std::shared_ptr<GpuUploadStepState> state_{};
};

} // namespace cubey::vulkan
