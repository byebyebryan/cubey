#include <cubey/vulkan/gpu_runtime.h>

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

FrameTicket GpuOwnerContext::completed_submission() const {
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
      owner_thread_(std::this_thread::get_id()) {
    if (device_ == nullptr) {
        throw std::runtime_error("GPU runtime requires a device");
    }
    if (submission_ == nullptr) {
        throw std::runtime_error("GPU runtime requires a submission coordinator");
    }
}

GpuRuntime::GpuRuntime(Device& device, SubmissionCoordinator& submission)
    : GpuRuntime({.device = &device, .submission = &submission}) {}

GpuWorkTicket GpuRuntime::enqueue(GpuWorkRequest request) {
    return queue_.enqueue(std::move(request));
}

GpuDrainResult GpuRuntime::drain_inline() {
    require_owner_thread("GPU runtime inline drain requires the owner thread");

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

std::size_t GpuRuntime::pending_count() const {
    return queue_.pending_count();
}

bool GpuRuntime::empty() const {
    return queue_.empty();
}

GpuOwnerContext GpuRuntime::owner_context() const {
    return {*device_, *submission_, owner_thread_};
}

void GpuRuntime::require_owner_thread(const char* label) const {
    if (std::this_thread::get_id() != owner_thread_) {
        throw std::runtime_error(label);
    }
}

} // namespace cubey::vulkan
