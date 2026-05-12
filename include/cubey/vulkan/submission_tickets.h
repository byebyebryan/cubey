#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <vector>

namespace cubey::vulkan {

struct GpuSubmissionTicket {
    std::uint64_t value = 0;
};

[[nodiscard]] constexpr bool operator==(GpuSubmissionTicket lhs, GpuSubmissionTicket rhs) {
    return lhs.value == rhs.value;
}

[[nodiscard]] constexpr bool operator<(GpuSubmissionTicket lhs, GpuSubmissionTicket rhs) {
    return lhs.value < rhs.value;
}

[[nodiscard]] constexpr bool operator<=(GpuSubmissionTicket lhs, GpuSubmissionTicket rhs) {
    return lhs.value <= rhs.value;
}

class GpuSubmissionTicketIssuer {
  public:
    [[nodiscard]] GpuSubmissionTicket issue();
    [[nodiscard]] GpuSubmissionTicket current() const;

  private:
    mutable std::mutex mutex_;
    std::uint64_t next_value_ = 1;
    GpuSubmissionTicket current_{};
};

class DeferredGpuDestructionQueue {
  public:
    void defer_after(GpuSubmissionTicket ticket, std::function<void()> action);
    [[nodiscard]] std::size_t retire_completed(GpuSubmissionTicket completed);
    [[nodiscard]] std::size_t pending_count() const;
    [[nodiscard]] bool empty() const;

  private:
    struct PendingAction {
        GpuSubmissionTicket ticket;
        std::function<void()> action;
    };

    mutable std::mutex mutex_;
    std::vector<PendingAction> pending_;
};

} // namespace cubey::vulkan
