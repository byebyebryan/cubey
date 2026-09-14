#include <cubey/vulkan/gpu_runtime.h>

#include <cubey/vulkan/detail/upload_step_resources.h>
#include <cubey/vulkan/staging_pool.h>

#include <cassert>
#include <future>
#include <limits>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace cubey::vulkan::detail {

class GpuRuntimeOwnerCleanupState
    : public std::enable_shared_from_this<GpuRuntimeOwnerCleanupState> {
  public:
    explicit GpuRuntimeOwnerCleanupState(GpuRuntime& runtime) : runtime_(&runtime) {}

    [[nodiscard]] std::uint64_t register_action(std::string label,
                                                std::function<void(GpuOwnerContext&)> action) {
        if (label.empty()) {
            throw std::runtime_error("GPU owner cleanup requires a label");
        }
        if (!action) {
            throw std::runtime_error("GPU owner cleanup requires an action");
        }
        std::scoped_lock lock(mutex_);
        if (phase_ != Phase::Running) {
            throw std::runtime_error("GPU runtime is shutting down");
        }
        const std::uint64_t id = next_id_++;
        actions_.emplace(id, Action{.label = std::move(label), .callback = std::move(action)});
        return id;
    }

    void request(std::uint64_t id) noexcept {
        GpuRuntime* runtime = nullptr;
        bool request_active = false;
        try {
            std::string label;
            {
                std::scoped_lock lock(mutex_);
                const auto found = actions_.find(id);
                if (found == actions_.end() || found->second.requested ||
                    phase_ != Phase::Running) {
                    return;
                }
                // Copy before publishing the request so allocation failure
                // leaves the registration untouched inside this noexcept API.
                label = found->second.label;
                found->second.requested = true;
                runtime = runtime_;
                ++active_requests_;
                request_active = true;
            }
            const std::shared_ptr<GpuRuntimeOwnerCleanupState> self = shared_from_this();
            static_cast<void>(runtime->enqueue({
                .label = std::move(label),
                .work = [self, id](GpuOwnerContext& owner) { self->invoke_on_owner(id, owner); },
            }));
        } catch (...) {
            if (request_active) {
                std::scoped_lock lock(mutex_);
                if (const auto found = actions_.find(id); found != actions_.end()) {
                    found->second.requested = false;
                }
                --active_requests_;
            }
            if (request_active) {
                idle_.notify_all();
            }
            return;
        }
        if (request_active) {
            std::scoped_lock lock(mutex_);
            --active_requests_;
            idle_.notify_all();
        }
    }

    void reset(std::uint64_t id) noexcept {
        std::scoped_lock lock(mutex_);
        actions_.erase(id);
    }

    [[nodiscard]] bool registered(std::uint64_t id) const noexcept {
        std::scoped_lock lock(mutex_);
        return actions_.contains(id);
    }

    [[nodiscard]] bool belongs_to(const GpuRuntime* runtime) const noexcept {
        std::scoped_lock lock(mutex_);
        return runtime_ == runtime && phase_ != Phase::Stopped;
    }

    void begin_shutdown() {
        std::unique_lock lock(mutex_);
        if (phase_ == Phase::Stopped) {
            return;
        }
        phase_ = Phase::Closing;
        idle_.wait(lock, [this] { return active_requests_ == 0U; });
    }

    [[nodiscard]] std::optional<std::function<void(GpuOwnerContext&)>> take_next_shutdown_action() {
        std::scoped_lock lock(mutex_);
        if (actions_.empty()) {
            return std::nullopt;
        }
        auto found = actions_.begin();
        std::function<void(GpuOwnerContext&)> action = std::move(found->second.callback);
        actions_.erase(found);
        return action;
    }

    void mark_stopped() noexcept {
        std::scoped_lock lock(mutex_);
        assert(actions_.empty());
        phase_ = Phase::Stopped;
        runtime_ = nullptr;
    }

  private:
    enum class Phase {
        Running,
        Closing,
        Stopped,
    };

    struct Action {
        std::string label{};
        std::function<void(GpuOwnerContext&)> callback{};
        bool requested = false;
    };

    void invoke_on_owner(std::uint64_t id, GpuOwnerContext& owner) {
        std::function<void(GpuOwnerContext&)> callback;
        {
            std::scoped_lock lock(mutex_);
            const auto found = actions_.find(id);
            if (found == actions_.end()) {
                return;
            }
            callback = std::move(found->second.callback);
            actions_.erase(found);
        }
        callback(owner);
    }

    mutable std::mutex mutex_;
    std::condition_variable idle_;
    GpuRuntime* runtime_ = nullptr;
    std::uint64_t next_id_ = 1U;
    std::size_t active_requests_ = 0U;
    Phase phase_ = Phase::Running;
    std::unordered_map<std::uint64_t, Action> actions_{};
};

} // namespace cubey::vulkan::detail

