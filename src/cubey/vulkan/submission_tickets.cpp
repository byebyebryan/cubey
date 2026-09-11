#include <cubey/vulkan/submission_tickets.h>

#include <algorithm>
#include <exception>
#include <optional>
#include <stdexcept>
#include <utility>

namespace cubey::vulkan {

GpuSubmissionTicket GpuSubmissionTicketIssuer::issue() {
    std::scoped_lock lock(mutex_);

    current_ = GpuSubmissionTicket{.value = next_value_};
    ++next_value_;
    return current_;
}

GpuSubmissionTicket GpuSubmissionTicketIssuer::current() const {
    std::scoped_lock lock(mutex_);

    return current_;
}

void DeferredGpuDestructionQueue::defer_after(GpuSubmissionTicket ticket,
                                              std::function<void()> action) {
    if (!action) {
        throw std::runtime_error("deferred destruction action must be callable");
    }

    std::scoped_lock lock(mutex_);
    pending_.push_back({
        .ticket = ticket,
        .action = std::move(action),
    });
}

std::size_t DeferredGpuDestructionQueue::retire_completed(GpuSubmissionTicket completed) {
    std::exception_ptr first_failure;
    std::size_t retired_count = 0U;
    for (;;) {
        std::optional<std::function<void()>> action;
        {
            std::scoped_lock lock(mutex_);
            const auto found = std::find_if(
                pending_.begin(), pending_.end(),
                [completed](const PendingAction& pending) { return pending.ticket <= completed; });
            if (found == pending_.end()) {
                break;
            }
            action.emplace(std::move(found->action));
            pending_.erase(found);
        }
        try {
            action.value()();
        } catch (...) {
            if (first_failure == nullptr) {
                first_failure = std::current_exception();
            }
        }
        ++retired_count;
    }
    if (first_failure != nullptr) {
        std::rethrow_exception(first_failure);
    }
    return retired_count;
}

std::size_t DeferredGpuDestructionQueue::pending_count() const {
    std::scoped_lock lock(mutex_);

    return pending_.size();
}

bool DeferredGpuDestructionQueue::empty() const {
    std::scoped_lock lock(mutex_);

    return pending_.empty();
}

} // namespace cubey::vulkan
