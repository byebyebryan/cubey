#pragma once

#include <cubey/core/jobs.h>
#include <cubey/engine/video_encoder.h>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

namespace cubey {

struct CaptureRequest {
    std::filesystem::path output_path;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<std::uint8_t> rgba8;
};

struct VideoFrameEncodeRequest {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<std::uint8_t> rgba8;
};

class CaptureTicket {
  public:
    CaptureTicket(std::filesystem::path output_path, jobs::JobHandle<void> job);

    CaptureTicket(const CaptureTicket&) = delete;
    CaptureTicket& operator=(const CaptureTicket&) = delete;
    CaptureTicket(CaptureTicket&&) noexcept = default;
    CaptureTicket& operator=(CaptureTicket&&) noexcept = default;

    [[nodiscard]] const std::filesystem::path& output_path() const {
        return output_path_;
    }

    [[nodiscard]] bool ready() const {
        return job_.ready();
    }

    void wait() const {
        job_.wait();
    }

    void finish() {
        job_.get();
    }

  private:
    std::filesystem::path output_path_;
    jobs::JobHandle<void> job_;
};

class QueuedVideoEncoder {
  public:
    QueuedVideoEncoder(std::unique_ptr<VideoEncoder> encoder, std::uint32_t width,
                       std::uint32_t height);
    ~QueuedVideoEncoder();

    QueuedVideoEncoder(const QueuedVideoEncoder&) = delete;
    QueuedVideoEncoder& operator=(const QueuedVideoEncoder&) = delete;
    QueuedVideoEncoder(QueuedVideoEncoder&&) = delete;
    QueuedVideoEncoder& operator=(QueuedVideoEncoder&&) = delete;

    void enqueue_frame(VideoFrameEncodeRequest request);
    void finish();

  private:
    class Impl;

    std::unique_ptr<Impl> impl_;
};

class CaptureQueue {
  public:
    explicit CaptureQueue(jobs::JobSystem& jobs);
    explicit CaptureQueue(jobs::InlineExecutor& jobs);

    CaptureQueue(const CaptureQueue&) = delete;
    CaptureQueue& operator=(const CaptureQueue&) = delete;
    CaptureQueue(CaptureQueue&&) = delete;
    CaptureQueue& operator=(CaptureQueue&&) = delete;

    [[nodiscard]] CaptureTicket enqueue_png(CaptureRequest request);
    [[nodiscard]] QueuedVideoEncoder start_video_encoding(VideoEncoderConfig config);

  private:
    using SubmitFunction = std::function<jobs::JobHandle<void>(std::function<void()>)>;

    explicit CaptureQueue(SubmitFunction submit);

    SubmitFunction submit_;
};

} // namespace cubey
