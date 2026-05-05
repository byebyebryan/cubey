#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace cubey {

struct UploadTicket {
    std::uint64_t id = 0;
    std::string label;
    std::size_t byte_count = 0;
};

struct UploadRequest {
    std::string label;
    std::vector<std::uint8_t> bytes;
};

struct QueuedUpload {
    UploadTicket ticket;
    std::vector<std::uint8_t> bytes;
};

class UploadQueue {
public:
    UploadQueue() = default;

    UploadQueue(const UploadQueue&) = delete;
    UploadQueue& operator=(const UploadQueue&) = delete;
    UploadQueue(UploadQueue&&) = delete;
    UploadQueue& operator=(UploadQueue&&) = delete;

    [[nodiscard]] UploadTicket enqueue(UploadRequest request);
    [[nodiscard]] std::vector<QueuedUpload> drain();
    [[nodiscard]] std::size_t pending_count() const;
    [[nodiscard]] bool empty() const;

private:
    mutable std::mutex mutex_;
    std::uint64_t next_id_ = 1;
    std::vector<QueuedUpload> uploads_;
};

} // namespace cubey
