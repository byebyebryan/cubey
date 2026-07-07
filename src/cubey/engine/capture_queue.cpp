#include <cubey/engine/capture_queue.h>

#include <cubey/core/image_io.h>

#include <condition_variable>
#include <deque>
#include <exception>
#include <filesystem>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

namespace cubey {
namespace {

[[nodiscard]] std::unique_ptr<VideoEncoder> checked_video_encoder(
    std::unique_ptr<VideoEncoder> encoder) {
    if (!encoder) {
        throw std::runtime_error("queued video encoder requires an encoder");
    }
    return encoder;
}

void validate_capture_png_request(const CaptureRequest& request) {
    if (request.output_path.empty()) {
        throw std::runtime_error("capture PNG output path must not be empty");
    }
    try {
        validate_video_frame_size(request.width, request.height, request.rgba8.size());
    } catch (const std::runtime_error& error) {
        throw std::runtime_error(std::string{"capture PNG RGBA8 buffer is invalid: "} +
                                 error.what());
    }
}

void write_capture_png(CaptureRequest request) {
    validate_capture_png_request(request);
    if (request.output_path.has_parent_path()) {
        std::filesystem::create_directories(request.output_path.parent_path());
    }
    write_png_rgba8(request.output_path, request.width, request.height, request.rgba8);
}

} // namespace

CaptureTicket::CaptureTicket(std::filesystem::path output_path, jobs::JobHandle<void> job)
    : output_path_(std::move(output_path)), job_(std::move(job)) {}

CaptureBacklog::CaptureBacklog(std::size_t drain_threshold)
    : drain_threshold_(drain_threshold) {
    if (drain_threshold_ == 0) {
        throw std::runtime_error("capture backlog drain threshold must be positive");
    }
}

void CaptureBacklog::enqueue(CaptureTicket ticket) {
    pending_.push_back(std::move(ticket));
    while (pending_.size() >= drain_threshold_) {
        pending_.front().finish();
        pending_.pop_front();
    }
}

void CaptureBacklog::finish_all() {
    while (!pending_.empty()) {
        pending_.front().finish();
        pending_.pop_front();
    }
}

class QueuedVideoEncoder::Impl {
  public:
    Impl(std::unique_ptr<VideoEncoder> encoder, std::uint32_t width, std::uint32_t height)
        : encoder_(checked_video_encoder(std::move(encoder))), width_(width), height_(height),
          thread_([this] { run(); }) {
        static_cast<void>(video_frame_byte_size(width_, height_));
    }

    ~Impl() {
        if (!joined_) {
            try {
                finish();
            } catch (...) {
            }
        }
    }

    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;

    void enqueue_frame(VideoFrameEncodeRequest request) {
        rethrow_if_failed();
        if (request.width != width_ || request.height != height_) {
            throw std::runtime_error("queued video frame dimensions do not match encoder");
        }
        validate_video_frame_size(request.width, request.height, request.rgba8.size());
        {
            std::lock_guard lock(mutex_);
            if (closed_) {
                throw std::runtime_error("cannot enqueue video frames after finish");
            }
            frames_.push_back(std::move(request.rgba8));
        }
        condition_.notify_one();
    }

    void finish() {
        {
            std::lock_guard lock(mutex_);
            closed_ = true;
        }
        condition_.notify_one();
        if (thread_.joinable()) {
            thread_.join();
        }
        joined_ = true;
        rethrow_if_failed();
    }

  private:
    void run() noexcept {
        try {
            while (true) {
                std::vector<std::uint8_t> frame;
                {
                    std::unique_lock lock(mutex_);
                    condition_.wait(lock, [this] { return closed_ || !frames_.empty(); });
                    if (frames_.empty()) {
                        break;
                    }
                    frame = std::move(frames_.front());
                    frames_.pop_front();
                }
                encoder_->write_frame(frame);
            }
            encoder_->finish();
        } catch (...) {
            std::lock_guard lock(mutex_);
            error_ = std::current_exception();
        }
    }

    void rethrow_if_failed() {
        std::exception_ptr error;
        {
            std::lock_guard lock(mutex_);
            error = error_;
        }
        if (error != nullptr) {
            std::rethrow_exception(error);
        }
    }

    std::unique_ptr<VideoEncoder> encoder_;
    std::uint32_t width_ = 0;
    std::uint32_t height_ = 0;
    std::thread thread_;
    std::mutex mutex_;
    std::condition_variable condition_;
    std::deque<std::vector<std::uint8_t>> frames_;
    std::exception_ptr error_;
    bool closed_ = false;
    bool joined_ = false;
};

QueuedVideoEncoder::QueuedVideoEncoder(std::unique_ptr<VideoEncoder> encoder,
                                       std::uint32_t width, std::uint32_t height)
    : impl_(std::make_unique<Impl>(std::move(encoder), width, height)) {}

QueuedVideoEncoder::~QueuedVideoEncoder() = default;

void QueuedVideoEncoder::enqueue_frame(VideoFrameEncodeRequest request) {
    impl_->enqueue_frame(std::move(request));
}

void QueuedVideoEncoder::finish() {
    impl_->finish();
}

CaptureQueue::CaptureQueue(jobs::JobSystem& jobs)
    : CaptureQueue([&jobs](std::function<void()> job) { return jobs.submit(std::move(job)); }) {}

CaptureQueue::CaptureQueue(jobs::InlineExecutor& jobs)
    : CaptureQueue([&jobs](std::function<void()> job) { return jobs.submit(std::move(job)); }) {}

CaptureQueue::CaptureQueue(SubmitFunction submit) : submit_(std::move(submit)) {}

CaptureTicket CaptureQueue::enqueue_png(CaptureRequest request) {
    std::filesystem::path output_path = request.output_path;
    jobs::JobHandle<void> job =
        submit_([request = std::move(request)] { write_capture_png(std::move(request)); });
    return {std::move(output_path), std::move(job)};
}

QueuedVideoEncoder CaptureQueue::start_video_encoding(VideoEncoderConfig config) {
    validate_video_encoder_config(config);
    const std::uint32_t width = config.width;
    const std::uint32_t height = config.height;
    return QueuedVideoEncoder(create_video_encoder(std::move(config)), width, height);
}

} // namespace cubey
