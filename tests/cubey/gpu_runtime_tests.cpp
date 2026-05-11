#include <cubey/vulkan/gpu_runtime.h>

#include <vulkan/vulkan.h>

#include <atomic>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

cubey::vulkan::Device* fake_device() {
    return reinterpret_cast<cubey::vulkan::Device*>(0x55);
}

cubey::vulkan::SubmissionCoordinator fake_submission() {
    return cubey::vulkan::SubmissionCoordinator(
        reinterpret_cast<VkQueue>(0x56),
        [](VkQueue, const cubey::vulkan::QueueSubmitInfo&, const char*) {},
        [](VkQueue, const char*) {});
}

} // namespace

void test_gpu_work_queue_drains_fifo_and_owns_requests() {
    cubey::vulkan::GpuWorkQueue queue;
    require(queue.empty(), "GPU work queue should start empty");

    const cubey::vulkan::GpuWorkTicket first = queue.enqueue({
        .label = "first upload",
        .work = [](cubey::vulkan::GpuOwnerContext&) {},
    });
    const cubey::vulkan::GpuWorkTicket second = queue.enqueue({
        .label = "second capture",
        .work = [](cubey::vulkan::GpuOwnerContext&) {},
    });

    require(first.id == 1, "first GPU work ticket should start at one");
    require(first.label == "first upload", "GPU work ticket should preserve label");
    require(second.id == 2, "GPU work tickets should be monotonic");
    require(queue.pending_count() == 2, "GPU work queue should count pending work");

    std::vector<cubey::vulkan::QueuedGpuWork> drained = queue.drain();
    require(queue.empty(), "drain should clear the GPU work queue");
    require(drained.size() == 2, "drain should return all queued GPU work");
    require(drained[0].ticket.id == first.id, "drain should preserve FIFO order");
    require(drained[0].ticket.label == "first upload", "drained work should preserve labels");
    require(static_cast<bool>(drained[0].work), "drained work should preserve callbacks");
    require(drained[1].ticket.id == second.id, "drain should preserve second queued work");
}

void test_gpu_runtime_drains_inline_on_owner_thread() {
    cubey::vulkan::SubmissionCoordinator submission = fake_submission();
    const cubey::FrameTicket completed = submission.submit_and_wait(
        {.command_buffers = {reinterpret_cast<VkCommandBuffer>(0x57)}}, "submit fake", "wait fake");

    cubey::vulkan::GpuRuntime runtime({
        .device = fake_device(),
        .submission = &submission,
        .execution_mode = cubey::vulkan::GpuRuntimeExecutionMode::Inline,
    });

    std::vector<std::string> events;
    static_cast<void>(runtime.enqueue({
        .label = "first",
        .work =
            [&events, completed](cubey::vulkan::GpuOwnerContext& context) {
                require(context.is_owner_thread(),
                        "inline drain should execute work on the owner thread");
                require(context.completed_submission() == completed,
                        "owner context should expose completed GPU submission");
                events.push_back("first");
            },
    }));
    static_cast<void>(runtime.enqueue({
        .label = "second",
        .work = [&events](cubey::vulkan::GpuOwnerContext&) { events.push_back("second"); },
    }));

    const cubey::vulkan::GpuDrainResult result = runtime.drain_inline();

    require(events.size() == 2, "drain_inline should execute queued GPU work");
    require(events[0] == "first", "drain_inline should preserve FIFO execution");
    require(events[1] == "second", "drain_inline should execute later work");
    require(result.completed_count == 2, "drain result should count completed work");
    require(result.last_completed.id == 2, "drain result should expose last work ticket");
    require(result.completed_submission == completed,
            "drain result should expose completed submission state");
    require(runtime.pending_count() == 0, "drain_inline should clear completed work");
}

void test_gpu_runtime_accepts_cross_thread_enqueue_but_rejects_cross_thread_drain() {
    cubey::vulkan::SubmissionCoordinator submission = fake_submission();
    cubey::vulkan::GpuRuntime runtime({
        .device = fake_device(),
        .submission = &submission,
        .execution_mode = cubey::vulkan::GpuRuntimeExecutionMode::Inline,
    });

    std::atomic<bool> enqueued = false;
    std::thread producer([&runtime, &enqueued] {
        static_cast<void>(runtime.enqueue({
            .label = "worker-produced",
            .work = [](cubey::vulkan::GpuOwnerContext&) {},
        }));
        enqueued = true;
    });
    producer.join();

    require(enqueued.load(), "worker thread should enqueue GPU work");
    require(runtime.pending_count() == 1, "worker-produced work should remain pending");

    std::atomic<bool> drain_rejected = false;
    std::thread wrong_owner([&runtime, &drain_rejected] {
        try {
            static_cast<void>(runtime.drain_inline());
        } catch (const std::runtime_error&) {
            drain_rejected = true;
        }
    });
    wrong_owner.join();

    require(drain_rejected.load(), "non-owner thread should not drain GPU work");
    require(runtime.pending_count() == 1, "rejected drain should leave work pending");

    static_cast<void>(runtime.drain_inline());
    require(runtime.empty(), "owner thread should still be able to drain pending work");
}

