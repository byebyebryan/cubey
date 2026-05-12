#pragma once

#include <cubey/core/frame_tickets.h>
#include <cubey/vulkan/device.h>
#include <cubey/vulkan/queue_submit.h>

#include <vulkan/vulkan.h>

#include <functional>

namespace cubey::vulkan {

class SubmissionCoordinator {
  public:
    using SubmitFunction = std::function<void(VkQueue, const QueueSubmitInfo&, const char*)>;
    using WaitFunction = std::function<void(VkQueue, const char*)>;

    explicit SubmissionCoordinator(const Device& device);
    explicit SubmissionCoordinator(VkQueue queue);
    SubmissionCoordinator(VkQueue queue, SubmitFunction submit, WaitFunction wait);

    SubmissionCoordinator(const SubmissionCoordinator&) = delete;
    SubmissionCoordinator& operator=(const SubmissionCoordinator&) = delete;

    [[nodiscard]] FrameTicket submit(const QueueSubmitInfo& submit_info, const char* label);
    [[nodiscard]] FrameTicket submit_and_wait(const QueueSubmitInfo& submit_info,
                                              const char* submit_label, const char* wait_label);
    void wait_idle(const char* label) const;
    void mark_completed(FrameTicket ticket);

    [[nodiscard]] FrameTicket last_submitted() const noexcept {
        return last_submitted_;
    }
    [[nodiscard]] FrameTicket completed() const noexcept {
        return completed_;
    }

  private:
    VkQueue queue_ = VK_NULL_HANDLE;
    SubmitFunction submit_;
    WaitFunction wait_;
    FrameTicketIssuer tickets_;
    FrameTicket last_submitted_{};
    FrameTicket completed_{};
};

} // namespace cubey::vulkan
