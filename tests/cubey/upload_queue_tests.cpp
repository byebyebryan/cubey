#include <cubey/upload_queue.h>

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

} // namespace

void test_upload_queue_owns_payload_until_drain() {
    cubey::UploadQueue queue;
    cubey::UploadTicket ticket = queue.enqueue({
        .label = "checkerboard texture",
        .bytes = {1, 2, 3, 4},
    });

    require(ticket.id == 1, "first upload ticket should start at id 1");
    require(ticket.label == "checkerboard texture", "ticket should preserve upload label");
    require(ticket.byte_count == 4, "ticket should record byte count");
    require(queue.pending_count() == 1, "queue should count pending upload");

    std::vector<cubey::QueuedUpload> uploads = queue.drain();
    require(uploads.size() == 1, "drain should return queued upload");
    require(uploads.front().ticket.id == ticket.id, "drained upload should preserve ticket");
    require(uploads.front().bytes == std::vector<std::uint8_t>({1, 2, 3, 4}),
            "queue should own uploaded bytes until drain");
    require(queue.empty(), "drain should leave queue empty");
}

void test_upload_queue_drains_in_submission_order() {
    cubey::UploadQueue queue;
    cubey::UploadTicket first = queue.enqueue({.label = "first", .bytes = {1}});
    cubey::UploadTicket second = queue.enqueue({.label = "second", .bytes = {2}});

    std::vector<cubey::QueuedUpload> uploads = queue.drain();
    require(uploads.size() == 2, "drain should return all queued uploads");
    require(uploads.at(0).ticket.id == first.id, "first upload should drain first");
    require(uploads.at(1).ticket.id == second.id, "second upload should drain second");
}
