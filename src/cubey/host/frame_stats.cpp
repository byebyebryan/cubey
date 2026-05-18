#include <cubey/host/frame_stats.h>

#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace cubey::host {

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

    FrameStatsSnapshot snapshot =
        make_frame_stats_snapshot(sample, accumulated_seconds_, accumulated_frames_);
    reset();
    return snapshot;
}

void FrameStats::reset() {
    accumulated_seconds_ = 0.0;
    accumulated_frames_ = 0;
}

FrameStatsSnapshot make_frame_stats_snapshot(const FrameStatsSample& sample,
                                             double accumulated_seconds,
                                             std::uint64_t accumulated_frames) {
    if (accumulated_seconds <= 0.0) {
        throw std::runtime_error("frame stats accumulated seconds must be positive");
    }
    if (accumulated_frames == 0) {
        throw std::runtime_error("frame stats accumulated frames must be positive");
    }

    const double frames = static_cast<double>(accumulated_frames);
    const double fps = frames / accumulated_seconds;
    const double frame_ms = (accumulated_seconds / frames) * 1000.0;
    const double megapixels_per_second =
        (static_cast<double>(sample.width) * static_cast<double>(sample.height) * fps) /
        1'000'000.0;

    return {
        fps, frame_ms, megapixels_per_second, sample.width, sample.height, sample.triangles,
        accumulated_frames,
    };
}

std::string format_window_title(std::string_view base_title, const FrameStatsSnapshot& stats) {
    std::ostringstream title;
    title << base_title << " | " << std::fixed << std::setprecision(1) << stats.fps << " fps | "
          << std::setprecision(2) << stats.frame_ms << " ms | " << stats.width << "x"
          << stats.height << " | " << stats.triangles << " tris | " << std::setprecision(2)
          << stats.megapixels_per_second << " Mpix/s";
    return title.str();
}

std::string format_frame_stats_line(std::string_view label, const FrameStatsSnapshot& stats) {
    std::ostringstream line;
    line << label << ": " << std::fixed << std::setprecision(1) << stats.fps << " fps | "
         << std::setprecision(2) << stats.frame_ms << " ms | " << stats.width << "x"
         << stats.height << " | " << stats.triangles << " tris | " << std::setprecision(2)
         << stats.megapixels_per_second << " Mpix/s";
    return line.str();
}

std::string format_frame_stats_summary(std::string_view label, const FrameStatsSnapshot& stats,
                                       double accumulated_seconds) {
    std::ostringstream line;
    line << label << ": " << stats.frames << " frames in " << std::fixed
         << std::setprecision(3) << accumulated_seconds << " s | " << std::setprecision(1)
         << stats.fps << " fps | " << std::setprecision(2) << stats.frame_ms << " ms | "
         << stats.width << "x" << stats.height << " | " << stats.triangles << " tris | "
         << std::setprecision(2) << stats.megapixels_per_second << " Mpix/s";
    return line.str();
}

} // namespace cubey::host
