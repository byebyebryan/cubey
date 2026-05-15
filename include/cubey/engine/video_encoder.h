#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>

namespace cubey {

struct VideoEncoderConfig {
    std::filesystem::path output_path;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t fps = 60;
};

class VideoEncoder {
  public:
    VideoEncoder() = default;
    virtual ~VideoEncoder() = default;

    VideoEncoder(const VideoEncoder&) = delete;
    VideoEncoder& operator=(const VideoEncoder&) = delete;
    VideoEncoder(VideoEncoder&&) = delete;
    VideoEncoder& operator=(VideoEncoder&&) = delete;

    virtual void write_frame(std::span<const std::uint8_t> rgba8) = 0;
    virtual void finish() = 0;
};

[[nodiscard]] std::size_t video_frame_byte_size(std::uint32_t width, std::uint32_t height);
void validate_video_frame_size(std::uint32_t width, std::uint32_t height, std::size_t byte_count);
void validate_video_encoder_config(const VideoEncoderConfig& config);

[[nodiscard]] bool video_encoding_available();
[[nodiscard]] const char* video_encoding_backend_name();
[[nodiscard]] std::unique_ptr<VideoEncoder> create_video_encoder(VideoEncoderConfig config);

} // namespace cubey
