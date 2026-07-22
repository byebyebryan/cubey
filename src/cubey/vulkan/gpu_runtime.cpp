#include <cubey/vulkan/gpu_runtime.h>

#include <future>
#include <stdexcept>
#include <utility>

namespace cubey::vulkan {

GpuOwnerContext::GpuOwnerContext(Device& device, SubmissionCoordinator& submission,
                                 std::thread::id owner_thread)
    : device_(&device), submission_(&submission), owner_thread_(owner_thread) {}

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
    last_drain_result_.completed_submission = submission_->completed();
    if (execution_mode_ == GpuRuntimeExecutionMode::Threaded) {
        owner_thread_ = {};
        start_threaded_owner();
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
        if (!accepting_work_) {
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

    {
        std::unique_lock lock(state_mutex_);
        idle_.wait(lock, [this] { return !active_work_ && queue_.empty(); });
    }
    rethrow_threaded_failure_if_any();
}

void GpuRuntime::shutdown() {
    {
        std::scoped_lock lock(state_mutex_);
        if (shutdown_complete_) {
            return;
        }
    }
    wait_queue_idle("GPU runtime shutdown");

    if (execution_mode_ == GpuRuntimeExecutionMode::Inline) {
        {
            std::scoped_lock lock(state_mutex_);
            accepting_work_ = false;
        }
        static_cast<void>(drain_inline());
        std::scoped_lock lock(state_mutex_);
        stopping_ = true;
        shutdown_complete_ = true;
        return;
    }

    std::exception_ptr failure;
    {
        std::scoped_lock lock(state_mutex_);
        accepting_work_ = false;
    }

    try {
        wait_until_idle();
    } catch (...) {
        failure = std::current_exception();
    }

    {
        std::scoped_lock lock(state_mutex_);
        stopping_ = true;
    }
    work_available_.notify_all();
    if (owner_thread_handle_.joinable()) {
        owner_thread_handle_.join();
    }
    {
        std::scoped_lock lock(state_mutex_);
        shutdown_complete_ = true;
    }
    if (failure != nullptr) {
        std::rethrow_exception(failure);
    }
}

GpuOwnerContext GpuRuntime::owner_context() const {
    return {*device_, *submission_, owner_thread_};
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
        {
            std::unique_lock lock(state_mutex_);
            work_available_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
            if (stopping_ && queue_.empty()) {
                return;
            }
            active_work_ = true;
        }

        try {
            const GpuDrainResult result = drain_on_owner_thread();
            std::scoped_lock lock(state_mutex_);
            last_drain_result_ = result;
            active_work_ = false;
        } catch (...) {
            record_threaded_failure(std::current_exception());
            std::scoped_lock lock(state_mutex_);
            active_work_ = false;
        }
        idle_.notify_all();
    }
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
        try {
            work[index].work(context);
        } catch (...) {
            std::vector<QueuedGpuWork> remaining;
            remaining.reserve(work.size() - index - 1U);
            for (std::size_t remaining_index = index + 1U; remaining_index < work.size();
                 ++remaining_index) {
                remaining.push_back(std::move(work[remaining_index]));
            }
            queue_.restore_front(std::move(remaining));
            throw;
        }

        ++result.completed_count;
        result.last_completed = work[index].ticket;
        result.completed_submission = submission_->completed();
    }

    return result;
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
