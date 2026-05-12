#include <cubey/core/frame_tickets.h>

#include <stdexcept>
#include <utility>

namespace cubey {

FrameTicket FrameTicketIssuer::issue() {
    std::scoped_lock lock(mutex_);

    current_ = FrameTicket{.value = next_value_};
    ++next_value_;
    return current_;
}

FrameTicket FrameTicketIssuer::current() const {
    std::scoped_lock lock(mutex_);

    return current_;
}

void DeferredDestructionQueue::defer_after(FrameTicket ticket, std::function<void()> action) {
    if (!action) {
        throw std::runtime_error("deferred destruction action must be callable");
    }

    std::scoped_lock lock(mutex_);
    pending_.push_back({
        .ticket = ticket,
        .action = std::move(action),
    });
}

std::size_t DeferredDestructionQueue::retire_completed(FrameTicket completed) {
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

    for (const std::function<void()>& action : ready_actions) {
        action();
    }
    return ready_actions.size();
}

std::size_t DeferredDestructionQueue::pending_count() const {
    std::scoped_lock lock(mutex_);

    return pending_.size();
}

bool DeferredDestructionQueue::empty() const {
    std::scoped_lock lock(mutex_);

    return pending_.empty();
}

} // namespace cubey
