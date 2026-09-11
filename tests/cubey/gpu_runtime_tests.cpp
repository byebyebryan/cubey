#include "source_file_test_helpers.h"

#include <cubey/vulkan/gpu_runtime.h>
#include <cubey/vulkan/upload_batch.h>
#include <cubey/vulkan/upload_step.h>

#include <vulkan/vulkan.h>

#include <atomic>
#include <condition_variable>
#include <filesystem>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

using cubey::tests::read_source_file;
using cubey::tests::require_contains;

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

void test_gpu_upload_batch_is_non_movable_lexical_owner_scope() {
    static_assert(!std::is_copy_constructible_v<cubey::vulkan::GpuUploadBatch>);
    static_assert(!std::is_copy_assignable_v<cubey::vulkan::GpuUploadBatch>);
    static_assert(!std::is_move_constructible_v<cubey::vulkan::GpuUploadBatch>);
    static_assert(!std::is_move_assignable_v<cubey::vulkan::GpuUploadBatch>);
    require(true, "GPU upload batch should remain a lexical owner-scope object");
}

void test_gpu_upload_step_is_non_movable_lexical_owner_scope() {
    static_assert(!std::is_copy_constructible_v<cubey::vulkan::GpuUploadStep>);
    static_assert(!std::is_copy_assignable_v<cubey::vulkan::GpuUploadStep>);
    static_assert(!std::is_move_constructible_v<cubey::vulkan::GpuUploadStep>);
    static_assert(!std::is_move_assignable_v<cubey::vulkan::GpuUploadStep>);
    require(true, "GPU upload step should remain a lexical owner-scope object");
}

void test_gpu_runtime_owner_cleanup_is_move_only_and_runs_on_owner() {
    static_assert(!std::is_copy_constructible_v<cubey::vulkan::GpuRuntimeOwnerCleanup>);
    static_assert(!std::is_copy_assignable_v<cubey::vulkan::GpuRuntimeOwnerCleanup>);
    static_assert(std::is_move_constructible_v<cubey::vulkan::GpuRuntimeOwnerCleanup>);
    static_assert(std::is_move_assignable_v<cubey::vulkan::GpuRuntimeOwnerCleanup>);

    cubey::vulkan::SubmissionCoordinator submission = fake_submission();
    cubey::vulkan::GpuRuntime runtime({
        .device = fake_device(),
        .submission = &submission,
        .execution_mode = cubey::vulkan::GpuRuntimeExecutionMode::Inline,
    });

    std::size_t cleanup_count = 0;
    cubey::vulkan::GpuRuntimeOwnerCleanup cleanup = runtime.register_owner_cleanup(
        "test owner cleanup", [&cleanup_count](cubey::vulkan::GpuOwnerContext& owner) {
            require(owner.is_owner_thread(), "registered cleanup must run on the GPU owner");
            ++cleanup_count;
        });
    cubey::vulkan::GpuRuntimeOwnerCleanup moved_cleanup = std::move(cleanup);
    require(!cleanup.registered(), "moved-from owner cleanup must be inert");
    require(!cleanup.belongs_to(runtime),
            "moved-from owner cleanup must not retain a runtime identity");
    require(moved_cleanup.registered(), "moved owner cleanup must retain its registration");
    require(moved_cleanup.belongs_to(runtime),
            "owner cleanup should retain its creating runtime identity");

    moved_cleanup.request();
    require(cleanup_count == 0U, "owner cleanup must not run on the requesting thread");
    static_cast<void>(runtime.drain_inline());
    require(cleanup_count == 1U, "owner cleanup should run exactly once after owner drain");
    require(!moved_cleanup.registered(), "completed owner cleanup should unregister itself");
    moved_cleanup.request();
    static_cast<void>(runtime.drain_inline());
    require(cleanup_count == 1U, "completed owner cleanup must not be queued twice");
}

