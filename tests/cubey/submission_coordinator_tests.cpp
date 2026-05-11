#include <cubey/vulkan/submission_coordinator.h>

#include <vulkan/vulkan.h>

#include <cstddef>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

struct SubmitRecord {
    VkQueue queue = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> command_buffers;
    VkFence fence = VK_NULL_HANDLE;
    std::string label;
};

} // namespace

void test_submission_coordinator_issues_monotonic_gpu_tickets() {
    const VkQueue queue = reinterpret_cast<VkQueue>(0x10);
    const VkCommandBuffer first_command_buffer = reinterpret_cast<VkCommandBuffer>(0x11);
    const VkCommandBuffer second_command_buffer = reinterpret_cast<VkCommandBuffer>(0x12);
    const VkFence second_fence = reinterpret_cast<VkFence>(0x13);

    std::vector<SubmitRecord> submits;
    cubey::vulkan::SubmissionCoordinator coordinator(
        queue,
        [&submits](VkQueue submitted_queue, const cubey::vulkan::QueueSubmitInfo& submit,
                   const char* label) {
            submits.push_back({
                .queue = submitted_queue,
                .command_buffers = submit.command_buffers,
                .fence = submit.fence,
                .label = label,
            });
        },
        [](VkQueue, const char*) {});

    const cubey::FrameTicket first =
        coordinator.submit({.command_buffers = {first_command_buffer}}, "submit first");
    const cubey::FrameTicket second = coordinator.submit(
        {.command_buffers = {second_command_buffer}, .fence = second_fence}, "submit second");

    require(first.value == 1, "first GPU submission ticket should start at one");
    require(second.value == 2, "GPU submission tickets should be monotonic");
    require(coordinator.last_submitted() == second,
            "coordinator should expose the last submitted ticket");
    require(coordinator.completed().value == 0,
            "submitting work should not imply that the GPU completed it");

    require(submits.size() == 2, "coordinator should call the submit backend for each submit");
    require(submits[0].queue == queue, "submit backend should receive the configured queue");
    require(submits[0].command_buffers.size() == 1,
            "submit backend should receive submitted command buffers");
    require(submits[0].command_buffers[0] == first_command_buffer,
            "submit backend should preserve command buffer handles");
    require(submits[0].label == "submit first", "submit backend should receive the label");
    require(submits[1].fence == second_fence, "submit backend should preserve the submit fence");
}

void test_submission_coordinator_submit_and_wait_marks_completion() {
    const VkQueue queue = reinterpret_cast<VkQueue>(0x20);
    const VkCommandBuffer command_buffer = reinterpret_cast<VkCommandBuffer>(0x21);

    std::vector<std::string> events;
    cubey::vulkan::SubmissionCoordinator coordinator(
        queue,
        [&events](VkQueue submitted_queue, const cubey::vulkan::QueueSubmitInfo& submit,
                  const char* label) {
            require(submitted_queue == reinterpret_cast<VkQueue>(0x20),
                    "submit_and_wait should submit to the configured queue");
            require(submit.command_buffers.size() == 1,
                    "submit_and_wait should forward command buffers");
            require(submit.command_buffers[0] == reinterpret_cast<VkCommandBuffer>(0x21),
                    "submit_and_wait should preserve the command buffer handle");
            events.push_back(std::string("submit:") + label);
        },
        [&events](VkQueue waited_queue, const char* label) {
            require(waited_queue == reinterpret_cast<VkQueue>(0x20),
                    "submit_and_wait should wait on the configured queue");
            events.push_back(std::string("wait:") + label);
        });

    const cubey::FrameTicket ticket = coordinator.submit_and_wait(
        {.command_buffers = {command_buffer}}, "vkQueueSubmit immediate", "vkQueueWaitIdle");

    require(ticket.value == 1, "submit_and_wait should return the submitted ticket");
    require(coordinator.last_submitted() == ticket,
            "submit_and_wait should update the last submitted ticket");
    require(coordinator.completed() == ticket,
            "submit_and_wait should mark the submitted ticket completed after waiting");
    require(events.size() == 2, "submit_and_wait should submit and then wait");
    require(events[0] == "submit:vkQueueSubmit immediate",
            "submit_and_wait should submit before waiting");
    require(events[1] == "wait:vkQueueWaitIdle",
            "submit_and_wait should pass the wait label to the wait backend");
}

void test_submission_coordinator_completion_tracking_rejects_future_tickets() {
    const VkQueue queue = reinterpret_cast<VkQueue>(0x30);
    const VkCommandBuffer first_command_buffer = reinterpret_cast<VkCommandBuffer>(0x31);
    const VkCommandBuffer second_command_buffer = reinterpret_cast<VkCommandBuffer>(0x32);

    cubey::vulkan::SubmissionCoordinator coordinator(
        queue, [](VkQueue, const cubey::vulkan::QueueSubmitInfo&, const char*) {},
        [](VkQueue, const char*) {});

    const cubey::FrameTicket first =
        coordinator.submit({.command_buffers = {first_command_buffer}}, "submit first");
    const cubey::FrameTicket second =
        coordinator.submit({.command_buffers = {second_command_buffer}}, "submit second");

    coordinator.mark_completed(second);
    require(coordinator.completed() == second, "completed ticket should advance to submitted work");

    coordinator.mark_completed(first);
    require(coordinator.completed() == second, "completed ticket should never regress");

    bool threw = false;
    try {
        coordinator.mark_completed(cubey::FrameTicket{.value = second.value + 1U});
    } catch (const std::runtime_error&) {
        threw = true;
    }
    require(threw, "coordinator should reject completion of never-submitted tickets");
}

void test_submission_coordinator_failed_submit_does_not_issue_ticket() {
    const VkQueue queue = reinterpret_cast<VkQueue>(0x40);
    const VkCommandBuffer command_buffer = reinterpret_cast<VkCommandBuffer>(0x41);

    cubey::vulkan::SubmissionCoordinator coordinator(
        queue,
        [](VkQueue, const cubey::vulkan::QueueSubmitInfo&, const char*) {
            throw std::runtime_error("submit failed");
        },
        [](VkQueue, const char*) {});

    bool threw = false;
    try {
        (void)coordinator.submit({.command_buffers = {command_buffer}}, "submit failure");
    } catch (const std::runtime_error&) {
        threw = true;
    }

    require(threw, "coordinator should propagate submit failures");
    require(coordinator.last_submitted().value == 0,
            "failed submits should not advance submitted tickets");
    require(coordinator.completed().value == 0, "failed submits should not mark completion");
}
