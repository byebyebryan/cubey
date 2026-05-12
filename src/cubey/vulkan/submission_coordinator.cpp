#include <cubey/vulkan/submission_coordinator.h>

#include <stdexcept>
#include <utility>

namespace cubey::vulkan {

SubmissionCoordinator::SubmissionCoordinator(const Device& device)
    : SubmissionCoordinator(device.queue()) {}

SubmissionCoordinator::SubmissionCoordinator(VkQueue queue)
    : SubmissionCoordinator(queue, submit_to_queue, wait_for_queue_idle) {}

SubmissionCoordinator::SubmissionCoordinator(VkQueue queue, SubmitFunction submit,
                                             WaitFunction wait)
    : queue_(queue), submit_(std::move(submit)), wait_(std::move(wait)) {
    if (queue_ == VK_NULL_HANDLE) {
        throw std::runtime_error("submission coordinator requires a queue");
    }
    if (!submit_) {
        throw std::runtime_error("submission coordinator requires a submit function");
    }
    if (!wait_) {
        throw std::runtime_error("submission coordinator requires a wait function");
    }
}

GpuSubmissionTicket SubmissionCoordinator::submit(const QueueSubmitInfo& submit_info,
                                                  const char* label) {
    submit_(queue_, submit_info, label);

    last_submitted_ = tickets_.issue();
    return last_submitted_;
}

GpuSubmissionTicket SubmissionCoordinator::submit_and_wait(const QueueSubmitInfo& submit_info,
                                                           const char* submit_label,
                                                           const char* wait_label) {
    const GpuSubmissionTicket ticket = submit(submit_info, submit_label);
    wait_idle(wait_label);
    mark_completed(ticket);
    return ticket;
}

void SubmissionCoordinator::wait_idle(const char* label) const {
    wait_(queue_, label);
}

void SubmissionCoordinator::mark_completed(GpuSubmissionTicket ticket) {
    if (last_submitted_ < ticket) {
        throw std::runtime_error("submission coordinator cannot complete unsubmitted work");
    }
    if (completed_ < ticket) {
        completed_ = ticket;
    }
}

} // namespace cubey::vulkan
