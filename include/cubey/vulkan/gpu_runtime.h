#pragma once

#include <cubey/vulkan/device.h>
#include <cubey/vulkan/submission_coordinator.h>
#include <cubey/vulkan/submission_tickets.h>

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <exception>
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
    [[nodiscard]] GpuSubmissionTicket completed_submission() const;
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
    GpuSubmissionTicket completed_submission;
};

enum class GpuRuntimeExecutionMode {
    Threaded,
    Inline,
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
    GpuRuntimeExecutionMode execution_mode = GpuRuntimeExecutionMode::Threaded;
};

class GpuRuntime {
  public:
    explicit GpuRuntime(GpuRuntimeConfig config);
    GpuRuntime(Device& device, SubmissionCoordinator& submission);
    ~GpuRuntime();

    GpuRuntime(const GpuRuntime&) = delete;
    GpuRuntime& operator=(const GpuRuntime&) = delete;
    GpuRuntime(GpuRuntime&&) = delete;
    GpuRuntime& operator=(GpuRuntime&&) = delete;

    [[nodiscard]] GpuWorkTicket enqueue(GpuWorkRequest request);
    [[nodiscard]] GpuWorkTicket submit_and_wait(GpuWorkRequest request);
    [[nodiscard]] GpuDrainResult drain();
    [[nodiscard]] GpuDrainResult drain_inline();
    [[nodiscard]] std::size_t pending_count() const;
    [[nodiscard]] bool empty() const;
    void mark_submission_completed(GpuSubmissionTicket ticket);
    void wait_queue_idle(std::string label);
    void wait_until_idle();
    void shutdown();

    [[nodiscard]] GpuOwnerContext owner_context() const;
    [[nodiscard]] GpuRuntimeExecutionMode execution_mode() const noexcept {
        return execution_mode_;
    }
    void require_owner_thread(const char* label) const;

  private:
    void start_threaded_owner();
    void run_threaded_owner();
    [[nodiscard]] GpuDrainResult drain_on_owner_thread();
    void record_threaded_failure(std::exception_ptr failure);
    void rethrow_threaded_failure_if_any();

    Device* device_ = nullptr;
    SubmissionCoordinator* submission_ = nullptr;
    GpuRuntimeExecutionMode execution_mode_ = GpuRuntimeExecutionMode::Threaded;
    std::thread::id owner_thread_{};
    GpuWorkQueue queue_;
    std::thread owner_thread_handle_;
    mutable std::mutex state_mutex_;
    std::condition_variable work_available_;
    std::condition_variable idle_;
    std::condition_variable owner_ready_;
    GpuDrainResult last_drain_result_{};
    std::exception_ptr threaded_failure_;
    bool accepting_work_ = true;
    bool stopping_ = false;
    bool active_work_ = false;
    bool owner_ready_flag_ = false;
    bool shutdown_complete_ = false;
};

} // namespace cubey::vulkan
