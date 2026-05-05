#include <cubey/project_runtime.h>

namespace cubey {

ProjectContext::ProjectContext(jobs::JobSystem& jobs, UploadQueue& uploads, CaptureQueue& captures,
                               FrameTicketIssuer& frame_tickets,
                               DeferredDestructionQueue& deferred_destruction)
    : jobs_(&jobs),
      uploads_(&uploads),
      captures_(&captures),
      frame_tickets_(&frame_tickets),
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

} // namespace cubey
