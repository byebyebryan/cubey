#include <cubey/core/profiling.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <sstream>
#include <span>
#include <stdexcept>
#include <utility>

namespace cubey::profiling {
namespace {

[[nodiscard]] const char* span_kind_name(ProfileSpanKind kind) {
    switch (kind) {
    case ProfileSpanKind::Cpu:
        return "cpu";
    case ProfileSpanKind::Gpu:
        return "gpu";
    }
    return "unknown";
}

[[nodiscard]] std::filesystem::path suffixed_path(std::filesystem::path prefix,
                                                  std::string_view suffix) {
    prefix += suffix;
    return prefix;
}

void ensure_parent_directory(const std::filesystem::path& path) {
    const std::filesystem::path parent = path.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent);
    }
}

[[nodiscard]] double bytes_to_mib(std::uint64_t bytes) {
    return static_cast<double>(bytes) / (1024.0 * 1024.0);
}

[[nodiscard]] std::string csv_escape(std::string_view value) {
    const bool needs_quotes = value.find_first_of(",\"\n\r") != std::string_view::npos;
    if (!needs_quotes) {
        return std::string(value);
    }

    std::string escaped;
    escaped.reserve(value.size() + 2U);
    escaped.push_back('"');
    for (const char character : value) {
        if (character == '"') {
            escaped.push_back('"');
        }
        escaped.push_back(character);
    }
    escaped.push_back('"');
    return escaped;
}

[[nodiscard]] std::string json_escape(std::string_view value) {
    std::string escaped;
    escaped.reserve(value.size());
    for (const char raw : value) {
        const auto character = static_cast<unsigned char>(raw);
        switch (character) {
        case '"':
            escaped += "\\\"";
            break;
        case '\\':
            escaped += "\\\\";
            break;
        case '\b':
            escaped += "\\b";
            break;
        case '\f':
            escaped += "\\f";
            break;
        case '\n':
            escaped += "\\n";
            break;
        case '\r':
            escaped += "\\r";
            break;
        case '\t':
            escaped += "\\t";
            break;
        default:
            if (character < 0x20U) {
                std::ostringstream stream;
                stream << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                       << static_cast<int>(character);
                escaped += stream.str();
            } else {
                escaped.push_back(raw);
            }
            break;
        }
    }
    return escaped;
}

struct SpanSummary {
    ProfileSpanKind kind = ProfileSpanKind::Cpu;
    std::string label;
    std::vector<double> durations;
};

[[nodiscard]] double percentile(std::vector<double> sorted_values, double value) {
    if (sorted_values.empty()) {
        return 0.0;
    }
    std::sort(sorted_values.begin(), sorted_values.end());
    const double scaled = value * static_cast<double>(sorted_values.size() - 1U);
    const auto index = static_cast<std::size_t>(scaled);
    return sorted_values.at(index);
}

void write_frames_csv(const std::filesystem::path& path,
                      std::span<const ProfileFrameRecord> frames) {
    ensure_parent_directory(path);
    std::ofstream file(path);
    if (!file) {
        throw std::runtime_error("failed to write profile frames CSV: " + path.string());
    }

    file << "frame_index,delta_ms,width,height,triangles,device_local_usage_mib,"
            "device_local_budget_mib,device_local_heap_mib,memory_budget_available\n";
    file << std::fixed << std::setprecision(6);
    for (const ProfileFrameRecord& frame : frames) {
        file << frame.frame_index << ',' << (frame.delta_seconds * 1000.0) << ',' << frame.width
             << ',' << frame.height << ',' << frame.triangles << ','
             << bytes_to_mib(frame.device_local_usage) << ','
             << bytes_to_mib(frame.device_local_budget) << ','
             << bytes_to_mib(frame.device_local_heap_size) << ','
             << (frame.memory_budget_available ? 1 : 0) << '\n';
    }
}

void write_passes_csv(const std::filesystem::path& path, std::span<const ProfileSpanRecord> spans) {
    ensure_parent_directory(path);
    std::ofstream file(path);
    if (!file) {
        throw std::runtime_error("failed to write profile passes CSV: " + path.string());
    }

    file << "frame_index,kind,label,start_ms,duration_ms\n";
    file << std::fixed << std::setprecision(6);
    for (const ProfileSpanRecord& span : spans) {
        file << span.frame_index << ',' << span_kind_name(span.kind) << ','
             << csv_escape(span.label) << ',' << span.start_milliseconds << ','
             << span.duration_milliseconds << '\n';
    }
}

