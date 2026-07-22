#include <cubey/vulkan/submission_tickets.h>

#include <exception>
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
    std::vector<std::function<void()>> ready_actions;
    {
        std::scoped_lock lock(mutex_);
        std::vector<PendingAction> still_pending;
        still_pending.reserve(pending_.size());

        for (PendingAction& pending : pending_) {
            if (pending.ticket <= completed) {
                ready_actions.push_back(std::move(pending.action));
            } else {
                still_pending.push_back(std::move(pending));
            }
        }

        pending_ = std::move(still_pending);
    }

    std::exception_ptr first_failure;
    for (const std::function<void()>& action : ready_actions) {
        try {
            action();
        } catch (...) {
            if (first_failure == nullptr) {
                first_failure = std::current_exception();
            }
        }
    }
    if (first_failure != nullptr) {
        std::rethrow_exception(first_failure);
    }
    return ready_actions.size();
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
