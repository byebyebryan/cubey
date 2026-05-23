#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace cubey::profiling {

enum class ProfileSpanKind {
    Cpu,
    Gpu,
};

struct ProfileFrameRecord {
    std::uint64_t frame_index = 0;
    double delta_seconds = 0.0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t triangles = 0;
    bool memory_budget_available = false;
    std::uint64_t device_local_usage = 0;
    std::uint64_t device_local_budget = 0;
    std::uint64_t device_local_heap_size = 0;
};

struct ProfileSpanRecord {
    std::uint64_t frame_index = 0;
    ProfileSpanKind kind = ProfileSpanKind::Cpu;
    std::string label;
    double start_milliseconds = 0.0;
    double duration_milliseconds = 0.0;
};

struct ProfileMetricRecord {
    std::uint64_t frame_index = 0;
    std::string category;
    std::string name;
    double value = 0.0;
};

struct ProfileRecorderConfig {
    std::filesystem::path output_prefix;
    std::uint32_t warmup_frames = 0;
};

class ProfileRecorder;

class ScopedCpuProfileSpan {
  public:
    ScopedCpuProfileSpan() = default;
    ScopedCpuProfileSpan(ProfileRecorder* recorder, std::uint64_t frame_index,
                         std::string_view label);
    ~ScopedCpuProfileSpan();

    ScopedCpuProfileSpan(const ScopedCpuProfileSpan&) = delete;
    ScopedCpuProfileSpan& operator=(const ScopedCpuProfileSpan&) = delete;
    ScopedCpuProfileSpan(ScopedCpuProfileSpan&& other) noexcept;
    ScopedCpuProfileSpan& operator=(ScopedCpuProfileSpan&& other) noexcept;

  private:
    using Clock = std::chrono::steady_clock;

    void finish() noexcept;

    ProfileRecorder* recorder_ = nullptr;
    std::uint64_t frame_index_ = 0;
    std::string label_;
    Clock::time_point start_{};
};

class ProfileRecorder {
  public:
    explicit ProfileRecorder(ProfileRecorderConfig config);

    ProfileRecorder(const ProfileRecorder&) = delete;
    ProfileRecorder& operator=(const ProfileRecorder&) = delete;

    [[nodiscard]] bool enabled() const noexcept {
        return !config_.output_prefix.empty();
    }

    [[nodiscard]] const ProfileRecorderConfig& config() const noexcept {
        return config_;
    }

    [[nodiscard]] bool should_record_frame(std::uint64_t frame_index) const noexcept;

    [[nodiscard]] ScopedCpuProfileSpan cpu_span(std::uint64_t frame_index, std::string_view label);
    void record_frame(const ProfileFrameRecord& frame);
    void record_cpu_span(std::uint64_t frame_index, std::string_view label,
                         double duration_milliseconds);
    void record_gpu_span(std::uint64_t frame_index, std::string_view label,
                         double duration_milliseconds);
    void record_metric(std::uint64_t frame_index, std::string_view category, std::string_view name,
                       double value);

    [[nodiscard]] std::vector<ProfileFrameRecord> frame_records() const;
    [[nodiscard]] std::vector<ProfileSpanRecord> span_records() const;
    [[nodiscard]] std::vector<ProfileMetricRecord> metric_records() const;

    void write_outputs() const;

  private:
    friend class ScopedCpuProfileSpan;
    using Clock = std::chrono::steady_clock;

    void record_cpu_span(std::uint64_t frame_index, std::string_view label, Clock::time_point start,
                         Clock::time_point end);
    void record_span(ProfileSpanRecord span);
    [[nodiscard]] double milliseconds_since_start(Clock::time_point time) const;

    ProfileRecorderConfig config_;
    Clock::time_point start_ = Clock::now();
    mutable std::mutex mutex_;
    std::vector<ProfileFrameRecord> frames_;
    std::vector<ProfileSpanRecord> spans_;
    std::vector<ProfileMetricRecord> metrics_;
};

} // namespace cubey::profiling
