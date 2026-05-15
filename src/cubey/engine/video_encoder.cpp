#include <cubey/engine/video_encoder.h>

#include <limits>
#include <stdexcept>
#include <utility>

namespace cubey {
namespace {

constexpr std::uint32_t kRgba8Channels = 4;

} // namespace

#if CUBEY_HAS_LIBAV
[[nodiscard]] bool libav_video_encoder_available();
[[nodiscard]] const char* libav_video_encoder_backend_name();
[[nodiscard]] std::unique_ptr<VideoEncoder> create_libav_video_encoder(VideoEncoderConfig config);
#endif

std::size_t video_frame_byte_size(std::uint32_t width, std::uint32_t height) {
    if (width == 0 || height == 0) {
        throw std::runtime_error("video frame dimensions must be positive");
    }

    const std::uint64_t pixels = static_cast<std::uint64_t>(width) * height;
    constexpr std::uint64_t max_size =
        static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max());
    if (pixels > max_size / kRgba8Channels) {
        throw std::runtime_error("video frame byte size overflows size_t");
    }
    return static_cast<std::size_t>(pixels * kRgba8Channels);
}

void validate_video_frame_size(std::uint32_t width, std::uint32_t height, std::size_t byte_count) {
    if (byte_count != video_frame_byte_size(width, height)) {
        throw std::runtime_error("video frame RGBA8 byte count does not match dimensions");
    }
}

void validate_video_encoder_config(const VideoEncoderConfig& config) {
    if (config.output_path.empty()) {
        throw std::runtime_error("video output path must not be empty");
    }
    if (config.fps == 0) {
        throw std::runtime_error("video fps must be positive");
    }
    static_cast<void>(video_frame_byte_size(config.width, config.height));
}

bool video_encoding_available() {
#if CUBEY_HAS_LIBAV
    return libav_video_encoder_available();
#else
    return false;
#endif
}

const char* video_encoding_backend_name() {
#if CUBEY_HAS_LIBAV
    return libav_video_encoder_backend_name();
#else
    return "unavailable";
#endif
}

std::unique_ptr<VideoEncoder> create_video_encoder(VideoEncoderConfig config) {
    validate_video_encoder_config(config);
#if CUBEY_HAS_LIBAV
    return create_libav_video_encoder(std::move(config));
#else
    static_cast<void>(config);
    throw std::runtime_error(
        "video capture requires libav/FFmpeg development libraries at configure time");
#endif
}

} // namespace cubey
