#include <cubey/vulkan/submission_tickets.h>

#include <stdexcept>
#include <vector>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

} // namespace

void test_gpu_submission_ticket_issuer_returns_monotonic_tickets() {
    cubey::vulkan::GpuSubmissionTicketIssuer issuer;

    cubey::vulkan::GpuSubmissionTicket first = issuer.issue();
    cubey::vulkan::GpuSubmissionTicket second = issuer.issue();

    require(first.value == 1, "first issued GPU submission ticket should be 1");
    require(second.value == 2, "second issued GPU submission ticket should be 2");
    require(first < second, "GPU submission tickets should compare by value");
}

void test_deferred_gpu_destruction_queue_retires_completed_tickets() {
    cubey::vulkan::DeferredGpuDestructionQueue queue;
    std::vector<int> retired;

    queue.defer_after(cubey::vulkan::GpuSubmissionTicket{.value = 2},
                      [&retired] { retired.push_back(2); });
    queue.defer_after(cubey::vulkan::GpuSubmissionTicket{.value = 4},
                      [&retired] { retired.push_back(4); });

    require(queue.retire_completed(cubey::vulkan::GpuSubmissionTicket{.value = 1}) == 0,
            "retire before first ticket should not run actions");
    require(retired.empty(), "no action should run before its ticket completes");

    require(queue.retire_completed(cubey::vulkan::GpuSubmissionTicket{.value = 2}) == 1,
            "retire at first ticket should run one action");
    require(retired == std::vector<int>({2}), "first retired action should run");
    require(queue.pending_count() == 1, "later action should stay pending");

    require(queue.retire_completed(cubey::vulkan::GpuSubmissionTicket{.value = 4}) == 1,
            "retire at second ticket should run later action");
    require(retired == std::vector<int>({2, 4}), "actions should run in enqueue order");
    require(queue.empty(), "queue should be empty after all actions retire");
}