namespace cubey::vulkan {

GpuRuntimeOwnerCleanup::GpuRuntimeOwnerCleanup(GpuRuntimeOwnerCleanup&& other) noexcept
    : state_(std::move(other.state_)), id_(std::exchange(other.id_, 0U)) {
    other.state_.reset();
}

GpuRuntimeOwnerCleanup& GpuRuntimeOwnerCleanup::operator=(GpuRuntimeOwnerCleanup&& other) noexcept {
    if (this != &other) {
        // Replacing a live registration is an ownership handoff, not a
        // cancellation: queue its old owner cleanup before adopting `other`.
        request();
        state_ = std::move(other.state_);
        id_ = std::exchange(other.id_, 0U);
        other.state_.reset();
    }
    return *this;
}

void GpuRuntimeOwnerCleanup::request() const noexcept {
    if (const std::shared_ptr<detail::GpuRuntimeOwnerCleanupState> state = state_.lock()) {
        state->request(id_);
    }
}

void GpuRuntimeOwnerCleanup::reset() noexcept {
    if (const std::shared_ptr<detail::GpuRuntimeOwnerCleanupState> state = state_.lock()) {
        state->reset(id_);
    }
    id_ = 0U;
}

bool GpuRuntimeOwnerCleanup::registered() const noexcept {
    const std::shared_ptr<detail::GpuRuntimeOwnerCleanupState> state = state_.lock();
    return state != nullptr && state->registered(id_);
}

bool GpuRuntimeOwnerCleanup::belongs_to(const GpuRuntime& runtime) const noexcept {
    const std::shared_ptr<detail::GpuRuntimeOwnerCleanupState> state = state_.lock();
    return state != nullptr && state->belongs_to(&runtime);
}

GpuOwnerContext::GpuOwnerContext(Device& device, SubmissionCoordinator& submission,
                                 std::thread::id owner_thread, GpuRuntime* runtime)
    : GpuOwnerContext(&device, &submission, owner_thread, runtime) {}

GpuOwnerContext::GpuOwnerContext(Device* device, SubmissionCoordinator* submission,
                                 std::thread::id owner_thread, GpuRuntime* runtime)
    : device_(device), submission_(submission), owner_thread_(owner_thread), runtime_(runtime) {
    if (device_ == nullptr) {
        throw std::runtime_error("GPU owner context requires a device");
    }
    if (submission_ == nullptr) {
        throw std::runtime_error("GPU owner context requires a submission coordinator");
    }
}

Device& GpuOwnerContext::device() const {
    return *device_;
}

SubmissionCoordinator& GpuOwnerContext::submission() const {
    return *submission_;
}

GpuSubmissionTicket GpuOwnerContext::completed_submission() const {
    return submission().completed();
}

bool GpuOwnerContext::is_owner_thread() const {
    return std::this_thread::get_id() == owner_thread_;
}

void GpuOwnerContext::require_owner_thread(const char* label) const {
    if (!is_owner_thread()) {
        throw std::runtime_error(label);
    }
}

void GpuOwnerContext::defer_destruction_after(GpuSubmissionTicket ticket,
                                              std::function<void()> action) const {
    require_owner_thread("GPU deferred destruction requires the GPU owner thread");
    if (runtime_ == nullptr) {
        throw std::runtime_error("GPU owner context has no runtime for deferred destruction");
    }
    runtime_->defer_destruction_after_on_owner_thread(ticket, std::move(action));
}

void GpuOwnerContext::complete_submission(GpuSubmissionTicket ticket) const {
    require_owner_thread("GPU submission completion requires the GPU owner thread");
    if (runtime_ == nullptr) {
        throw std::runtime_error("GPU owner context has no runtime for completion");
    }
    runtime_->complete_submission_on_owner_thread(ticket);
}

GpuStagingPool& GpuOwnerContext::staging_pool() const {
    require_owner_thread("GPU staging pool access requires the GPU owner thread");
    if (runtime_ == nullptr || runtime_->staging_pool_ == nullptr) {
        throw std::runtime_error("GPU runtime has no configured staging pool");
    }
    return *runtime_->staging_pool_;
}

GpuRuntime& GpuOwnerContext::runtime() const {
    require_owner_thread("GPU runtime access requires the GPU owner thread");
    if (runtime_ == nullptr) {
        throw std::runtime_error("GPU owner context has no runtime");
    }
    return *runtime_;
}

GpuWorkTicket GpuWorkQueue::enqueue(GpuWorkRequest request) {
    if (!request.work) {
        throw std::runtime_error("GPU work request requires a callback");
    }

    std::scoped_lock lock(mutex_);
    GpuWorkTicket ticket{
        .id = next_id_,
        .label = std::move(request.label),
    };
    ++next_id_;

    work_.push_back({
        .ticket = ticket,
        .work = std::move(request.work),
    });
    return ticket;
}

std::vector<QueuedGpuWork> GpuWorkQueue::drain() {
    std::scoped_lock lock(mutex_);

    std::vector<QueuedGpuWork> work;
    work.swap(work_);
    return work;
}

std::size_t GpuWorkQueue::pending_count() const {
    std::scoped_lock lock(mutex_);

    return work_.size();
}

bool GpuWorkQueue::empty() const {
    std::scoped_lock lock(mutex_);

    return work_.empty();
}

void GpuWorkQueue::restore_front(std::vector<QueuedGpuWork> work) {
    if (work.empty()) {
        return;
    }

    std::scoped_lock lock(mutex_);
    work.insert(work.end(), std::make_move_iterator(work_.begin()),
                std::make_move_iterator(work_.end()));
    work_ = std::move(work);
}

GpuRuntime::GpuRuntime(GpuRuntimeConfig config)
    : device_(config.device), submission_(config.submission),
      execution_mode_(config.execution_mode), owner_thread_(std::this_thread::get_id()) {
    if (device_ == nullptr) {
        throw std::runtime_error("GPU runtime requires a device");
    }
    if (submission_ == nullptr) {
        throw std::runtime_error("GPU runtime requires a submission coordinator");
    }
    owner_cleanup_state_ = std::make_shared<detail::GpuRuntimeOwnerCleanupState>(*this);
    last_drain_result_.completed_submission = submission_->completed();
    if (config.staging_pool.has_value()) {
        staging_pool_.reset(new GpuStagingPool(config.staging_pool.value()));
    }
    if (execution_mode_ == GpuRuntimeExecutionMode::Threaded) {
        owner_thread_ = {};
        start_threaded_owner();
    }
    if (staging_pool_ != nullptr) {
        try {
            if (execution_mode_ == GpuRuntimeExecutionMode::Inline) {
                GpuOwnerContext context = owner_context();
                staging_pool_->initialize(context);
            } else {
                static_cast<void>(submit_and_wait({
                    .label = "initialize GPU staging pool",
                    .work =
                        [](GpuOwnerContext& context) {
                            context.staging_pool().initialize(context);
                        },
                }));
            }
        } catch (...) {
            try {
                shutdown();
            } catch (...) {
            }
            throw;
        }
    }
}

GpuRuntime::GpuRuntime(Device& device, SubmissionCoordinator& submission)
    : GpuRuntime({.device = &device, .submission = &submission}) {}

GpuRuntime::~GpuRuntime() {
    try {
        shutdown();
    } catch (...) {
    }
}

GpuWorkTicket GpuRuntime::enqueue(GpuWorkRequest request) {
    GpuWorkTicket ticket;
    {
        std::scoped_lock lock(state_mutex_);
        if (state_ != State::Running) {
            throw std::runtime_error("GPU runtime is shut down");
        }
        ticket = queue_.enqueue(std::move(request));
    }
    if (execution_mode_ == GpuRuntimeExecutionMode::Threaded) {
        work_available_.notify_one();
    }
    return ticket;
}

GpuWorkTicket GpuRuntime::submit_and_wait(GpuWorkRequest request) {
    if (!request.work) {
        throw std::runtime_error("GPU work request requires a callback");
    }

    auto finished = std::make_shared<std::promise<void>>();
    std::future<void> future = finished->get_future();
    std::function<void(GpuOwnerContext&)> work = std::move(request.work);
    request.work = [work = std::move(work), finished](GpuOwnerContext& context) mutable {
        try {
            work(context);
            finished->set_value();
        } catch (...) {
            finished->set_exception(std::current_exception());
        }
    };

    GpuWorkTicket ticket = enqueue(std::move(request));
    if (execution_mode_ == GpuRuntimeExecutionMode::Inline) {
        static_cast<void>(drain_inline());
    } else if (std::this_thread::get_id() == owner_thread_) {
        const GpuDrainResult result = drain_on_owner_thread();
        std::scoped_lock lock(state_mutex_);
        last_drain_result_ = result;
    } else {
        future.wait();
    }
    future.get();
    return ticket;
}

GpuDrainResult GpuRuntime::drain() {
    if (execution_mode_ == GpuRuntimeExecutionMode::Inline) {
        return drain_inline();
    }
    wait_until_idle();
    std::scoped_lock lock(state_mutex_);
    return last_drain_result_;
}

GpuDrainResult GpuRuntime::drain_inline() {
    require_owner_thread("GPU runtime inline drain requires the owner thread");
    return drain_on_owner_thread();
}

std::size_t GpuRuntime::pending_count() const {
    return queue_.pending_count();
}

bool GpuRuntime::empty() const {
    return queue_.empty();
}

void GpuRuntime::defer_destruction_after(GpuSubmissionTicket ticket, std::function<void()> action) {
    if (!action) {
        throw std::runtime_error("deferred GPU destruction action must be callable");
    }
    static_cast<void>(submit_and_wait({
        .label = "defer GPU resource destruction",
        .work =
            [this, ticket, action = std::move(action)](GpuOwnerContext& context) mutable {
                if (context.submission().last_submitted() < ticket) {
                    throw std::runtime_error(
                        "deferred GPU destruction ticket has not been submitted");
                }
                deferred_destruction_.defer_after(ticket, std::move(action));
                static_cast<void>(
                    deferred_destruction_.retire_completed(context.completed_submission()));
            },
    }));
}

std::size_t GpuRuntime::deferred_destruction_count() const {
    return deferred_destruction_.pending_count();
}

void GpuRuntime::mark_submission_completed(GpuSubmissionTicket ticket) {
    static_cast<void>(submit_and_wait({
        .label = "mark GPU submission completed",
        .work =
            [this, ticket](GpuOwnerContext& context) {
                context.submission().mark_completed(ticket);
                static_cast<void>(deferred_destruction_.retire_completed(ticket));
            },
    }));
}

void GpuRuntime::wait_queue_idle(std::string label) {
    if (label.empty()) {
        label = "GPU queue idle";
    }
    static_cast<void>(submit_and_wait({
        .label = label,
        .work =
            [this, wait_label = std::move(label)](GpuOwnerContext& context) {
                context.submission().wait_idle(wait_label.c_str());
                const GpuSubmissionTicket latest = context.submission().last_submitted();
                context.submission().mark_completed(latest);
                static_cast<void>(deferred_destruction_.retire_completed(latest));
            },
    }));
}

void GpuRuntime::wait_until_idle() {
    if (execution_mode_ == GpuRuntimeExecutionMode::Inline) {
        static_cast<void>(drain_inline());
        return;
    }
    if (std::this_thread::get_id() == owner_thread_) {
        throw std::runtime_error("GPU runtime owner thread cannot wait for itself");
    }

    {
        std::unique_lock lock(state_mutex_);
        idle_.wait(lock, [this] { return !active_work_ && queue_.empty(); });
    }
    rethrow_threaded_failure_if_any();
}

void GpuRuntime::shutdown() {
    {
        std::scoped_lock lock(state_mutex_);
        if (state_ == State::Stopped) {
            return;
        }
    }
    if (std::this_thread::get_id() == owner_thread_ &&
        execution_mode_ == GpuRuntimeExecutionMode::Threaded) {
        throw std::runtime_error("GPU runtime owner thread cannot shut itself down");
    }
    if (execution_mode_ == GpuRuntimeExecutionMode::Inline) {
        require_owner_thread("inline GPU runtime shutdown requires the owner thread");
    }

    std::exception_ptr failure;
    {
        std::unique_lock lock(state_mutex_);
        if (state_ != State::Running) {
            if (execution_mode_ == GpuRuntimeExecutionMode::Inline && state_ == State::Closing &&
                std::this_thread::get_id() == owner_thread_) {
                // The outer inline shutdown is currently draining this owner
                // callback or cleanup action. Waiting for Stopped here would
                // deadlock that outer teardown, so re-entry is idempotent.
                return;
            }
            idle_.wait(lock, [this] { return state_ == State::Stopped; });
            return;
        }
        state_ = State::Closing;
    }
    owner_cleanup_state_->begin_shutdown();

    {
        std::scoped_lock lock(state_mutex_);
        shutdown_barrier_requested_ = true;
        shutdown_barrier_finished_ = false;
        shutdown_barrier_failure_ = nullptr;
    }
    if (execution_mode_ == GpuRuntimeExecutionMode::Inline) {
        // Inline mode has no owner thread to observe the stateful barrier.
        // Drain already-admitted callbacks first, matching the threaded
        // ordering before the owner performs queue-idle and cleanup.
        while (!queue_.empty()) {
            try {
                static_cast<void>(drain_inline());
            } catch (...) {
                if (failure == nullptr) {
                    failure = std::current_exception();
                }
            }
        }
        run_shutdown_barrier_on_owner_thread();
    } else {
        // The owner observes this stateful barrier after previously admitted
        // work drains. Unlike queueing a promise callback, this cannot leave
        // Closing stuck if allocation or queue admission fails.
        work_available_.notify_one();
        std::unique_lock lock(state_mutex_);
        idle_.wait(lock, [this] { return shutdown_barrier_finished_; });
    }
    {
        std::scoped_lock lock(state_mutex_);
        if (failure == nullptr) {
            failure = shutdown_barrier_failure_;
        }
    }

    if (execution_mode_ == GpuRuntimeExecutionMode::Threaded) {
        std::unique_lock lock(state_mutex_);
        idle_.wait(lock, [this] { return !active_work_ && queue_.empty(); });
    }

    try {
        rethrow_threaded_failure_if_any();
    } catch (...) {
        if (failure == nullptr) {
            failure = std::current_exception();
        }
    }

    {
        std::scoped_lock lock(state_mutex_);
        state_ =
            execution_mode_ == GpuRuntimeExecutionMode::Threaded ? State::Stopping : State::Stopped;
    }
    work_available_.notify_all();
    if (owner_thread_handle_.joinable()) {
        owner_thread_handle_.join();
    }
    {
        std::scoped_lock lock(state_mutex_);
        state_ = State::Stopped;
    }
    owner_cleanup_state_->mark_stopped();
    idle_.notify_all();
    if (failure != nullptr) {
        std::rethrow_exception(failure);
    }
}

GpuOwnerContext GpuRuntime::owner_context() {
    return {device_, submission_, owner_thread_, this};
}

bool GpuRuntime::has_staging_pool() const noexcept {
    return staging_pool_ != nullptr;
}

GpuRuntimeOwnerCleanup
GpuRuntime::register_owner_cleanup(std::string label,
                                   std::function<void(GpuOwnerContext&)> action) {
    const std::uint64_t id =
        owner_cleanup_state_->register_action(std::move(label), std::move(action));
    return GpuRuntimeOwnerCleanup(owner_cleanup_state_, id);
}

void GpuRuntime::require_owner_thread(const char* label) const {
    if (std::this_thread::get_id() != owner_thread_) {
        throw std::runtime_error(label);
    }
}

void GpuRuntime::start_threaded_owner() {
    owner_thread_handle_ = std::thread([this] { run_threaded_owner(); });
    std::unique_lock lock(state_mutex_);
    owner_ready_.wait(lock, [this] { return owner_ready_flag_; });
}

void GpuRuntime::run_threaded_owner() {
    {
        std::scoped_lock lock(state_mutex_);
        owner_thread_ = std::this_thread::get_id();
        owner_ready_flag_ = true;
    }
    owner_ready_.notify_all();

    while (true) {
        bool run_shutdown_barrier = false;
        {
            std::unique_lock lock(state_mutex_);
            work_available_.wait(lock, [this] {
                return state_ == State::Stopping || !queue_.empty() ||
                       (state_ == State::Closing && shutdown_barrier_requested_ &&
                        !shutdown_barrier_finished_);
            });
            if (state_ == State::Stopping && queue_.empty()) {
                return;
            }
            run_shutdown_barrier = state_ == State::Closing && shutdown_barrier_requested_ &&
                                   !shutdown_barrier_finished_ && queue_.empty();
            active_work_ = true;
        }

        try {
            GpuDrainResult result{};
            if (run_shutdown_barrier) {
                run_shutdown_barrier_on_owner_thread();
            } else {
                result = drain_on_owner_thread();
            }
            std::scoped_lock lock(state_mutex_);
            if (!run_shutdown_barrier) {
                last_drain_result_ = result;
            }
            active_work_ = false;
        } catch (...) {
            record_threaded_failure(std::current_exception());
            std::scoped_lock lock(state_mutex_);
            active_work_ = false;
        }
        idle_.notify_all();
    }
}

void GpuRuntime::run_shutdown_barrier_on_owner_thread() noexcept {
    require_owner_thread("GPU runtime shutdown barrier requires the owner thread");
    GpuOwnerContext context = owner_context();
    const auto run_retained_cleanups = [&] {
        std::exception_ptr first_failure;
        for (;;) {
            std::optional<std::function<void(GpuOwnerContext&)>> action =
                owner_cleanup_state_->take_next_shutdown_action();
            if (!action.has_value()) {
                break;
            }
            try {
                action.value()(context);
            } catch (...) {
                if (first_failure == nullptr) {
                    first_failure = std::current_exception();
                }
            }
        }
        if (first_failure != nullptr) {
            std::rethrow_exception(first_failure);
        }
    };

    std::exception_ptr failure;
    bool staging_pool_shutdown_attempted = false;
    try {
        context.submission().wait_idle("GPU runtime shutdown");
        const GpuSubmissionTicket latest = context.submission().last_submitted();
        context.submission().mark_completed(latest);
        static_cast<void>(collect_retired_on_owner_thread());
        run_retained_cleanups();
        if (staging_pool_ != nullptr) {
            staging_pool_shutdown_attempted = true;
            staging_pool_->shutdown_on_owner_thread(context);
        }
        idle_upload_step_resources_.clear();
    } catch (...) {
        failure = std::current_exception();
        // A failed queue-idle wait is a fatal shutdown path, but it must still
        // dispose retained session state on this owner. Force-retiring the
        // deferred queue is the best available owner-only teardown boundary;
        // no callback can reach mark_stopped() on the caller thread.
        try {
            run_retained_cleanups();
        } catch (...) {
        }
        try {
            static_cast<void>(deferred_destruction_.retire_completed(
                {.value = std::numeric_limits<std::uint64_t>::max()}));
        } catch (...) {
        }
        if (staging_pool_ != nullptr && !staging_pool_shutdown_attempted) {
            try {
                staging_pool_->shutdown_on_owner_thread(context);
            } catch (...) {
            }
        }
        idle_upload_step_resources_.clear();
    }

    {
        std::scoped_lock lock(state_mutex_);
        shutdown_barrier_failure_ = failure;
        shutdown_barrier_finished_ = true;
    }
    idle_.notify_all();
}

GpuDrainResult GpuRuntime::drain_on_owner_thread() {

    GpuDrainResult result{
        .completed_count = 0,
        .last_completed = {},
        .completed_submission = submission_->completed(),
    };
    std::vector<QueuedGpuWork> work = queue_.drain();
    GpuOwnerContext context = owner_context();

    for (std::size_t index = 0; index < work.size(); ++index) {
        std::exception_ptr failure;
        try {
            work[index].work(context);
        } catch (...) {
            failure = std::current_exception();
        }
        try {
            static_cast<void>(collect_retired_on_owner_thread());
        } catch (...) {
            if (failure == nullptr) {
                failure = std::current_exception();
            }
        }
        if (failure != nullptr) {
            std::vector<QueuedGpuWork> remaining;
            remaining.reserve(work.size() - index - 1U);
            for (std::size_t remaining_index = index + 1U; remaining_index < work.size();
                 ++remaining_index) {
                remaining.push_back(std::move(work[remaining_index]));
            }
            queue_.restore_front(std::move(remaining));
            std::rethrow_exception(failure);
        }

        ++result.completed_count;
        result.last_completed = work[index].ticket;
        result.completed_submission = submission_->completed();
    }

    return result;
}

std::size_t GpuRuntime::collect_retired_on_owner_thread() {
    require_owner_thread("GPU retirement requires the owner thread");
    std::size_t retired = deferred_destruction_.retire_completed(submission_->completed());
    if (staging_pool_ != nullptr) {
        GpuOwnerContext context = owner_context();
        retired += staging_pool_->reclaim(context);
    }
    return retired;
}

void GpuRuntime::defer_destruction_after_on_owner_thread(GpuSubmissionTicket ticket,
                                                         std::function<void()> action) {
    require_owner_thread("GPU deferred destruction requires the owner thread");
    if (submission_->last_submitted() < ticket) {
        throw std::runtime_error("deferred GPU destruction ticket has not been submitted");
    }
    deferred_destruction_.defer_after(ticket, std::move(action));
    static_cast<void>(collect_retired_on_owner_thread());
}

void GpuRuntime::complete_submission_on_owner_thread(GpuSubmissionTicket ticket) {
    require_owner_thread("GPU submission completion requires the owner thread");
    submission_->mark_completed(ticket);
    static_cast<void>(collect_retired_on_owner_thread());
}

std::unique_ptr<GpuUploadStepResources>
GpuRuntime::acquire_upload_step_resources_on_owner_thread() {
    require_owner_thread("GPU upload step resource acquisition requires the GPU owner thread");
    std::unique_ptr<GpuUploadStepResources> resources;
    if (!idle_upload_step_resources_.empty()) {
        resources = std::move(idle_upload_step_resources_.back());
        idle_upload_step_resources_.pop_back();
    } else {
        resources = std::make_unique<GpuUploadStepResources>(*device_);
    }
    resources->begin_recording();
    return resources;
}

void GpuRuntime::recycle_upload_step_resources_on_owner_thread(
    std::unique_ptr<GpuUploadStepResources> resources) {
    require_owner_thread("GPU upload step resource recycle requires the GPU owner thread");
    if (resources != nullptr) {
        idle_upload_step_resources_.push_back(std::move(resources));
    }
}

void GpuRuntime::record_threaded_failure(std::exception_ptr failure) {
    std::scoped_lock lock(state_mutex_);
    if (threaded_failure_ == nullptr) {
        threaded_failure_ = failure;
    }
}

void GpuRuntime::rethrow_threaded_failure_if_any() {
    std::exception_ptr failure;
    {
        std::scoped_lock lock(state_mutex_);
        failure = std::exchange(threaded_failure_, nullptr);
    }
    if (failure != nullptr) {
        std::rethrow_exception(failure);
    }
}

} // namespace cubey::vulkan
