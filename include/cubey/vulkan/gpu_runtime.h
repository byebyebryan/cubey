#pragma once

#include <cubey/vulkan/device.h>
#include <cubey/vulkan/staging_pool_config.h>
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
#include <optional>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace cubey::vulkan {

class GpuRuntime;
class GpuStagingPool;
class GpuUploadStepResources;
class GpuUploadStepState;
namespace detail {
class GpuRuntimeOwnerCleanupState;
}

class GpuOwnerContext {
  public:
    GpuOwnerContext(Device& device, SubmissionCoordinator& submission, std::thread::id owner_thread,
                    GpuRuntime* runtime = nullptr);

    [[nodiscard]] Device& device() const;
    [[nodiscard]] SubmissionCoordinator& submission() const;
    [[nodiscard]] GpuSubmissionTicket completed_submission() const;
    [[nodiscard]] bool is_owner_thread() const;
    void require_owner_thread(const char* label) const;
    void defer_destruction_after(GpuSubmissionTicket ticket, std::function<void()> action) const;
    void complete_submission(GpuSubmissionTicket ticket) const;
    [[nodiscard]] GpuStagingPool& staging_pool() const;
    [[nodiscard]] GpuRuntime& runtime() const;

  private:
    friend class GpuRuntime;

    GpuOwnerContext(Device* device, SubmissionCoordinator* submission, std::thread::id owner_thread,
                    GpuRuntime* runtime);

    Device* device_ = nullptr;
    SubmissionCoordinator* submission_ = nullptr;
    std::thread::id owner_thread_{};
    GpuRuntime* runtime_ = nullptr;
};

struct GpuWorkTicket {
    std::uint64_t id = 0;
    std::string label;
};

template <typename T> class GpuJobHandle {
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

// A runtime-bound registration for GPU-owner cleanup. While registered, the
// runtime retains the cleanup action through shutdown; request() either queues
// it before shutdown or leaves it for the shutdown owner barrier. This lets
// asynchronous products safely outlive ordinary caller scopes without
// retaining or dereferencing a raw GpuRuntime pointer from their destructors.
class GpuRuntimeOwnerCleanup {
  public:
    GpuRuntimeOwnerCleanup() = default;
    ~GpuRuntimeOwnerCleanup() = default;

    GpuRuntimeOwnerCleanup(const GpuRuntimeOwnerCleanup&) = delete;
    GpuRuntimeOwnerCleanup& operator=(const GpuRuntimeOwnerCleanup&) = delete;
    GpuRuntimeOwnerCleanup(GpuRuntimeOwnerCleanup&& other) noexcept;
    GpuRuntimeOwnerCleanup& operator=(GpuRuntimeOwnerCleanup&& other) noexcept;

    // Nonblocking and noexcept: a runtime already closing will run the
    // registered action from its owner shutdown barrier instead. Move
    // assignment requests any existing cleanup before adopting the incoming
    // registration, so replacing a live token never cancels its retirement.
    void request() const noexcept;
    void reset() noexcept;
    [[nodiscard]] bool registered() const noexcept;
    [[nodiscard]] bool belongs_to(const GpuRuntime& runtime) const noexcept;

  private:
    friend class GpuRuntime;
    GpuRuntimeOwnerCleanup(std::weak_ptr<detail::GpuRuntimeOwnerCleanupState> state,
                           std::uint64_t id)
        : state_(std::move(state)), id_(id) {}

    std::weak_ptr<detail::GpuRuntimeOwnerCleanupState> state_{};
    std::uint64_t id_ = 0;
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
    // Tests and existing callers remain pool-free until a host opts in. When
    // present, the runtime preallocates and persistently maps the initial
    // block on its GPU owner before construction returns.
    std::optional<GpuStagingPoolConfig> staging_pool = std::nullopt;
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
    [[nodiscard]] auto submit(std::string label, Function&& function) -> GpuJobHandle<
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
    // Calling shutdown from an already-admitted inline owner callback while
    // this runtime is Closing is idempotent; the outer shutdown owns teardown.
    void shutdown();

    [[nodiscard]] GpuOwnerContext owner_context();
    // A session-level capability query. glTF residency deliberately requires
    // the persistent staging pool rather than falling back to one-shot uploads.
    [[nodiscard]] bool has_staging_pool() const noexcept;
    // Registers an owner-only cleanup action retained until it either runs or
    // is reset. Intended for long-lived asynchronous GPU products.
    [[nodiscard]] GpuRuntimeOwnerCleanup
    register_owner_cleanup(std::string label, std::function<void(GpuOwnerContext&)> action);
    [[nodiscard]] GpuRuntimeExecutionMode execution_mode() const noexcept {
        return execution_mode_;
    }
    void require_owner_thread(const char* label) const;

  private:
    friend class GpuOwnerContext;
    friend class GpuUploadStepState;

    enum class State {
        Running,
        Closing,
        Stopping,
        Stopped,
    };

    void start_threaded_owner();
    void run_threaded_owner();
    void run_shutdown_barrier_on_owner_thread() noexcept;
    [[nodiscard]] GpuDrainResult drain_on_owner_thread();
    [[nodiscard]] std::size_t collect_retired_on_owner_thread();
    void defer_destruction_after_on_owner_thread(GpuSubmissionTicket ticket,
                                                 std::function<void()> action);
    void complete_submission_on_owner_thread(GpuSubmissionTicket ticket);
    [[nodiscard]] std::unique_ptr<GpuUploadStepResources>
    acquire_upload_step_resources_on_owner_thread();
    void recycle_upload_step_resources_on_owner_thread(
        std::unique_ptr<GpuUploadStepResources> resources);
    void record_threaded_failure(std::exception_ptr failure);
    void rethrow_threaded_failure_if_any();
    Device* device_ = nullptr;
    SubmissionCoordinator* submission_ = nullptr;
    GpuRuntimeExecutionMode execution_mode_ = GpuRuntimeExecutionMode::Threaded;
    std::thread::id owner_thread_{};
    GpuWorkQueue queue_;
    DeferredGpuDestructionQueue deferred_destruction_;
    std::unique_ptr<GpuStagingPool> staging_pool_{};
    std::shared_ptr<detail::GpuRuntimeOwnerCleanupState> owner_cleanup_state_{};
    std::vector<std::unique_ptr<GpuUploadStepResources>> idle_upload_step_resources_{};
    std::thread owner_thread_handle_;
    mutable std::mutex state_mutex_;
    std::condition_variable work_available_;
    std::condition_variable idle_;
    std::condition_variable owner_ready_;
    GpuDrainResult last_drain_result_{};
    std::exception_ptr threaded_failure_;
    std::exception_ptr shutdown_barrier_failure_;
    State state_ = State::Running;
    bool active_work_ = false;
    bool owner_ready_flag_ = false;
    bool shutdown_barrier_requested_ = false;
    bool shutdown_barrier_finished_ = false;
};

} // namespace cubey::vulkan
