#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace cubey::host {

struct FrameStatsSnapshot {
    double fps = 0.0;
    double frame_ms = 0.0;
    double megapixels_per_second = 0.0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t triangles = 0;
    std::uint64_t frames = 0;
};

struct FrameStatsSample {
    double delta_seconds = 0.0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t triangles = 0;
};

class FrameStats {
  public:
    explicit FrameStats(double sample_seconds = 1.0);

    std::optional<FrameStatsSnapshot> record_frame(const FrameStatsSample& sample);
    void reset();

  private:
    double sample_seconds_ = 1.0;
    double accumulated_seconds_ = 0.0;
    std::uint64_t accumulated_frames_ = 0;
};

FrameStatsSnapshot make_frame_stats_snapshot(const FrameStatsSample& sample,
                                             double accumulated_seconds,
                                             std::uint64_t accumulated_frames);
std::string format_window_title(std::string_view base_title, const FrameStatsSnapshot& stats);
std::string format_frame_stats_line(std::string_view label, const FrameStatsSnapshot& stats);
std::string format_frame_stats_summary(std::string_view label, const FrameStatsSnapshot& stats,
                                       double accumulated_seconds);

} // namespace cubey::host
