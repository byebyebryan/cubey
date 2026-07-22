#include <cubey/engine/project_gpu_services.h>
#include <cubey/engine/project_runtime.h>

#include <memory>
#include <stdexcept>

namespace cubey {

ProjectContext::ProjectContext(jobs::JobSystem& jobs, UploadQueue& uploads, CaptureQueue& captures,
                               ProjectGpuServices* gpu)
    : jobs_(&jobs), uploads_(&uploads), captures_(&captures), gpu_(gpu) {}

jobs::JobSystem& ProjectContext::jobs() const {
    return *jobs_;
}

UploadQueue& ProjectContext::upload_queue() const {
    return *uploads_;
}

CaptureQueue& ProjectContext::capture_queue() const {
    return *captures_;
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
    return {jobs_, uploads_, captures_, gpu};
}

ProjectFrame ProjectRuntimeServices::begin_frame(const FrameTiming& timing) {
    return {
        .delta_seconds = timing.delta_seconds,
        .elapsed_seconds = timing.elapsed_seconds,
        .frame_index = timing.frame_index,
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

void ProjectRuntimeAdapter::attach_gpu(vulkan::GpuRuntime& gpu) {
    ProjectContext active_context = services_.context();
    gpu_services_ = std::make_unique<ProjectGpuServices>(gpu, active_context.upload_queue());
}

void ProjectRuntimeAdapter::attach_gpu_if_needed(vulkan::GpuRuntime& gpu) {
    if (!has_gpu()) {
        attach_gpu(gpu);
    }
}

void ProjectRuntimeAdapter::detach_gpu() {
    gpu_services_.reset();
}

void ProjectRuntimeAdapter::detach_gpu_if_attached() {
    if (has_gpu()) {
        detach_gpu();
    }
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
