#include <cubey/engine/project_gpu_services.h>
#include <cubey/engine/project_runtime.h>

#include <vulkan/vulkan.h>

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

void test_project_context_exposes_optional_gpu_services() {
    cubey::jobs::JobSystem jobs(0);
    cubey::UploadQueue uploads;
    cubey::CaptureQueue captures(jobs);
    cubey::vulkan::SubmissionCoordinator submission = fake_submission();
    cubey::vulkan::GpuRuntime runtime({
        .device = fake_device(),
        .submission = &submission,
        .execution_mode = cubey::vulkan::GpuRuntimeExecutionMode::Inline,
    });
    cubey::ProjectGpuServices gpu(runtime, uploads);

    cubey::ProjectContext context(jobs, uploads, captures, &gpu);

    require(context.has_gpu(), "project context should report attached GPU services");
    require(&context.gpu() == &gpu, "project context should expose attached GPU services");
}

void test_project_gpu_services_submit_and_wait_runs_on_owner_thread() {
    cubey::UploadQueue uploads;
    cubey::vulkan::SubmissionCoordinator submission = fake_submission();
    cubey::vulkan::GpuRuntime runtime({
        .device = fake_device(),
        .submission = &submission,
    });
    cubey::ProjectGpuServices gpu(runtime, uploads);
    std::thread::id caller_thread = std::this_thread::get_id();
    std::thread::id work_thread{};

    static_cast<void>(gpu.submit_and_wait({
        .label = "project gpu work",
        .work =
            [&work_thread](cubey::vulkan::GpuOwnerContext& owner) {
                require(owner.is_owner_thread(),
                        "project GPU work should execute on the owner thread");
                work_thread = std::this_thread::get_id();
            },
    }));

    require(work_thread != std::thread::id{}, "project GPU work should run");
    require(work_thread != caller_thread,
            "project GPU work should not run on the caller thread by default");
}

void test_project_gpu_services_wait_queue_idle_runs_on_owner_thread() {
    cubey::UploadQueue uploads;
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
    cubey::ProjectGpuServices gpu(runtime, uploads);

    gpu.wait_queue_idle("project queue idle");

    require(wait_thread != std::thread::id{}, "project GPU queue idle should wait");
    require(wait_thread != caller_thread,
            "project GPU queue idle should execute on the owner thread by default");
}

void test_project_gpu_services_enqueue_uploads_and_track_completion() {
    cubey::UploadQueue uploads;
    cubey::vulkan::SubmissionCoordinator submission = fake_submission();
    cubey::vulkan::GpuRuntime runtime({
        .device = fake_device(),
        .submission = &submission,
        .execution_mode = cubey::vulkan::GpuRuntimeExecutionMode::Inline,
    });
    cubey::ProjectGpuServices gpu(runtime, uploads);
    cubey::UploadTicket upload = uploads.enqueue({
        .label = "vertex upload",
        .bytes = {1, 2, 3, 4},
    });

    std::vector<std::string> copied_labels;
    cubey::vulkan::GpuSubmissionTicket submitted{};
    cubey::ProjectGpuUploadDrainResult enqueued = gpu.enqueue_pending_uploads(
        [&](const cubey::QueuedUpload& queued, cubey::vulkan::GpuOwnerContext& owner) {
            require(owner.is_owner_thread(), "uploads should execute on the GPU owner thread");
            copied_labels.push_back(queued.ticket.label);
            require(queued.bytes == std::vector<std::uint8_t>({1, 2, 3, 4}),
                    "upload callback should receive owned upload bytes");
            submitted = owner.submission().submit_and_wait(
                {.command_buffers = {reinterpret_cast<VkCommandBuffer>(0x57)}}, "submit upload",
                "wait upload");
        });

    require(enqueued.upload_count == 1, "GPU services should drain one pending upload");
    require(enqueued.work_tickets.size() == 1, "GPU services should enqueue one GPU work item");
    require(uploads.empty(), "GPU services should drain pending upload storage");
    require(uploads.status(upload).state == cubey::UploadState::Pending,
            "upload should remain pending until GPU work executes");

    const cubey::vulkan::GpuDrainResult drain = gpu.drain();

    require(drain.completed_submission == submitted,
            "GPU services should report the completed submission after drain");
    require(copied_labels.size() == 1 && copied_labels.front() == "vertex upload",
            "GPU services should execute upload work");
    const cubey::UploadStatus completed = uploads.status(upload);
    require(completed.state == cubey::UploadState::Completed,
            "successful GPU upload should mark ticket completed");
    require(completed.completed_submission == submitted,
            "completed upload should record completed submission ticket");

}

