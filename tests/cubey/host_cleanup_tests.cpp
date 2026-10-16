#include "../../src/cubey/host/cleanup.h"

#include <stdexcept>
#include <string>
#include <vector>

namespace {
void require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
} // namespace

void test_host_cleanup_finishes_actions_and_preserves_first_failure() {
    bool finished = false;
    std::string failure;
    try {
        cubey::host::detail::cleanup_all([] { throw std::runtime_error("first cleanup failure"); },
                                         [] { throw std::runtime_error("later cleanup failure"); },
                                         [&] { finished = true; });
    } catch (const std::runtime_error& error) {
        failure = error.what();
    }
    require(finished, "a failed cleanup action must not skip the remaining actions");
    require(failure == "first cleanup failure", "cleanup must preserve the first failure");
}

void test_host_cleanup_retires_admitted_work_after_raw_callback_failure() {
    using namespace cubey::vulkan;
    for (const auto mode : {GpuRuntimeExecutionMode::Inline, GpuRuntimeExecutionMode::Threaded}) {
        std::vector<std::string> events;
        SubmissionCoordinator submission(
            reinterpret_cast<VkQueue>(0x56),
            [&](VkQueue, const QueueSubmitInfo&, const char*) { events.emplace_back("submit"); },
            [&](VkQueue, const char*) { events.emplace_back("idle"); });
        GpuRuntime gpu({.device = reinterpret_cast<Device*>(0x1),
                        .submission = &submission,
                        .execution_mode = mode});
        static_cast<void>(gpu.enqueue({.label = "failing admitted work", .work = [&](auto&) {
                                           events.emplace_back("failure");
                                           throw std::runtime_error("admitted work failure");
                                       }}));
        static_cast<void>(gpu.enqueue(
            {.label = "submitted tail", .work = [&](auto& owner) {
                 const auto ticket = owner.submission().submit({}, "admitted submission");
                 owner.defer_destruction_after(ticket, [&] { events.emplace_back("retire"); });
             }}));
        std::string failure;
        try {
            cubey::host::detail::wait_for_gpu(gpu, "host cleanup test");
        } catch (const std::runtime_error& error) {
            failure = error.what();
        }
        require(failure == "admitted work failure", "cleanup must report admitted work failure");
        require(events == std::vector<std::string>{"failure", "submit", "idle", "retire"},
                "cleanup must finish restored work, wait on the owner and retire before returning");
        gpu.shutdown();
    }
}
