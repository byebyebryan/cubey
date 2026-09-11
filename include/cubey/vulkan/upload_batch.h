#pragma once

#include <cubey/vulkan/gpu_runtime.h>

#include <vulkan/vulkan.h>

#include <cstdint>
#include <memory>
#include <string>

namespace cubey::vulkan {

class Buffer;
class Device;
class GpuUploadBatchState;

// Measurements for one submitted upload batch. The CPU value ends when the
// batch is queued; completion latency runs from that queue submission until
// the GPU owner observes its fence signaled.
struct GpuUploadBatchMetrics {
    std::uint64_t uploaded_byte_count = 0;
    std::uint32_t copy_count = 0;
    std::uint32_t submission_count = 0;
    double owner_submit_milliseconds = 0.0;
    double completion_latency_milliseconds = 0.0;
};

// A completion handle for one same-queue upload submission. Completion is
// queried only by GPU-owner work, so the caller can poll it without waiting for
// the graphics queue or touching Vulkan objects from an arbitrary thread.
class GpuUploadTicket {
  public:
    GpuUploadTicket() = default;

    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] bool complete() const noexcept;
    [[nodiscard]] bool failed() const noexcept;
    [[nodiscard]] std::string failure_message() const;
    [[nodiscard]] GpuSubmissionTicket submission_ticket() const noexcept;
    [[nodiscard]] GpuUploadBatchMetrics metrics() const;

    // Schedules one nonblocking fence-status query when one is not already in
    // flight. Returns true only after the GPU has completed the batch.
    [[nodiscard]] bool poll(GpuRuntime& gpu) const;
    // The explicit headless/shutdown path. Normal windowed loading must use
    // poll() instead.
    void wait(GpuRuntime& gpu) const;

  private:
    friend class GpuUploadBatch;
    explicit GpuUploadTicket(std::shared_ptr<GpuUploadBatchState> state);

    std::shared_ptr<GpuUploadBatchState> state_{};
};

// Generation-scoped staging and command ownership for one asynchronous upload
// submission on Cubey's existing graphics queue. Callers create destination
// resources normally, append copies, submit once, then retain the returned
// ticket until it completes. Like ImmediateCommands, this deliberately stays
// lexical and non-movable inside its creating GPU-owner callback, so an
// unsubmitted recording is released on that owner scope.
class GpuUploadBatch {
  public:
    explicit GpuUploadBatch(GpuOwnerContext& context);
    ~GpuUploadBatch();

    GpuUploadBatch(const GpuUploadBatch&) = delete;
    GpuUploadBatch& operator=(const GpuUploadBatch&) = delete;
    GpuUploadBatch(GpuUploadBatch&&) = delete;
    GpuUploadBatch& operator=(GpuUploadBatch&&) = delete;

    [[nodiscard]] const Device& device() const;
    [[nodiscard]] VkCommandBuffer command_buffer() const;
    void retain_staging(Buffer&& staging);
    void add_uploaded_bytes(VkDeviceSize byte_count);
    void add_copy_count(std::uint32_t copy_count = 1U);
    [[nodiscard]] GpuUploadTicket submit(GpuOwnerContext& context,
                                         std::string label = "vkQueueSubmit upload batch");

  private:
    // This is an owner-thread lexical object, like ImmediateCommands. Its
    // destructor releases an unsubmitted recording on this context; completed
    // submissions are retained by their ticket instead.
    GpuOwnerContext* owner_context_ = nullptr;
    std::shared_ptr<GpuUploadBatchState> state_{};
};

} // namespace cubey::vulkan
