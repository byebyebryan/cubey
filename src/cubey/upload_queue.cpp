#include <cubey/upload_queue.h>

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
    return ticket;
}

std::vector<QueuedUpload> UploadQueue::drain() {
    std::scoped_lock lock(mutex_);

    std::vector<QueuedUpload> uploads;
    uploads.swap(uploads_);
    return uploads;
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
