#include <cubey/engine/capture_queue.h>

#include <cubey/core/image_io.h>

#include <algorithm>
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

[[nodiscard]] std::unique_ptr<VideoEncoder>
checked_video_encoder(std::unique_ptr<VideoEncoder> encoder) {
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

CaptureBacklog::CaptureBacklog(std::size_t drain_threshold) : drain_threshold_(drain_threshold) {
    if (drain_threshold_ == 0) {
        throw std::runtime_error("capture backlog drain threshold must be positive");
    }
}

void CaptureBacklog::enqueue(CaptureTicket ticket) {
    pending_.push_back(std::move(ticket));
    while (pending_.size() >= drain_threshold_) {
        auto current = std::move(pending_.front());
        pending_.pop_front();
        current.finish();
    }
}

void CaptureBacklog::finish_all() {
    std::exception_ptr failure;
    while (!pending_.empty()) {
        auto current = std::move(pending_.front());
        pending_.pop_front();
        try {
            current.finish();
        } catch (...) {
            if (failure == nullptr)
                failure = std::current_exception();
        }
    }
    if (failure != nullptr)
        std::rethrow_exception(failure);
}

class QueuedVideoEncoder::Impl {
  public:
    Impl(std::unique_ptr<VideoEncoder> encoder, std::uint32_t width, std::uint32_t height)
        : encoder_(checked_video_encoder(std::move(encoder))), width_(width), height_(height) {
        const auto bytes = video_frame_byte_size(width_, height_);
        // Bound queued payloads to three frames and 64 MiB, allowing one
        // oversized frame so high-resolution captures can still make progress.
        queue_limit_ =
            std::max<std::size_t>(1, std::min<std::size_t>(3, (64U * 1024U * 1024U) / bytes));
        thread_ = std::thread([this] { run(); });
    }

    ~Impl() {
        try {
            finish();
        } catch (...) {
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
            std::unique_lock lock(mutex_);
            condition_.wait(lock, [this] {
                return error_ != nullptr || closed_ || frames_.size() < queue_limit_;
            });
            if (error_ != nullptr)
                std::rethrow_exception(error_);
            if (closed_) {
                throw std::runtime_error("cannot enqueue video frames after finish");
            }
            frames_.push_back(std::move(request.rgba8));
        }
        condition_.notify_all();
    }

    void finish() {
        std::lock_guard finalizer(finish_mutex_);
        {
            std::lock_guard lock(mutex_);
            closed_ = true;
        }
        condition_.notify_all();
        if (thread_.joinable()) {
            thread_.join();
        }
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
                condition_.notify_all();
                encoder_->write_frame(frame);
            }
        } catch (...) {
            publish_failure(std::current_exception());
        }
        // Finalize once, even after a write failure. A cleanup error must not
        // replace the original error already reported to blocked producers.
        try {
            encoder_->finish();
        } catch (...) {
            publish_failure(std::current_exception());
        }
    }

    void publish_failure(std::exception_ptr failure) {
        {
            std::lock_guard lock(mutex_);
            if (error_ == nullptr)
                error_ = failure;
            closed_ = true;
            frames_.clear();
        }
        condition_.notify_all();
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
    std::size_t queue_limit_ = 0;
    std::mutex finish_mutex_;
    std::mutex mutex_;
    std::condition_variable condition_;
    std::deque<std::vector<std::uint8_t>> frames_;
    std::exception_ptr error_;
    bool closed_ = false;
    std::thread thread_;
};

QueuedVideoEncoder::QueuedVideoEncoder(std::unique_ptr<VideoEncoder> encoder, std::uint32_t width,
                                       std::uint32_t height)
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
