#pragma once

#include <cubey/vulkan/device.h>
#include <cubey/vulkan/submission_coordinator.h>
#include <cubey/vulkan/submission_tickets.h>

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
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

template <typename T>
class GpuJobHandle {
  public:
    GpuJobHandle(GpuWorkTicket ticket, std::future<T> future)
        : ticket_(std::move(ticket)), future_(std::move(future)) {}

    GpuJobHandle(const GpuJobHandle&) = delete;
    GpuJobHandle& operator=(const GpuJobHandle&) = delete;
    GpuJobHandle(GpuJobHandle&&) noexcept = default;
    GpuJobHandle& operator=(GpuJobHandle&&) noexcept = default;

    [[nodiscard]] const GpuWorkTicket& ticket() const noexcept {
        return ticket_;
    }

    [[nodiscard]] bool ready() const {
        return future_.wait_for(std::chrono::seconds{0}) == std::future_status::ready;
    }

    void wait() const {
        future_.wait();
    }

    T get() {
        return future_.get();
    }

  private:
    GpuWorkTicket ticket_{};
    mutable std::future<T> future_;
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
    template <typename Function>
    [[nodiscard]] auto submit(std::string label, Function&& function)
        -> GpuJobHandle<
            std::invoke_result_t<std::decay_t<Function>&, cubey::vulkan::GpuOwnerContext&>> {
        using Callable = std::decay_t<Function>;
        using Result = std::invoke_result_t<Callable&, cubey::vulkan::GpuOwnerContext&>;

        Callable callable(std::forward<Function>(function));
        auto task = std::make_shared<std::packaged_task<Result(GpuOwnerContext&)>>(
            [callable = std::move(callable)](GpuOwnerContext& context) mutable -> Result {
                if constexpr (std::is_void_v<Result>) {
                    std::invoke(callable, context);
                } else {
                    return std::invoke(callable, context);
                }
            });
        std::future<Result> future = task->get_future();
        GpuWorkTicket ticket = enqueue({
            .label = std::move(label),
            .work = [task](GpuOwnerContext& context) { (*task)(context); },
        });
        return GpuJobHandle<Result>(std::move(ticket), std::move(future));
    }
    [[nodiscard]] GpuWorkTicket submit_and_wait(GpuWorkRequest request);
    [[nodiscard]] GpuDrainResult drain();
    [[nodiscard]] GpuDrainResult drain_inline();
    [[nodiscard]] std::size_t pending_count() const;
    [[nodiscard]] bool empty() const;
    void defer_destruction_after(GpuSubmissionTicket ticket, std::function<void()> action);
    [[nodiscard]] std::size_t deferred_destruction_count() const;
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
    enum class State {
        Running,
        Closing,
        Stopping,
        Stopped,
    };

    void start_threaded_owner();
    void run_threaded_owner();
    [[nodiscard]] GpuDrainResult drain_on_owner_thread();
    [[nodiscard]] std::size_t collect_retired_on_owner_thread();
    void record_threaded_failure(std::exception_ptr failure);
    void rethrow_threaded_failure_if_any();

    Device* device_ = nullptr;
    SubmissionCoordinator* submission_ = nullptr;
    GpuRuntimeExecutionMode execution_mode_ = GpuRuntimeExecutionMode::Threaded;
    std::thread::id owner_thread_{};
    GpuWorkQueue queue_;
    DeferredGpuDestructionQueue deferred_destruction_;
    std::thread owner_thread_handle_;
    mutable std::mutex state_mutex_;
    std::condition_variable work_available_;
    std::condition_variable idle_;
    std::condition_variable owner_ready_;
    GpuDrainResult last_drain_result_{};
    std::exception_ptr threaded_failure_;
    State state_ = State::Running;
    bool active_work_ = false;
    bool owner_ready_flag_ = false;
};

} // namespace cubey::vulkan
