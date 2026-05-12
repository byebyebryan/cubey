#include <cubey/runtime/project_gpu_services.h>
#include <cubey/runtime/project_runtime.h>

#include <memory>
#include <stdexcept>

namespace cubey {

ProjectContext::ProjectContext(jobs::JobSystem& jobs, UploadQueue& uploads, CaptureQueue& captures,
                               FrameTicketIssuer& frame_tickets,
                               DeferredDestructionQueue& deferred_destruction,
                               ProjectGpuServices* gpu)
    : jobs_(&jobs), uploads_(&uploads), captures_(&captures), frame_tickets_(&frame_tickets),
      deferred_destruction_(&deferred_destruction), gpu_(gpu) {}

jobs::JobSystem& ProjectContext::jobs() const {
    return *jobs_;
}

UploadQueue& ProjectContext::upload_queue() const {
    return *uploads_;
}

CaptureQueue& ProjectContext::capture_queue() const {
    return *captures_;
}

FrameTicketIssuer& ProjectContext::frame_tickets() const {
    return *frame_tickets_;
}

DeferredDestructionQueue& ProjectContext::deferred_destruction() const {
    return *deferred_destruction_;
}

bool ProjectContext::has_gpu() const noexcept {
    return gpu_ != nullptr;
}

ProjectGpuServices& ProjectContext::gpu() const {
    if (gpu_ == nullptr) {
        throw std::runtime_error("project context has no GPU services");
    }
    return *gpu_;
}

ProjectRuntimeServices::ProjectRuntimeServices(std::size_t worker_count)
    : jobs_(worker_count), captures_(jobs_) {}

ProjectContext ProjectRuntimeServices::context(ProjectGpuServices* gpu) {
    return {jobs_, uploads_, captures_, frame_tickets_, deferred_destruction_, gpu};
}

ProjectFrame ProjectRuntimeServices::begin_frame(const FrameTiming& timing) {
    return {
        .delta_seconds = timing.delta_seconds,
        .elapsed_seconds = timing.elapsed_seconds,
        .frame_index = timing.frame_index,
        .ticket = frame_tickets_.issue(),
    };
}

ProjectRuntimeAdapter::ProjectRuntimeAdapter(std::size_t worker_count) : services_(worker_count) {}

ProjectRuntimeAdapter::~ProjectRuntimeAdapter() = default;

ProjectContext ProjectRuntimeAdapter::context() {
    return services_.context(gpu_services_.get());
}

const ProjectFrame& ProjectRuntimeAdapter::frame_for_timing(const FrameTiming& timing) {
    if (!has_active_frame_ || active_frame_.frame_index != timing.frame_index ||
        active_frame_.elapsed_seconds != timing.elapsed_seconds) {
        active_frame_ = services_.begin_frame(timing);
        has_active_frame_ = true;
    }
    return active_frame_;
}

std::size_t ProjectRuntimeAdapter::retire_deferred_destruction() {
    ProjectContext active_context = context();
    return active_context.deferred_destruction().retire_completed(
        active_context.frame_tickets().current());
}

void ProjectRuntimeAdapter::attach_gpu(vulkan::GpuRuntime& gpu) {
    ProjectContext active_context = services_.context();
    gpu_services_ = std::make_unique<ProjectGpuServices>(gpu, active_context.upload_queue(),
                                                         active_context.deferred_destruction());
}

void ProjectRuntimeAdapter::detach_gpu() {
    gpu_services_.reset();
}

bool ProjectRuntimeAdapter::has_gpu() const noexcept {
    return gpu_services_ != nullptr;
}

ProjectGpuServices& ProjectRuntimeAdapter::gpu() const {
    if (gpu_services_ == nullptr) {
        throw std::runtime_error("project runtime adapter has no GPU services");
    }
    return *gpu_services_;
}

} // namespace cubey
