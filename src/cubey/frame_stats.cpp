#include <cubey/frame_stats.h>

#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace cubey {

FrameStats::FrameStats(double sample_seconds) : sample_seconds_(sample_seconds) {
    if (sample_seconds_ <= 0.0) {
        throw std::runtime_error("frame stats sample window must be positive");
    }
}

std::optional<FrameStatsSnapshot> FrameStats::record_frame(const FrameStatsSample& sample) {
    if (sample.delta_seconds < 0.0) {
        throw std::runtime_error("frame stats delta must be non-negative");
    }

    accumulated_seconds_ += sample.delta_seconds;
    ++accumulated_frames_;

    if (accumulated_seconds_ < sample_seconds_) {
        return std::nullopt;
    }

    const double frames = static_cast<double>(accumulated_frames_);
    const double fps = frames / accumulated_seconds_;
    const double frame_ms = (accumulated_seconds_ / frames) * 1000.0;
    const double megapixels_per_second =
        (static_cast<double>(sample.width) * static_cast<double>(sample.height) * fps) /
        1'000'000.0;

    FrameStatsSnapshot snapshot{
        fps,           frame_ms,         megapixels_per_second, sample.width,
        sample.height, sample.triangles, accumulated_frames_,
    };

    reset();
    return snapshot;
}

void FrameStats::reset() {
    accumulated_seconds_ = 0.0;
    accumulated_frames_ = 0;
}

std::string format_window_title(std::string_view base_title, const FrameStatsSnapshot& stats) {
    std::ostringstream title;
    title << base_title << " | " << std::fixed << std::setprecision(1) << stats.fps << " fps | "
          << std::setprecision(2) << stats.frame_ms << " ms | " << stats.width << "x"
          << stats.height << " | " << stats.triangles << " tris | " << std::setprecision(2)
          << stats.megapixels_per_second << " Mpix/s";
    return title.str();
}

} // namespace cubey