void test_gpu_runtime_preserves_pending_work_after_callback_failure() {
    cubey::vulkan::SubmissionCoordinator submission = fake_submission();
    cubey::vulkan::GpuRuntime runtime({
        .device = fake_device(),
        .submission = &submission,
        .execution_mode = cubey::vulkan::GpuRuntimeExecutionMode::Inline,
    });

    std::vector<std::string> events;
    static_cast<void>(runtime.enqueue({
        .label = "first",
        .work = [&events](cubey::vulkan::GpuOwnerContext&) { events.push_back("first"); },
    }));
    static_cast<void>(runtime.enqueue({
        .label = "failing",
        .work =
            [&events](cubey::vulkan::GpuOwnerContext&) {
                events.push_back("failing");
                throw std::runtime_error("GPU work failed");
            },
    }));
    static_cast<void>(runtime.enqueue({
        .label = "after failure",
        .work = [&events](cubey::vulkan::GpuOwnerContext&) { events.push_back("after failure"); },
    }));

    bool threw = false;
    try {
        static_cast<void>(runtime.drain_inline());
    } catch (const std::runtime_error&) {
        threw = true;
    }

    require(threw, "drain_inline should propagate GPU work failures");
    require(events.size() == 2, "drain_inline should stop at the failing callback");
    require(events[0] == "first", "work before the failure should run");
    require(events[1] == "failing", "failing work should run before propagating");
    require(runtime.pending_count() == 1, "work after a failure should remain pending");

    static_cast<void>(runtime.drain_inline());
    require(events.size() == 3, "remaining work should be drainable after failure");
    require(events[2] == "after failure", "remaining work should preserve FIFO order");
}

void test_gpu_runtime_defaults_to_threaded_execution() {
    cubey::vulkan::SubmissionCoordinator submission = fake_submission();
    cubey::vulkan::GpuRuntime runtime({
        .device = fake_device(),
        .submission = &submission,
    });

    const std::thread::id caller_thread = std::this_thread::get_id();
    std::thread::id work_thread;
    bool context_reported_owner_thread = false;

    static_cast<void>(runtime.submit_and_wait({
        .label = "threaded setup",
        .work =
            [&work_thread,
             &context_reported_owner_thread](cubey::vulkan::GpuOwnerContext& context) {
                work_thread = std::this_thread::get_id();
                context_reported_owner_thread = context.is_owner_thread();
            },
    }));

    require(context_reported_owner_thread,
            "threaded runtime should execute work on the owner thread");
    require(work_thread != caller_thread,
            "default GPU runtime execution should happen on a background owner thread");
    runtime.shutdown();
}

void test_gpu_runtime_submit_and_wait_propagates_threaded_failures() {
    cubey::vulkan::SubmissionCoordinator submission = fake_submission();
    cubey::vulkan::GpuRuntime runtime({
        .device = fake_device(),
        .submission = &submission,
    });

    bool threw = false;
    try {
        static_cast<void>(runtime.submit_and_wait({
            .label = "failing setup",
            .work =
                [](cubey::vulkan::GpuOwnerContext&) {
                    throw std::runtime_error("threaded GPU setup failed");
                },
        }));
    } catch (const std::runtime_error& error) {
        threw = std::string(error.what()) == "threaded GPU setup failed";
    }

    require(threw, "submit_and_wait should propagate threaded GPU work failures");
    require(runtime.empty(), "failed submit_and_wait work should not leave pending work");
    runtime.shutdown();
}

void test_gpu_runtime_shutdown_rejects_new_work() {
    cubey::vulkan::SubmissionCoordinator submission = fake_submission();
    cubey::vulkan::GpuRuntime runtime({
        .device = fake_device(),
        .submission = &submission,
    });

    runtime.shutdown();

    bool rejected = false;
    try {
        static_cast<void>(runtime.enqueue({
            .label = "after shutdown",
            .work = [](cubey::vulkan::GpuOwnerContext&) {},
        }));
    } catch (const std::runtime_error&) {
        rejected = true;
    }

    require(rejected, "GPU runtime should reject enqueue after shutdown");
}
