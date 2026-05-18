#include <cubey/host/frame_stats.h>

#include <cmath>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void require_close(double actual, double expected, const char* message) {
    if (std::fabs(actual - expected) > 0.000001) {
        throw std::runtime_error(message);
    }
}

} // namespace

void test_host_frame_stats_publish_window_title_metrics() {
    cubey::host::FrameStats stats(0.5);

    std::optional<cubey::host::FrameStatsSnapshot> first = stats.record_frame({
        .delta_seconds = 0.25,
        .width = 100,
        .height = 50,
        .triangles = 12,
    });
    require(!first.has_value(), "stats should wait for the sample window");

    std::optional<cubey::host::FrameStatsSnapshot> second = stats.record_frame({
        .delta_seconds = 0.25,
        .width = 100,
        .height = 50,
        .triangles = 12,
    });
    if (!second.has_value()) {
        throw std::runtime_error("stats should publish when the sample window is reached");
    }
    const cubey::host::FrameStatsSnapshot snapshot = *second;
    require_close(snapshot.fps, 4.0, "stats should calculate fps");
    require_close(snapshot.frame_ms, 250.0, "stats should calculate frame milliseconds");
    require_close(snapshot.megapixels_per_second, 0.02, "stats should calculate pixel rate");
    if (snapshot.width != 100 || snapshot.height != 50 || snapshot.triangles != 12) {
        throw std::runtime_error("stats should preserve extent and triangle count");
    }

    const std::string title = cubey::host::format_window_title("cubey textured_cube", snapshot);
    require(title == "cubey textured_cube | 4.0 fps | 250.00 ms | 100x50 | 12 tris | 0.02 Mpix/s",
            "stats title should be stable");

    const std::string line = cubey::host::format_frame_stats_line("frame_stats", snapshot);
    require(line == "frame_stats: 4.0 fps | 250.00 ms | 100x50 | 12 tris | 0.02 Mpix/s",
            "stats log line should be stable");

    const std::string summary =
        cubey::host::format_frame_stats_summary("windowed_perf", snapshot, 0.5);
    require(summary ==
                "windowed_perf: 2 frames in 0.500 s | 4.0 fps | 250.00 ms | 100x50 | "
                "12 tris | 0.02 Mpix/s",
            "stats summary line should be stable");
}
