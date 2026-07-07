#include <cubey/core/file_io.h>
#include <cubey/engine/capture_queue.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

} // namespace

static_assert(!std::is_copy_constructible_v<cubey::CaptureQueue>);
static_assert(!std::is_copy_assignable_v<cubey::CaptureQueue>);
static_assert(!std::is_move_constructible_v<cubey::CaptureQueue>);
static_assert(!std::is_move_assignable_v<cubey::CaptureQueue>);
static_assert(!std::is_copy_constructible_v<cubey::QueuedVideoEncoder>);
static_assert(!std::is_copy_assignable_v<cubey::QueuedVideoEncoder>);
static_assert(!std::is_move_constructible_v<cubey::QueuedVideoEncoder>);
static_assert(!std::is_move_assignable_v<cubey::QueuedVideoEncoder>);

namespace {

struct FakeVideoEncoderState {
    std::vector<std::vector<std::uint8_t>> frames;
    int finish_count = 0;
    bool fail_on_write = false;
};

class FakeVideoEncoder final : public cubey::VideoEncoder {
  public:
    explicit FakeVideoEncoder(std::shared_ptr<FakeVideoEncoderState> state)
        : state_(std::move(state)) {}

    void write_frame(std::span<const std::uint8_t> rgba8) override {
        if (state_->fail_on_write) {
            throw std::runtime_error("fake video write failed");
        }
        state_->frames.emplace_back(rgba8.begin(), rgba8.end());
    }

    void finish() override {
        ++state_->finish_count;
    }

  private:
    std::shared_ptr<FakeVideoEncoderState> state_;
};

} // namespace

void test_capture_queue_encodes_png_with_inline_executor() {
    const std::filesystem::path output =
        std::filesystem::temp_directory_path() / "cubey_capture_queue_inline.png";
    std::filesystem::remove(output);

    cubey::jobs::InlineExecutor jobs;
    cubey::CaptureQueue queue(jobs);
    cubey::CaptureTicket ticket = queue.enqueue_png({
        .output_path = output,
        .width = 2,
        .height = 2,
        .rgba8 = {255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 255},
    });

    require(ticket.ready(), "inline capture queue should finish before returning");
    ticket.finish();

    const std::vector<std::uint8_t> bytes = cubey::read_binary_file(output);
    constexpr std::array<std::uint8_t, 8> png_signature{
        0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n',
    };
    require(bytes.size() > png_signature.size(), "capture PNG should contain payload");
    require(std::equal(png_signature.begin(), png_signature.end(), bytes.begin()),
            "capture queue should write a PNG artifact");

    std::filesystem::remove(output);
}

void test_capture_queue_propagates_encoding_errors() {
    const std::filesystem::path output =
        std::filesystem::temp_directory_path() / "cubey_capture_queue_invalid.png";
    std::filesystem::remove(output);

    cubey::jobs::InlineExecutor jobs;
    cubey::CaptureQueue queue(jobs);
    cubey::CaptureTicket ticket = queue.enqueue_png({
        .output_path = output,
        .width = 2,
        .height = 2,
        .rgba8 = {255, 0, 0, 255},
    });

    bool propagated = false;
    try {
        ticket.finish();
    } catch (const std::runtime_error&) {
        propagated = true;
    }

    require(propagated, "capture ticket should propagate PNG encoding failures");
}

void test_capture_queue_creates_png_parent_directories() {
    const std::filesystem::path output_root =
        std::filesystem::temp_directory_path() / "cubey_capture_queue_parent";
    const std::filesystem::path output = output_root / "nested" / "capture.png";
    std::filesystem::remove_all(output_root);

    cubey::jobs::InlineExecutor jobs;
    cubey::CaptureQueue queue(jobs);
    cubey::CaptureTicket ticket = queue.enqueue_png({
        .output_path = output,
        .width = 1,
        .height = 1,
        .rgba8 = {255, 255, 255, 255},
    });
    ticket.finish();

    require(std::filesystem::exists(output), "capture queue should create PNG parent dirs");
    std::filesystem::remove_all(output_root);
}

void test_capture_backlog_drains_at_threshold() {
    const std::filesystem::path output_root =
        std::filesystem::temp_directory_path() / "cubey_capture_backlog";
    std::filesystem::remove_all(output_root);

    cubey::jobs::InlineExecutor jobs;
    cubey::CaptureQueue queue(jobs);
    cubey::CaptureBacklog backlog(2);

    for (int index = 0; index < 3; ++index) {
        backlog.enqueue(queue.enqueue_png({
            .output_path = output_root / ("capture_" + std::to_string(index) + ".png"),
            .width = 1,
            .height = 1,
            .rgba8 = {255, 255, 255, 255},
        }));
        require(backlog.pending_count() == 1,
                "capture backlog should drain once it reaches the threshold");
    }
    backlog.finish_all();

    require(backlog.pending_count() == 0, "capture backlog should drain all pending tickets");
    std::filesystem::remove_all(output_root);
}

void test_capture_queue_encodes_video_frames_in_order() {
    const std::shared_ptr<FakeVideoEncoderState> state =
        std::make_shared<FakeVideoEncoderState>();
    cubey::QueuedVideoEncoder encoder(std::make_unique<FakeVideoEncoder>(state), 2, 1);

    const std::vector<std::uint8_t> first{255, 0, 0, 255, 0, 255, 0, 255};
    const std::vector<std::uint8_t> second{0, 0, 255, 255, 255, 255, 255, 255};
    encoder.enqueue_frame({
        .width = 2,
        .height = 1,
        .rgba8 = first,
    });
    encoder.enqueue_frame({
        .width = 2,
        .height = 1,
        .rgba8 = second,
    });

    encoder.finish();

    require(state->finish_count == 1, "queued video encoder should finish once");
    require(state->frames.size() == 2, "queued video encoder should write both frames");
    require(state->frames[0] == first, "queued video encoder should preserve first frame");
    require(state->frames[1] == second, "queued video encoder should preserve frame order");
}

void test_capture_queue_video_encoder_rejects_dimension_mismatch() {
    const std::shared_ptr<FakeVideoEncoderState> state =
        std::make_shared<FakeVideoEncoderState>();
    cubey::QueuedVideoEncoder encoder(std::make_unique<FakeVideoEncoder>(state), 2, 1);

    bool rejected = false;
    try {
        encoder.enqueue_frame({
            .width = 1,
            .height = 2,
            .rgba8 = {255, 0, 0, 255, 0, 255, 0, 255},
        });
    } catch (const std::runtime_error&) {
        rejected = true;
    }

    require(rejected, "queued video encoder should reject mismatched frame dimensions");
    encoder.finish();
}

void test_capture_queue_video_encoder_propagates_worker_errors() {
    const std::shared_ptr<FakeVideoEncoderState> state =
        std::make_shared<FakeVideoEncoderState>();
    state->fail_on_write = true;
    cubey::QueuedVideoEncoder encoder(std::make_unique<FakeVideoEncoder>(state), 1, 1);
    encoder.enqueue_frame({
        .width = 1,
        .height = 1,
        .rgba8 = {255, 0, 0, 255},
    });

    bool propagated = false;
    try {
        encoder.finish();
    } catch (const std::runtime_error& error) {
        propagated = std::string{error.what()} == "fake video write failed";
    }

    require(propagated, "queued video encoder should propagate worker failures");
}