void test_gpu_runtime_owner_cleanup_move_assignment_retires_replaced_registration() {
    cubey::vulkan::SubmissionCoordinator submission = fake_submission();
    cubey::vulkan::GpuRuntime runtime({
        .device = fake_device(),
        .submission = &submission,
        .execution_mode = cubey::vulkan::GpuRuntimeExecutionMode::Inline,
    });

    std::size_t replaced_cleanup_count = 0;
    std::size_t adopted_cleanup_count = 0;
    cubey::vulkan::GpuRuntimeOwnerCleanup cleanup = runtime.register_owner_cleanup(
        "replaced cleanup", [&replaced_cleanup_count](cubey::vulkan::GpuOwnerContext& owner) {
            require(owner.is_owner_thread(), "replaced cleanup must run on the GPU owner");
            ++replaced_cleanup_count;
        });
    cubey::vulkan::GpuRuntimeOwnerCleanup adopted = runtime.register_owner_cleanup(
        "adopted cleanup", [&adopted_cleanup_count](cubey::vulkan::GpuOwnerContext& owner) {
            require(owner.is_owner_thread(), "adopted cleanup must run on the GPU owner");
            ++adopted_cleanup_count;
        });

    cleanup = std::move(adopted);
    require(!adopted.registered(), "move assignment must leave its source inert");
    require(cleanup.registered(), "move assignment must adopt the incoming registration");
    require(replaced_cleanup_count == 0U && adopted_cleanup_count == 0U,
            "move assignment should queue, not run, replaced cleanup on the caller thread");

    static_cast<void>(runtime.drain_inline());
    require(replaced_cleanup_count == 1U && adopted_cleanup_count == 0U,
            "move assignment must retire the replaced registration on the GPU owner");

    cleanup.request();
    static_cast<void>(runtime.drain_inline());
    require(adopted_cleanup_count == 1U,
            "move-assigned token must still own and retire the incoming registration");
}

void test_gpu_runtime_shutdown_runs_remaining_owner_cleanups_before_teardown() {
    cubey::vulkan::SubmissionCoordinator submission = fake_submission();
    cubey::vulkan::GpuRuntime runtime({
        .device = fake_device(),
        .submission = &submission,
        .execution_mode = cubey::vulkan::GpuRuntimeExecutionMode::Inline,
    });

    std::vector<std::string> events;
    static_cast<void>(runtime.enqueue({
        .label = "work before shutdown cleanup",
        .work =
            [&events](cubey::vulkan::GpuOwnerContext& owner) {
                require(owner.is_owner_thread(), "queued work should run on the GPU owner");
                events.emplace_back("work");
            },
    }));
    cubey::vulkan::GpuRuntimeOwnerCleanup cleanup = runtime.register_owner_cleanup(
        "shutdown owner cleanup", [&events](cubey::vulkan::GpuOwnerContext& owner) {
            require(owner.is_owner_thread(), "shutdown cleanup must run on the GPU owner");
            events.emplace_back("cleanup");
        });

    runtime.shutdown();
    require(events.size() == 2U && events[0] == "work" && events[1] == "cleanup",
            "shutdown should drain work before owner cleanup and device teardown");
    require(!cleanup.registered(), "shutdown should consume remaining cleanup registrations");
    require(!cleanup.belongs_to(runtime),
            "stopped runtimes must no longer accept registered cleanup identities");
    cleanup.request();
    require(events.size() == 2U, "late cleanup requests must be inert after runtime shutdown");
}

