#include <cubey/project_gpu_services.h>
#include <cubey/project_runtime.h>

#include <vulkan/vulkan.h>

#include <stdexcept>
#include <string>
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
    cubey::FrameTicketIssuer tickets;
    cubey::DeferredDestructionQueue deferred;
    cubey::vulkan::SubmissionCoordinator submission = fake_submission();
    cubey::vulkan::GpuRuntime runtime({
        .device = fake_device(),
        .submission = &submission,
        .execution_mode = cubey::vulkan::GpuRuntimeExecutionMode::Inline,
    });
    cubey::ProjectGpuServices gpu(runtime, uploads, deferred);

    cubey::ProjectContext context(jobs, uploads, captures, tickets, deferred, &gpu);

    require(context.has_gpu(), "project context should report attached GPU services");
    require(&context.gpu() == &gpu, "project context should expose attached GPU services");
}

void test_project_gpu_services_enqueue_uploads_and_retire_completed_gpu_work() {
    cubey::UploadQueue uploads;
    cubey::DeferredDestructionQueue deferred;
    cubey::vulkan::SubmissionCoordinator submission = fake_submission();
    cubey::vulkan::GpuRuntime runtime({
        .device = fake_device(),
        .submission = &submission,
        .execution_mode = cubey::vulkan::GpuRuntimeExecutionMode::Inline,
    });
    cubey::ProjectGpuServices gpu(runtime, uploads, deferred);
    cubey::UploadTicket upload = uploads.enqueue({
        .label = "vertex upload",
        .bytes = {1, 2, 3, 4},
    });

    std::vector<std::string> copied_labels;
    cubey::FrameTicket submitted{};
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

    bool retired = false;
    deferred.defer_after(submitted, [&retired] { retired = true; });
    require(gpu.retire_deferred_destruction() == 1,
            "GPU services should retire work completed by the GPU runtime");
    require(retired, "GPU services should run retired deferred actions");
}

void test_project_gpu_services_mark_failed_uploads() {
    cubey::UploadQueue uploads;
    cubey::DeferredDestructionQueue deferred;
    cubey::vulkan::SubmissionCoordinator submission = fake_submission();
    cubey::vulkan::GpuRuntime runtime({
        .device = fake_device(),
        .submission = &submission,
        .execution_mode = cubey::vulkan::GpuRuntimeExecutionMode::Inline,
    });
    cubey::ProjectGpuServices gpu(runtime, uploads, deferred);
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
