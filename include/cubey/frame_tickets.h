#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <vector>

namespace cubey {

struct FrameTicket {
    std::uint64_t value = 0;
};

[[nodiscard]] constexpr bool operator==(FrameTicket lhs, FrameTicket rhs) {
    return lhs.value == rhs.value;
}

[[nodiscard]] constexpr bool operator<(FrameTicket lhs, FrameTicket rhs) {
    return lhs.value < rhs.value;
}

[[nodiscard]] constexpr bool operator<=(FrameTicket lhs, FrameTicket rhs) {
    return lhs.value <= rhs.value;
}

class FrameTicketIssuer {
public:
    [[nodiscard]] FrameTicket issue();
    [[nodiscard]] FrameTicket current() const;

private:
    mutable std::mutex mutex_;
    std::uint64_t next_value_ = 1;
    FrameTicket current_{};
};

class DeferredDestructionQueue {
public:
    void defer_after(FrameTicket ticket, std::function<void()> action);
    [[nodiscard]] std::size_t retire_completed(FrameTicket completed);
    [[nodiscard]] std::size_t pending_count() const;
    [[nodiscard]] bool empty() const;

private:
    struct PendingAction {
        FrameTicket ticket;
        std::function<void()> action;
    };

    mutable std::mutex mutex_;
    std::vector<PendingAction> pending_;
};

} // namespace cubey