void test_gpu_runtime_default_staging_pool_is_explicitly_disabled() {
    cubey::vulkan::SubmissionCoordinator submission = fake_submission();
    cubey::vulkan::GpuRuntime runtime({
        .device = fake_device(),
        .submission = &submission,
        .execution_mode = cubey::vulkan::GpuRuntimeExecutionMode::Inline,
    });
    require(!runtime.has_staging_pool(),
            "fake runtimes should report that staging-pool support is disabled");

    bool rejected = false;
    static_cast<void>(runtime.submit_and_wait({
        .label = "observe absent staging pool",
        .work =
            [&rejected](cubey::vulkan::GpuOwnerContext& context) {
                try {
                    static_cast<void>(context.staging_pool());
                } catch (const std::runtime_error& error) {
                    rejected =
                        std::string(error.what()) == "GPU runtime has no configured staging pool";
                }
            },
    }));
    require(rejected, "fake runtimes should require an explicit staging-pool configuration");
}

void test_gpu_runtime_drains_inline_on_owner_thread() {
    cubey::vulkan::SubmissionCoordinator submission = fake_submission();
    const cubey::vulkan::GpuSubmissionTicket completed = submission.submit_and_wait(
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

void test_gpu_runtime_typed_jobs_return_results_inline() {
    cubey::vulkan::SubmissionCoordinator submission = fake_submission();
    cubey::vulkan::GpuRuntime runtime({
        .device = fake_device(),
        .submission = &submission,
        .execution_mode = cubey::vulkan::GpuRuntimeExecutionMode::Inline,
    });

    auto job = runtime.submit("typed inline job", [](cubey::vulkan::GpuOwnerContext& owner) {
        require(owner.is_owner_thread(), "typed GPU job should receive its owner context");
        return 42;
    });

    require(job.ticket().id == 1, "typed GPU job should expose its queued ticket");
    require(job.ticket().label == "typed inline job", "typed GPU job should preserve its label");
    require(!job.ready(), "inline typed GPU job should remain pending before drain");
    static_cast<void>(runtime.drain_inline());
    require(job.ready(), "inline typed GPU job should become ready after drain");
    require(job.get() == 42, "typed GPU job should return its result");
}

void test_gpu_runtime_typed_jobs_capture_failures_without_stopping_queue() {
    cubey::vulkan::SubmissionCoordinator submission = fake_submission();
    cubey::vulkan::GpuRuntime runtime({
        .device = fake_device(),
        .submission = &submission,
        .execution_mode = cubey::vulkan::GpuRuntimeExecutionMode::Inline,
    });

    auto failed = runtime.submit("typed failure", [](cubey::vulkan::GpuOwnerContext&) -> int {
        throw std::runtime_error("typed GPU failure");
    });
    auto following =
        runtime.submit("typed following", [](cubey::vulkan::GpuOwnerContext&) { return 7; });

    const cubey::vulkan::GpuDrainResult drained = runtime.drain_inline();
    require(drained.completed_count == 2,
            "captured typed failure should not stop later queued GPU work");
    require(failed.ready() && following.ready(),
            "typed GPU jobs should both complete after the queue drains");
    bool propagated = false;
    try {
        static_cast<void>(failed.get());
    } catch (const std::runtime_error& error) {
        propagated = std::string(error.what()) == "typed GPU failure";
    }
    require(propagated, "typed GPU job should propagate its captured failure through get");
    require(following.get() == 7, "typed failure should not poison following GPU jobs");
}

void test_gpu_runtime_typed_jobs_execute_on_threaded_owner() {
    cubey::vulkan::SubmissionCoordinator submission = fake_submission();
    cubey::vulkan::GpuRuntime runtime({
        .device = fake_device(),
        .submission = &submission,
    });
    const std::thread::id caller = std::this_thread::get_id();

    auto job = runtime.submit("typed threaded job", [](cubey::vulkan::GpuOwnerContext& owner) {
        require(owner.is_owner_thread(), "threaded typed job should run on the GPU owner");
        return std::this_thread::get_id();
    });
    const std::thread::id worker = job.get();

    require(worker != std::thread::id{}, "threaded typed GPU job should execute");
    require(worker != caller, "threaded typed GPU job should not execute on its caller");
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

void test_gpu_runtime_threaded_submit_and_wait_handles_owner_thread_calls() {
    const std::filesystem::path root{CUBEY_SOURCE_DIR};
    const std::string source = read_source_file(root / "src/cubey/vulkan/gpu_runtime.cpp");

    require_contains(source, "std::this_thread::get_id() == owner_thread_",
                     "threaded submit_and_wait should detect owner-thread callers");
    require_contains(source, "drain_on_owner_thread();",
                     "owner-thread submit_and_wait should drain directly instead of waiting");
}

void test_gpu_runtime_wait_queue_idle_runs_on_owner_thread() {
    std::thread::id caller_thread = std::this_thread::get_id();
    std::thread::id wait_thread{};
    cubey::vulkan::SubmissionCoordinator submission(
        reinterpret_cast<VkQueue>(0x56),
        [](VkQueue, const cubey::vulkan::QueueSubmitInfo&, const char*) {},
        [&wait_thread](VkQueue, const char*) { wait_thread = std::this_thread::get_id(); });
    cubey::vulkan::GpuRuntime runtime({
        .device = fake_device(),
        .submission = &submission,
    });

    runtime.wait_queue_idle("test queue idle");

    require(wait_thread != std::thread::id{}, "queue idle should call the wait function");
    require(wait_thread != caller_thread,
            "threaded queue idle should execute on the GPU owner thread");
}

void test_gpu_runtime_mark_submission_completed_updates_completed_ticket() {
    cubey::vulkan::GpuSubmissionTicket submitted{};
    std::thread::id caller_thread = std::this_thread::get_id();
    std::thread::id completion_thread{};
    cubey::vulkan::SubmissionCoordinator submission(
        reinterpret_cast<VkQueue>(0x56),
        [&submitted](VkQueue, const cubey::vulkan::QueueSubmitInfo&, const char*) {
            submitted = cubey::vulkan::GpuSubmissionTicket{.value = 1};
        },
        [](VkQueue, const char*) {});
    submitted = submission.submit({.command_buffers = {reinterpret_cast<VkCommandBuffer>(0x57)}},
                                  "submit frame");
    cubey::vulkan::GpuRuntime runtime({
        .device = fake_device(),
        .submission = &submission,
    });

    runtime.mark_submission_completed(submitted);
    static_cast<void>(runtime.submit_and_wait({
        .label = "observe completion thread",
        .work =
            [&completion_thread, submitted](cubey::vulkan::GpuOwnerContext& owner) {
                completion_thread = std::this_thread::get_id();
                require(owner.completed_submission() == submitted,
                        "GPU runtime should mark submitted ticket completed");
            },
    }));
    runtime.mark_submission_completed(cubey::vulkan::GpuSubmissionTicket{.value = 0});

    require(completion_thread != std::thread::id{}, "completion observer should run");
    require(completion_thread != caller_thread,
            "submission completion should be observable on the owner thread by default");
    require(submission.completed() == submitted,
            "mark_submission_completed should not regress completed tickets");
}

void test_gpu_runtime_defers_destruction_until_submission_completion() {
    cubey::vulkan::SubmissionCoordinator submission = fake_submission();
    const cubey::vulkan::GpuSubmissionTicket submitted = submission.submit(
        {.command_buffers = {reinterpret_cast<VkCommandBuffer>(0x57)}}, "submit frame");
    cubey::vulkan::GpuRuntime runtime({
        .device = fake_device(),
        .submission = &submission,
        .execution_mode = cubey::vulkan::GpuRuntimeExecutionMode::Inline,
    });

    std::size_t retired = 0;
    runtime.defer_destruction_after(submitted, [&retired] { ++retired; });
    require(retired == 0 && runtime.deferred_destruction_count() == 1,
            "deferred destruction should remain pending while its submission is in flight");

    runtime.mark_submission_completed(submitted);
    require(retired == 1 && runtime.deferred_destruction_count() == 0,
            "submission completion should retire the deferred action exactly once");
    runtime.mark_submission_completed(submitted);
    require(retired == 1, "repeated completion should not repeat a retired action");
}

void test_gpu_owner_completion_retires_ticket_without_advancing_later_tickets() {
    cubey::vulkan::SubmissionCoordinator submission = fake_submission();
    const cubey::vulkan::GpuSubmissionTicket first = submission.submit(
        {.command_buffers = {reinterpret_cast<VkCommandBuffer>(0x57)}}, "first upload");
    const cubey::vulkan::GpuSubmissionTicket later = submission.submit(
        {.command_buffers = {reinterpret_cast<VkCommandBuffer>(0x58)}}, "later upload");
    cubey::vulkan::GpuRuntime runtime({
        .device = fake_device(),
        .submission = &submission,
        .execution_mode = cubey::vulkan::GpuRuntimeExecutionMode::Inline,
    });

    std::size_t retired = 0U;
    static_cast<void>(runtime.submit_and_wait({
        .label = "register ticketed retirement",
        .work =
            [first, &retired](cubey::vulkan::GpuOwnerContext& owner) {
                owner.defer_destruction_after(first, [&retired] { ++retired; });
            },
    }));
    require(retired == 0U && submission.completed().value == 0U,
            "registering a ticketed retirement must not complete its submission");

    static_cast<void>(runtime.submit_and_wait({
        .label = "complete first ticket",
        .work =
            [first](cubey::vulkan::GpuOwnerContext& owner) { owner.complete_submission(first); },
    }));
    require(retired == 1U && submission.completed() == first,
            "owner completion should retire only the completed upload ticket");
    require(submission.completed() < later,
            "completing one upload must not make a later graphics ticket appear complete");
}

void test_gpu_runtime_collects_retirement_after_owner_work_advances_completion() {
    cubey::vulkan::SubmissionCoordinator submission = fake_submission();
    const cubey::vulkan::GpuSubmissionTicket submitted = submission.submit(
        {.command_buffers = {reinterpret_cast<VkCommandBuffer>(0x57)}}, "submit frame");
    cubey::vulkan::GpuRuntime runtime({
        .device = fake_device(),
        .submission = &submission,
        .execution_mode = cubey::vulkan::GpuRuntimeExecutionMode::Inline,
    });

    bool retired = false;
    runtime.defer_destruction_after(submitted, [&retired] { retired = true; });
    static_cast<void>(runtime.enqueue({
        .label = "complete submission",
        .work =
            [submitted](cubey::vulkan::GpuOwnerContext& owner) {
                owner.submission().mark_completed(submitted);
            },
    }));
    static_cast<void>(runtime.drain_inline());

    require(retired && runtime.deferred_destruction_count() == 0,
            "owner work should collect destruction after advancing submission completion");
}

void test_gpu_runtime_retires_completed_and_queue_idle_destruction() {
    cubey::vulkan::SubmissionCoordinator submission = fake_submission();
    const cubey::vulkan::GpuSubmissionTicket submitted = submission.submit(
        {.command_buffers = {reinterpret_cast<VkCommandBuffer>(0x57)}}, "submit frame");
    submission.mark_completed(submitted);
    cubey::vulkan::GpuRuntime runtime({
        .device = fake_device(),
        .submission = &submission,
        .execution_mode = cubey::vulkan::GpuRuntimeExecutionMode::Inline,
    });

    std::size_t retired = 0;
    runtime.defer_destruction_after(submitted, [&retired] { ++retired; });
    require(retired == 1 && runtime.deferred_destruction_count() == 0,
            "already-completed submission should retire destruction immediately");

    const cubey::vulkan::GpuSubmissionTicket next = submission.submit(
        {.command_buffers = {reinterpret_cast<VkCommandBuffer>(0x58)}}, "submit next frame");
    runtime.defer_destruction_after(next, [&retired] { ++retired; });
    runtime.wait_queue_idle("test retirement queue idle");
    require(retired == 2 && submission.completed() == next,
            "queue idle should complete submissions and retire their resources");
}

void test_gpu_runtime_rejects_unsubmitted_destruction_ticket() {
    cubey::vulkan::SubmissionCoordinator submission = fake_submission();
    cubey::vulkan::GpuRuntime runtime({
        .device = fake_device(),
        .submission = &submission,
        .execution_mode = cubey::vulkan::GpuRuntimeExecutionMode::Inline,
    });

    bool rejected = false;
    try {
        runtime.defer_destruction_after({.value = 1}, [] {});
    } catch (const std::runtime_error& error) {
        rejected =
            std::string(error.what()) == "deferred GPU destruction ticket has not been submitted";
    }
    require(rejected, "GPU runtime should reject destruction for a future submission");
}

void test_gpu_runtime_shutdown_rejects_new_work() {
    cubey::vulkan::SubmissionCoordinator submission = fake_submission();
    const cubey::vulkan::GpuSubmissionTicket submitted = submission.submit(
        {.command_buffers = {reinterpret_cast<VkCommandBuffer>(0x57)}}, "submit frame");
    cubey::vulkan::GpuRuntime runtime({
        .device = fake_device(),
        .submission = &submission,
    });

    bool retired = false;
    runtime.defer_destruction_after(submitted, [&retired] { retired = true; });
    runtime.shutdown();
    require(retired && runtime.deferred_destruction_count() == 0,
            "GPU runtime shutdown should retire submitted GPU resources");

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

void test_gpu_runtime_inline_shutdown_allows_reentrant_owner_callbacks() {
    {
        cubey::vulkan::SubmissionCoordinator submission = fake_submission();
        cubey::vulkan::GpuRuntime runtime({
            .device = fake_device(),
            .submission = &submission,
            .execution_mode = cubey::vulkan::GpuRuntimeExecutionMode::Inline,
        });
        bool admitted_callback_returned = false;
        static_cast<void>(runtime.enqueue({
            .label = "reentrant inline shutdown callback",
            .work =
                [&runtime, &admitted_callback_returned](cubey::vulkan::GpuOwnerContext& owner) {
                    require(owner.is_owner_thread(), "inline callback must run on the GPU owner");
                    runtime.shutdown();
                    admitted_callback_returned = true;
                },
        }));

        runtime.shutdown();
        require(admitted_callback_returned,
                "outer inline shutdown should complete an admitted reentrant callback");
    }

    {
        cubey::vulkan::SubmissionCoordinator submission = fake_submission();
        cubey::vulkan::GpuRuntime runtime({
            .device = fake_device(),
            .submission = &submission,
            .execution_mode = cubey::vulkan::GpuRuntimeExecutionMode::Inline,
        });
        bool retained_cleanup_returned = false;
        cubey::vulkan::GpuRuntimeOwnerCleanup cleanup = runtime.register_owner_cleanup(
            "reentrant inline shutdown cleanup",
            [&runtime, &retained_cleanup_returned](cubey::vulkan::GpuOwnerContext& owner) {
                require(owner.is_owner_thread(), "inline cleanup must run on the GPU owner");
                runtime.shutdown();
                retained_cleanup_returned = true;
            });

        runtime.shutdown();
        require(retained_cleanup_returned,
                "outer inline shutdown should complete a reentrant retained cleanup");
        require(!cleanup.registered(),
                "outer inline shutdown should consume a reentrant retained cleanup registration");
    }
}

void test_gpu_runtime_shutdown_closes_admission_before_queue_idle() {
    std::mutex mutex;
    std::condition_variable wait_started;
    std::condition_variable release_wait;
    bool waiting = false;
    bool released = false;
    cubey::vulkan::SubmissionCoordinator submission(
        reinterpret_cast<VkQueue>(0x56),
        [](VkQueue, const cubey::vulkan::QueueSubmitInfo&, const char*) {},
        [&](VkQueue, const char*) {
            std::unique_lock lock(mutex);
            waiting = true;
            wait_started.notify_one();
            release_wait.wait(lock, [&] { return released; });
        });
    cubey::vulkan::GpuRuntime runtime({
        .device = fake_device(),
        .submission = &submission,
    });

    std::exception_ptr shutdown_failure;
    std::thread shutdown_thread([&] {
        try {
            runtime.shutdown();
        } catch (...) {
            shutdown_failure = std::current_exception();
        }
    });
    {
        std::unique_lock lock(mutex);
        wait_started.wait(lock, [&] { return waiting; });
    }

    bool rejected = false;
    try {
        static_cast<void>(runtime.enqueue({
            .label = "racing shutdown",
            .work = [](cubey::vulkan::GpuOwnerContext&) {},
        }));
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    {
        std::scoped_lock lock(mutex);
        released = true;
    }
    release_wait.notify_one();
    shutdown_thread.join();

    require(shutdown_failure == nullptr, "GPU runtime shutdown should complete successfully");
    require(rejected, "GPU runtime should reject work while its final idle barrier is pending");
}

void test_gpu_runtime_shutdown_joins_owner_after_wait_failure() {
    cubey::vulkan::SubmissionCoordinator submission(
        reinterpret_cast<VkQueue>(0x56),
        [](VkQueue, const cubey::vulkan::QueueSubmitInfo&, const char*) {},
        [](VkQueue, const char*) { throw std::runtime_error("queue idle failed"); });
    const cubey::vulkan::GpuSubmissionTicket submitted = submission.submit(
        {.command_buffers = {reinterpret_cast<VkCommandBuffer>(0x57)}}, "submitted before failure");
    cubey::vulkan::GpuRuntime runtime({
        .device = fake_device(),
        .submission = &submission,
    });
    bool cleanup_on_owner = false;
    bool deferred_cleanup_on_owner = false;
    cubey::vulkan::GpuRuntimeOwnerCleanup cleanup = runtime.register_owner_cleanup(
        "cleanup after queue idle failure", [&cleanup_on_owner, &deferred_cleanup_on_owner,
                                             submitted](cubey::vulkan::GpuOwnerContext& owner) {
            cleanup_on_owner = owner.is_owner_thread();
            owner.defer_destruction_after(
                submitted, [&deferred_cleanup_on_owner] { deferred_cleanup_on_owner = true; });
        });

    bool propagated = false;
    try {
        runtime.shutdown();
    } catch (const std::runtime_error& error) {
        propagated = std::string(error.what()) == "queue idle failed";
    }

    require(propagated, "GPU runtime shutdown should propagate queue-idle failure after joining");
    require(cleanup_on_owner,
            "queue-idle failure should still run retained cleanup actions on the GPU owner");
    require(deferred_cleanup_on_owner,
            "queue-idle failure should force-retire retained deferred cleanup on the GPU owner");
    require(!cleanup.registered(),
            "queue-idle failure should consume retained cleanup registrations before stopping");
    runtime.shutdown();
}

void test_gpu_runtime_rejects_shutdown_from_owner_thread() {
    cubey::vulkan::SubmissionCoordinator submission = fake_submission();
    cubey::vulkan::GpuRuntime runtime({
        .device = fake_device(),
        .submission = &submission,
    });

    bool rejected = false;
    static_cast<void>(runtime.submit_and_wait({
        .label = "owner shutdown",
        .work =
            [&runtime, &rejected](cubey::vulkan::GpuOwnerContext&) {
                try {
                    runtime.shutdown();
                } catch (const std::runtime_error& error) {
                    rejected = std::string(error.what()) ==
                               "GPU runtime owner thread cannot shut itself down";
                }
            },
    }));

    require(rejected, "GPU runtime should reject owner-thread shutdown instead of deadlocking");
    runtime.shutdown();
}
