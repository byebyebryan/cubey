#include <cubey/core/profiling.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

[[nodiscard]] std::string read_text(const std::filesystem::path& path) {
    std::ifstream file(path);
    if (!file) {
        throw std::runtime_error("failed to read test profile output: " + path.string());
    }
    return std::string{std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}};
}

[[nodiscard]] std::filesystem::path unique_profile_dir(std::string_view name) {
    const auto tick = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() /
           ("cubey_profile_" + std::string(name) + "_" + std::to_string(tick));
}

} // namespace

void test_profile_recorder_skips_warmup_and_records_spans() {
    cubey::profiling::ProfileRecorder recorder({
        .output_prefix = std::filesystem::path("ignored"),
        .warmup_frames = 1,
    });

    recorder.record_frame({.frame_index = 0, .delta_seconds = 0.016});
    recorder.record_cpu_span(0, "warmup cpu", 1.0);
    recorder.record_gpu_span(0, "warmup gpu", 2.0);
    recorder.record_frame({.frame_index = 1, .delta_seconds = 0.010, .width = 64, .height = 32});
    recorder.record_gpu_span(1, "particle to grid", 4.5);

    const std::vector<cubey::profiling::ProfileFrameRecord> frames = recorder.frame_records();
    const std::vector<cubey::profiling::ProfileSpanRecord> spans = recorder.span_records();
    require(frames.size() == 1, "profile recorder should skip warmup frame records");
    require(spans.size() == 1, "profile recorder should skip warmup spans");
    require(frames.front().frame_index == 1, "profile frame should preserve frame index");
    require(spans.front().label == "particle to grid", "profile span should preserve label");
    require(spans.front().duration_milliseconds == 4.5,
            "profile span should preserve GPU duration");
}

void test_profile_recorder_writes_csv_summary_and_trace_outputs() {
    const std::filesystem::path dir = unique_profile_dir("outputs");
    const std::filesystem::path prefix = dir / "water3d";
    std::filesystem::remove_all(dir);

    cubey::profiling::ProfileRecorder recorder({
        .output_prefix = prefix,
        .warmup_frames = 0,
    });
    recorder.record_frame({
        .frame_index = 0,
        .delta_seconds = 0.020,
        .width = 1280,
        .height = 720,
        .triangles = 12,
        .memory_budget_available = true,
        .device_local_usage = 1024U * 1024U,
        .device_local_budget = 4U * 1024U * 1024U,
        .device_local_heap_size = 8U * 1024U * 1024U,
    });
    recorder.record_cpu_span(0, "host,update", 1.25);
    recorder.record_gpu_span(0, "surface \"composite\"", 2.5);
    recorder.write_outputs();

    const std::string frames = read_text(prefix.string() + ".frames.csv");
    const std::string passes = read_text(prefix.string() + ".passes.csv");
    const std::string trace = read_text(prefix.string() + ".trace.json");
    const std::string summary = read_text(prefix.string() + ".summary.txt");

    require(frames.find("frame_index,delta_ms") != std::string::npos,
            "profile frames CSV should contain a header");
    require(frames.find(",1.000000,4.000000,8.000000,1") != std::string::npos,
            "profile frames CSV should include memory budget columns in MiB");
    require(passes.find("\"host,update\"") != std::string::npos,
            "profile passes CSV should quote comma labels");
    require(passes.find("\"surface \"\"composite\"\"\"") != std::string::npos,
            "profile passes CSV should escape quotes");
    require(trace.find("\"ph\":\"X\"") != std::string::npos,
            "profile trace should contain complete events");
    require(trace.find("surface \\\"composite\\\"") != std::string::npos,
            "profile trace should JSON-escape labels");
    require(summary.find("kind,label,count,avg_ms,min_ms,median_ms,p95_ms,max_ms,total_ms") !=
                std::string::npos,
            "profile summary should contain aggregate timing columns");

    std::filesystem::remove_all(dir);
}
