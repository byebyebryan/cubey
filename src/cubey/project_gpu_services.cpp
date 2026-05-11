#include <cubey/project_gpu_services.h>

#include <exception>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace cubey {

ProjectGpuServices::ProjectGpuServices(vulkan::GpuRuntime& gpu, UploadQueue& uploads,
                                       DeferredDestructionQueue& deferred_destruction)
    : gpu_(&gpu), uploads_(&uploads), deferred_destruction_(&deferred_destruction) {}

ProjectGpuUploadDrainResult
ProjectGpuServices::enqueue_pending_uploads(ProjectGpuUploadHandler handler) {
    if (!handler) {
        throw std::runtime_error("project GPU upload drain requires a handler");
    }

    std::vector<QueuedUpload> pending_uploads = uploads_->drain();
    ProjectGpuUploadDrainResult result{
        .upload_count = pending_uploads.size(),
        .work_tickets = {},
    };
    result.work_tickets.reserve(pending_uploads.size());

    for (QueuedUpload& upload : pending_uploads) {
        UploadTicket upload_ticket = upload.ticket;
        result.work_tickets.push_back(gpu_->enqueue({
            .label = upload_ticket.label,
            .work =
                [this, handler, upload = std::move(upload),
                 upload_ticket](vulkan::GpuOwnerContext& owner) mutable {
                    try {
                        handler(upload, owner);
                        uploads_->mark_completed(upload_ticket, owner.completed_submission());
                    } catch (const std::exception& error) {
                        uploads_->mark_failed(upload_ticket, error.what());
                        throw;
                    } catch (...) {
                        uploads_->mark_failed(upload_ticket, "unknown GPU upload failure");
                        throw;
                    }
                },
        }));
    }

    return result;
}

vulkan::GpuWorkTicket ProjectGpuServices::enqueue(vulkan::GpuWorkRequest request) {
    return gpu_->enqueue(std::move(request));
}

vulkan::GpuWorkTicket ProjectGpuServices::submit_and_wait(vulkan::GpuWorkRequest request) {
    return gpu_->submit_and_wait(std::move(request));
}

void ProjectGpuServices::wait_queue_idle(std::string label) {
    gpu_->wait_queue_idle(std::move(label));
}

vulkan::GpuDrainResult ProjectGpuServices::drain() {
    return gpu_->drain();
}

std::size_t ProjectGpuServices::retire_deferred_destruction() {
    const vulkan::GpuDrainResult result = gpu_->drain();
    return deferred_destruction_->retire_completed(result.completed_submission);
}

} // namespace cubey
