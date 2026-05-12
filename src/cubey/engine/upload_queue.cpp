#include <cubey/engine/upload_queue.h>

#include <utility>

namespace cubey {

UploadTicket UploadQueue::enqueue(UploadRequest request) {
    std::scoped_lock lock(mutex_);

    UploadTicket ticket{
        .id = next_id_,
        .label = request.label,
        .byte_count = request.bytes.size(),
    };
    ++next_id_;

    uploads_.push_back({
        .ticket = ticket,
        .bytes = std::move(request.bytes),
    });
    statuses_.emplace(ticket.id, UploadStatus{});
    return ticket;
}

std::vector<QueuedUpload> UploadQueue::drain() {
    std::scoped_lock lock(mutex_);

    std::vector<QueuedUpload> uploads;
    uploads.swap(uploads_);
    return uploads;
}

UploadStatus UploadQueue::status(const UploadTicket& ticket) const {
    std::scoped_lock lock(mutex_);

    const auto it = statuses_.find(ticket.id);
    if (it == statuses_.end()) {
        return UploadStatus{};
    }
    return it->second;
}

void UploadQueue::mark_completed(const UploadTicket& ticket,
                                 vulkan::GpuSubmissionTicket completed_submission) {
    std::scoped_lock lock(mutex_);

    statuses_[ticket.id] = UploadStatus{
        .state = UploadState::Completed,
        .completed_submission = completed_submission,
        .error = {},
    };
}

void UploadQueue::mark_failed(const UploadTicket& ticket, std::string error) {
    std::scoped_lock lock(mutex_);

    statuses_[ticket.id] = UploadStatus{
        .state = UploadState::Failed,
        .completed_submission = {},
        .error = std::move(error),
    };
}

std::size_t UploadQueue::pending_count() const {
    std::scoped_lock lock(mutex_);

    return uploads_.size();
}

bool UploadQueue::empty() const {
    std::scoped_lock lock(mutex_);

    return uploads_.empty();
}

} // namespace cubey