void write_trace_json(const std::filesystem::path& path, std::span<const ProfileSpanRecord> spans) {
    ensure_parent_directory(path);
    std::ofstream file(path);
    if (!file) {
        throw std::runtime_error("failed to write profile trace JSON: " + path.string());
    }

    std::map<std::uint64_t, double> gpu_frame_offsets;
    bool first = true;
    file << "{\"traceEvents\":[\n";
    for (const ProfileSpanRecord& span : spans) {
        double start_milliseconds = span.start_milliseconds;
        if (span.kind == ProfileSpanKind::Gpu) {
            double& offset = gpu_frame_offsets[span.frame_index];
            start_milliseconds =
                static_cast<double>(span.frame_index) * 100.0 + offset;
            offset += span.duration_milliseconds;
        }

        if (!first) {
            file << ",\n";
        }
        first = false;
        file << "{\"name\":\"" << json_escape(span.label) << "\","
             << "\"cat\":\"" << span_kind_name(span.kind) << "\","
             << "\"ph\":\"X\","
             << "\"ts\":" << std::fixed << std::setprecision(3)
             << (start_milliseconds * 1000.0) << ','
             << "\"dur\":" << (span.duration_milliseconds * 1000.0) << ','
             << "\"pid\":1,"
             << "\"tid\":" << (span.kind == ProfileSpanKind::Gpu ? 2 : 1) << ','
             << "\"args\":{\"frame\":" << span.frame_index << "}}";
    }
    file << "\n]}\n";
}

void write_summary(const std::filesystem::path& path, std::span<const ProfileFrameRecord> frames,
                   std::span<const ProfileSpanRecord> spans) {
    ensure_parent_directory(path);
    std::ofstream file(path);
    if (!file) {
        throw std::runtime_error("failed to write profile summary: " + path.string());
    }

    file << "frames: " << frames.size() << '\n';
    if (!frames.empty()) {
        double total_delta = 0.0;
        for (const ProfileFrameRecord& frame : frames) {
            total_delta += frame.delta_seconds;
        }
        if (total_delta > 0.0) {
            file << std::fixed << std::setprecision(3)
                 << "average_fps: " << (static_cast<double>(frames.size()) / total_delta)
                 << '\n';
        }
    }

    std::map<std::pair<ProfileSpanKind, std::string>, SpanSummary> summaries;
    for (const ProfileSpanRecord& span : spans) {
        auto& summary = summaries[{span.kind, span.label}];
        summary.kind = span.kind;
        summary.label = span.label;
        summary.durations.push_back(span.duration_milliseconds);
    }

    file << "spans:\n";
    file << "kind,label,count,avg_ms,min_ms,median_ms,p95_ms,max_ms,total_ms\n";
    file << std::fixed << std::setprecision(6);
    for (const auto& entry : summaries) {
        const SpanSummary& summary = entry.second;
        if (summary.durations.empty()) {
            continue;
        }
        const auto minmax =
            std::minmax_element(summary.durations.begin(), summary.durations.end());
        double total = 0.0;
        for (const double duration : summary.durations) {
            total += duration;
        }
        const double average = total / static_cast<double>(summary.durations.size());
        file << span_kind_name(summary.kind) << ',' << csv_escape(summary.label) << ','
             << summary.durations.size() << ',' << average << ',' << *minmax.first << ','
             << percentile(summary.durations, 0.50) << ','
             << percentile(summary.durations, 0.95) << ',' << *minmax.second << ',' << total
             << '\n';
    }
}

} // namespace

ScopedCpuProfileSpan::ScopedCpuProfileSpan(ProfileRecorder* recorder, std::uint64_t frame_index,
                                           std::string_view label)
    : recorder_(recorder), frame_index_(frame_index), label_(label), start_(Clock::now()) {}

ScopedCpuProfileSpan::~ScopedCpuProfileSpan() {
    finish();
}

