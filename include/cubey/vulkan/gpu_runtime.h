#pragma once

#include <cubey/frame_tickets.h>
#include <cubey/vulkan/device.h>
#include <cubey/vulkan/submission_coordinator.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace cubey::vulkan {

class GpuOwnerContext {
  public:
    GpuOwnerContext(Device& device, SubmissionCoordinator& submission,
                    std::thread::id owner_thread);

    [[nodiscard]] Device& device() const;
    [[nodiscard]] SubmissionCoordinator& submission() const;
    [[nodiscard]] FrameTicket completed_submission() const;
    [[nodiscard]] bool is_owner_thread() const;
    void require_owner_thread(const char* label) const;

  private:
    Device* device_ = nullptr;
    SubmissionCoordinator* submission_ = nullptr;
    std::thread::id owner_thread_{};
};

struct GpuWorkTicket {
    std::uint64_t id = 0;
    std::string label;
};

struct GpuWorkRequest {
    std::string label;
    std::function<void(GpuOwnerContext&)> work;
};

struct QueuedGpuWork {
    GpuWorkTicket ticket;
    std::function<void(GpuOwnerContext&)> work;
};

struct GpuDrainResult {
    std::size_t completed_count = 0;
    GpuWorkTicket last_completed;
    FrameTicket completed_submission;
};

class GpuWorkQueue {
  public:
    GpuWorkQueue() = default;

    GpuWorkQueue(const GpuWorkQueue&) = delete;
    GpuWorkQueue& operator=(const GpuWorkQueue&) = delete;
    GpuWorkQueue(GpuWorkQueue&&) = delete;
    GpuWorkQueue& operator=(GpuWorkQueue&&) = delete;

    [[nodiscard]] GpuWorkTicket enqueue(GpuWorkRequest request);
    [[nodiscard]] std::vector<QueuedGpuWork> drain();
    [[nodiscard]] std::size_t pending_count() const;
    [[nodiscard]] bool empty() const;

  private:
    friend class GpuRuntime;

    void restore_front(std::vector<QueuedGpuWork> work);

    mutable std::mutex mutex_;
    std::uint64_t next_id_ = 1;
    std::vector<QueuedGpuWork> work_;
};

struct GpuRuntimeConfig {
    Device* device = nullptr;
    SubmissionCoordinator* submission = nullptr;
};

class GpuRuntime {
  public:
    explicit GpuRuntime(GpuRuntimeConfig config);
    GpuRuntime(Device& device, SubmissionCoordinator& submission);

    GpuRuntime(const GpuRuntime&) = delete;
    GpuRuntime& operator=(const GpuRuntime&) = delete;
    GpuRuntime(GpuRuntime&&) = delete;
    GpuRuntime& operator=(GpuRuntime&&) = delete;

    [[nodiscard]] GpuWorkTicket enqueue(GpuWorkRequest request);
    [[nodiscard]] GpuDrainResult drain_inline();
    [[nodiscard]] std::size_t pending_count() const;
    [[nodiscard]] bool empty() const;

    [[nodiscard]] GpuOwnerContext owner_context() const;
    void require_owner_thread(const char* label) const;

  private:
    Device* device_ = nullptr;
    SubmissionCoordinator* submission_ = nullptr;
    std::thread::id owner_thread_{};
    GpuWorkQueue queue_;
};

} // namespace cubey::vulkan