void test_project_gpu_services_mark_failed_uploads() {
    cubey::UploadQueue uploads;
    cubey::vulkan::SubmissionCoordinator submission = fake_submission();
    cubey::vulkan::GpuRuntime runtime({
        .device = fake_device(),
        .submission = &submission,
        .execution_mode = cubey::vulkan::GpuRuntimeExecutionMode::Inline,
    });
    cubey::ProjectGpuServices gpu(runtime, uploads);
    cubey::UploadTicket upload = uploads.enqueue({.label = "broken", .bytes = {9}});

    static_cast<void>(gpu.enqueue_pending_uploads(
        [](const cubey::QueuedUpload&, cubey::vulkan::GpuOwnerContext&) {
            throw std::runtime_error("copy failed");
        }));

    bool saw_failure = false;
    try {
        static_cast<void>(gpu.drain());
    } catch (const std::runtime_error& error) {
        saw_failure = std::string(error.what()) == "copy failed";
    }

    require(saw_failure, "GPU services should propagate upload execution failures");
    const cubey::UploadStatus failed = uploads.status(upload);
    require(failed.state == cubey::UploadState::Failed,
            "failed GPU upload should mark ticket failed");
    require(failed.error == "copy failed", "failed GPU upload should preserve failure message");
}

void test_project_gpu_services_tracks_failed_rgba8_readbacks() {
    cubey::UploadQueue uploads;
    cubey::vulkan::SubmissionCoordinator submission = fake_submission();
    cubey::vulkan::GpuRuntime runtime({
        .device = fake_device(),
        .submission = &submission,
        .execution_mode = cubey::vulkan::GpuRuntimeExecutionMode::Inline,
    });
    cubey::ProjectGpuServices gpu(runtime, uploads);

    cubey::ProjectGpuReadbackTicket ticket =
        gpu.enqueue_rgba8_image_readback(VK_NULL_HANDLE, {2, 3}, "broken readback");
    cubey::ProjectGpuReadbackStatus pending = gpu.readback_status(ticket);
    require(ticket.id != 0, "readback ticket should receive an id");
    require(ticket.label == "broken readback", "readback ticket should preserve its label");
    require(ticket.byte_count == 24, "readback ticket should describe RGBA8 byte count");
    require(pending.state == cubey::ProjectGpuReadbackState::Pending,
            "readback should stay pending until GPU work executes");
    require(pending.byte_count == 24, "pending readback status should expose byte count");

    bool saw_failure = false;
    try {
        static_cast<void>(gpu.drain());
    } catch (const std::runtime_error& error) {
        saw_failure = std::string(error.what()) == "project GPU readback requires a source image";
    }

    require(saw_failure, "GPU services should propagate readback execution failures");
    const cubey::ProjectGpuReadbackStatus failed = gpu.readback_status(ticket);
    require(failed.state == cubey::ProjectGpuReadbackState::Failed,
            "failed readback should update ticket status");
    require(failed.error == "project GPU readback requires a source image",
            "failed readback should preserve failure message");
}

void test_project_gpu_services_rejects_invalid_rgba8_readback_requests() {
    cubey::UploadQueue uploads;
    cubey::vulkan::SubmissionCoordinator submission = fake_submission();
    cubey::vulkan::GpuRuntime runtime({
        .device = fake_device(),
        .submission = &submission,
        .execution_mode = cubey::vulkan::GpuRuntimeExecutionMode::Inline,
    });
    cubey::ProjectGpuServices gpu(runtime, uploads);

    bool rejected_extent = false;
    try {
        static_cast<void>(
            gpu.enqueue_rgba8_image_readback(reinterpret_cast<VkImage>(0x58), {0, 3}, "empty"));
    } catch (const std::runtime_error& error) {
        rejected_extent =
            std::string(error.what()) == "project GPU readback extent must be positive";
    }
    require(rejected_extent, "readback should reject zero-width extents before enqueue");

    bool rejected_unknown = false;
    try {
        static_cast<void>(
            gpu.readback_status(cubey::ProjectGpuReadbackTicket{.id = 404, .label = "missing"}));
    } catch (const std::runtime_error& error) {
        rejected_unknown = std::string(error.what()) == "unknown project GPU readback ticket";
    }
    require(rejected_unknown, "readback status should reject unknown tickets");
}