ScopedCpuProfileSpan::ScopedCpuProfileSpan(ScopedCpuProfileSpan&& other) noexcept
    : recorder_(std::exchange(other.recorder_, nullptr)), frame_index_(other.frame_index_),
      label_(std::move(other.label_)), start_(other.start_) {}

ScopedCpuProfileSpan& ScopedCpuProfileSpan::operator=(ScopedCpuProfileSpan&& other) noexcept {
    if (this == &other) {
        return *this;
    }
    finish();
    recorder_ = std::exchange(other.recorder_, nullptr);
    frame_index_ = other.frame_index_;
    label_ = std::move(other.label_);
    start_ = other.start_;
    return *this;
}

void ScopedCpuProfileSpan::finish() noexcept {
    if (recorder_ == nullptr) {
        return;
    }
    try {
        recorder_->record_cpu_span(frame_index_, label_, start_, Clock::now());
    } catch (...) {
    }
    recorder_ = nullptr;
}

ProfileRecorder::ProfileRecorder(ProfileRecorderConfig config) : config_(std::move(config)) {}

bool ProfileRecorder::should_record_frame(std::uint64_t frame_index) const noexcept {
    return enabled() && frame_index >= config_.warmup_frames;
}

ScopedCpuProfileSpan ProfileRecorder::cpu_span(std::uint64_t frame_index, std::string_view label) {
    return ScopedCpuProfileSpan(this, frame_index, label);
}

void ProfileRecorder::record_frame(const ProfileFrameRecord& frame) {
    if (!should_record_frame(frame.frame_index)) {
        return;
    }
    std::lock_guard lock(mutex_);
    frames_.push_back(frame);
}

void ProfileRecorder::record_cpu_span(std::uint64_t frame_index, std::string_view label,
                                      double duration_milliseconds) {
    record_span({
        .frame_index = frame_index,
        .kind = ProfileSpanKind::Cpu,
        .label = std::string(label),
        .start_milliseconds = 0.0,
        .duration_milliseconds = duration_milliseconds,
    });
}

void ProfileRecorder::record_gpu_span(std::uint64_t frame_index, std::string_view label,
                                      double duration_milliseconds) {
    record_span({
        .frame_index = frame_index,
        .kind = ProfileSpanKind::Gpu,
        .label = std::string(label),
        .start_milliseconds = 0.0,
        .duration_milliseconds = duration_milliseconds,
    });
}

std::vector<ProfileFrameRecord> ProfileRecorder::frame_records() const {
    std::lock_guard lock(mutex_);
    return frames_;
}

std::vector<ProfileSpanRecord> ProfileRecorder::span_records() const {
    std::lock_guard lock(mutex_);
    return spans_;
}

void ProfileRecorder::write_outputs() const {
    if (!enabled()) {
        return;
    }

    const std::vector<ProfileFrameRecord> frames = frame_records();
    const std::vector<ProfileSpanRecord> spans = span_records();
    write_frames_csv(suffixed_path(config_.output_prefix, ".frames.csv"), frames);
    write_passes_csv(suffixed_path(config_.output_prefix, ".passes.csv"), spans);
    write_trace_json(suffixed_path(config_.output_prefix, ".trace.json"), spans);
    write_summary(suffixed_path(config_.output_prefix, ".summary.txt"), frames, spans);
}

void ProfileRecorder::record_cpu_span(std::uint64_t frame_index, std::string_view label,
                                      Clock::time_point start, Clock::time_point end) {
    record_span({
        .frame_index = frame_index,
        .kind = ProfileSpanKind::Cpu,
        .label = std::string(label),
        .start_milliseconds = milliseconds_since_start(start),
        .duration_milliseconds = std::chrono::duration<double, std::milli>(end - start).count(),
    });
}

void ProfileRecorder::record_span(ProfileSpanRecord span) {
    if (!should_record_frame(span.frame_index)) {
        return;
    }
    std::lock_guard lock(mutex_);
    spans_.push_back(std::move(span));
}

double ProfileRecorder::milliseconds_since_start(Clock::time_point time) const {
    return std::chrono::duration<double, std::milli>(time - start_).count();
}

} // namespace cubey::profiling
