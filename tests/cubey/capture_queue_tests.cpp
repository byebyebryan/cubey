#include <cubey/core/file_io.h>
#include <cubey/engine/capture_queue.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <future>
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
    bool fail_on_finish = false;
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
        if (state_->fail_on_finish)
            throw std::runtime_error("fake video finish failed");
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

void test_capture_backlog_consumes_failures_and_finishes_tail() {
    for (unsigned failed = 0; failed < 3; ++failed) {
        unsigned visited = 0;
        cubey::CaptureBacklog backlog(4);
        for (unsigned index = 0; index < 3; ++index) {
            auto job = std::async(std::launch::deferred, [&, index] {
                ++visited;
                if (index == failed)
                    throw std::runtime_error("original capture failure");
                if (failed == 0 && index == 2)
                    throw std::runtime_error("later capture failure");
            });
            backlog.enqueue({{}, cubey::jobs::JobHandle<void>(std::move(job))});
        }
        bool original = false;
        try {
            backlog.finish_all();
        } catch (const std::runtime_error& error) {
            original = std::string(error.what()) == "original capture failure";
        }
        require(original && visited == 3 && backlog.pending_count() == 0,
                "failed drain must consume every ticket and preserve its first error");
        backlog.finish_all();
    }
    cubey::CaptureBacklog backlog(2);
    unsigned visited = 0;
    auto failed = std::async(std::launch::deferred, [] { throw std::runtime_error("threshold"); });
    backlog.enqueue({{}, cubey::jobs::JobHandle<void>(std::move(failed))});
    auto tail = std::async(std::launch::deferred, [&] { ++visited; });
    bool original = false;
    try {
        backlog.enqueue({{}, cubey::jobs::JobHandle<void>(std::move(tail))});
    } catch (const std::runtime_error& error) {
        original = std::string(error.what()) == "threshold";
    }
    backlog.finish_all();
    require(original && visited == 1 && backlog.pending_count() == 0,
            "threshold failure must leave only unconsumed tickets for cleanup");
}

namespace {
struct BlockedEncoderState {
    std::promise<void> entered, release;
    std::vector<std::uint8_t> order;
    unsigned finishes = 0;
    bool fail = false;
};

class BlockedEncoder final : public cubey::VideoEncoder {
  public:
    explicit BlockedEncoder(std::shared_ptr<BlockedEncoderState> state)
        : state_(std::move(state)), release_(state_->release.get_future()) {}
    void write_frame(std::span<const std::uint8_t> frame) override {
        if (state_->order.empty()) {
            state_->entered.set_value();
            release_.wait();
        }
        if (state_->fail)
            throw std::runtime_error("blocked encoder failure");
        state_->order.push_back(frame.front());
    }
    void finish() override {
        ++state_->finishes;
    }

  private:
    std::shared_ptr<BlockedEncoderState> state_;
    std::future<void> release_;
};

void exercise_bounded_encoder(bool fail, bool close) {
    using namespace std::chrono_literals;
    auto state = std::make_shared<BlockedEncoderState>();
    state->fail = fail;
    cubey::QueuedVideoEncoder encoder(std::make_unique<BlockedEncoder>(state), 1, 1);
    const auto enqueue = [&](std::uint8_t identity) {
        encoder.enqueue_frame({1, 1, {identity, 0, 0, 255}});
    };
    enqueue(0);
    state->entered.get_future().wait();
    for (std::uint8_t index = 1; index <= 3; ++index)
        enqueue(index);
    std::promise<void> started;
    auto producer = std::async(std::launch::async, [&] {
        started.set_value();
        try {
            enqueue(4);
            return std::string{};
        } catch (const std::exception& error) {
            return std::string(error.what());
        }
    });
    started.get_future().wait();
    const bool blocked = producer.wait_for(100ms) == std::future_status::timeout;
    std::future<void> finishing;
    bool woke_on_close = true;
    if (close) {
        finishing = std::async(std::launch::async, [&] { encoder.finish(); });
        woke_on_close = producer.wait_for(2s) == std::future_status::ready;
    }
    // Always release the fake worker before checking assertions or destroying
    // futures, including on the old unbounded implementation.
    state->release.set_value();
    const auto error = producer.get();
    if (close)
        finishing.get();
    bool failed = false;
    try {
        encoder.finish();
    } catch (const std::runtime_error& original) {
        failed = std::string(original.what()) == "blocked encoder failure";
    }
    require(blocked, "video producer must block at the queue budget");
    require(woke_on_close, "finish must wake producers before the encoder completes");
    if (fail) {
        require(error == "blocked encoder failure" && failed && state->order.empty(),
                "worker failure must wake blocked admission and preserve its original error");
    } else {
        require(!failed && state->finishes == 1,
                "repeated finish must finalize the underlying encoder once");
        require(close ? error == "cannot enqueue video frames after finish" : error.empty(),
                "admission must report close or resume after capacity is available");
        const std::vector<std::uint8_t> expected = close ? std::vector<std::uint8_t>{0, 1, 2, 3}
                                                         : std::vector<std::uint8_t>{0, 1, 2, 3, 4};
        require(state->order == expected, "bounded encoding must retain accepted frame order");
    }
}
} // namespace

void test_capture_queue_video_backpressure_and_wakeups() {
    exercise_bounded_encoder(false, false);
    exercise_bounded_encoder(true, false);
    exercise_bounded_encoder(false, true);
}

void test_capture_queue_encodes_video_frames_in_order() {
    const std::shared_ptr<FakeVideoEncoderState> state = std::make_shared<FakeVideoEncoderState>();
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
    const std::shared_ptr<FakeVideoEncoderState> state = std::make_shared<FakeVideoEncoderState>();
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
    for (const bool fail_on_write : {false, true}) {
        const std::shared_ptr<FakeVideoEncoderState> state =
            std::make_shared<FakeVideoEncoderState>();
        state->fail_on_write = fail_on_write;
        state->fail_on_finish = true;
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
            propagated = std::string{error.what()} ==
                         (fail_on_write ? "fake video write failed" : "fake video finish failed");
        }

        require(propagated, "queued video encoder should propagate worker failures");
        require(state->finish_count == 1, "worker must attempt finalization exactly once");
    }
}
