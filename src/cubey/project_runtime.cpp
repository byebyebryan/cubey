#include <cubey/project_runtime.h>

namespace cubey {

ProjectContext::ProjectContext(jobs::JobSystem& jobs, UploadQueue& uploads, CaptureQueue& captures,
                               FrameTicketIssuer& frame_tickets,
                               DeferredDestructionQueue& deferred_destruction)
    : jobs_(&jobs), uploads_(&uploads), captures_(&captures), frame_tickets_(&frame_tickets),
      deferred_destruction_(&deferred_destruction) {}

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

ProjectRuntimeServices::ProjectRuntimeServices(std::size_t worker_count)
    : jobs_(worker_count), captures_(jobs_) {}

ProjectContext ProjectRuntimeServices::context() {
    return ProjectContext(jobs_, uploads_, captures_, frame_tickets_, deferred_destruction_);
}

ProjectFrame ProjectRuntimeServices::begin_frame(const FrameTiming& timing) {
    return {
        .delta_seconds = timing.delta_seconds,
        .elapsed_seconds = timing.elapsed_seconds,
        .frame_index = timing.frame_index,
        .ticket = frame_tickets_.issue(),
    };
}

} // namespace cubey
