#include "smoke_2d_diagnostics.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <string_view>

namespace cubey::projects::fluid::smoke_2d {

[[nodiscard]] std::uint64_t profile_frame_index(const ProjectFrame& frame) {
    return frame.frame_index == 0 ? 0 : frame.frame_index - 1U;
}

[[nodiscard]] std::uint64_t collected_profile_frame_index(const ProjectFrame& frame,
                                                          cubey::render::FrameSlot frame_slot) {
    if (frame.frame_index > frame_slot.count) {
        return frame.frame_index - static_cast<std::uint64_t>(frame_slot.count) - 1U;
    }
    return profile_frame_index(frame);
}

void record_gpu_timings(cubey::profiling::ProfileRecorder* recorder, std::uint64_t frame_index,
                        const std::vector<cubey::vulkan::GpuPassTiming>& timings) {
    if (recorder == nullptr) {
        return;
    }
    for (const cubey::vulkan::GpuPassTiming& timing : timings) {
        recorder->record_gpu_span(frame_index, timing.label, timing.milliseconds);
    }
}

[[nodiscard]] bool should_record_smoke_2d_diagnostics(cubey::profiling::ProfileRecorder* recorder,
                                                      const Smoke2DConfig& config,
                                                      std::uint64_t frame_index) {
    if (recorder == nullptr || !config.profile_diagnostics ||
        config.profile_diagnostic_interval == 0U) {
        return false;
    }
    return recorder->should_record_frame(frame_index) &&
           (frame_index % config.profile_diagnostic_interval) == 0U;
}

namespace {

void require_min_byte_size(const std::vector<std::uint8_t>& bytes, std::size_t byte_size,
                           const char* label) {
    if (bytes.size() < byte_size) {
        throw std::runtime_error(label);
    }
}

[[nodiscard]] SmokeCellGpu cell_at(const std::vector<std::uint8_t>& bytes, std::size_t index) {
    const std::size_t offset = index * sizeof(SmokeCellGpu);
    if (offset > bytes.size() || sizeof(SmokeCellGpu) > bytes.size() - offset) {
        throw std::runtime_error("smoke 2D field diagnostics readback is too small");
    }
    SmokeCellGpu cell;
    std::memcpy(&cell, bytes.data() + offset, sizeof(cell));
    return cell;
}

[[nodiscard]] float scalar_at(const std::vector<std::uint8_t>& bytes, std::size_t index,
                              const char* label) {
    const std::size_t offset = index * sizeof(float);
    if (offset > bytes.size() || sizeof(float) > bytes.size() - offset) {
        throw std::runtime_error(label);
    }
    float value = 0.0F;
    std::memcpy(&value, bytes.data() + offset, sizeof(value));
    return value;
}

[[nodiscard]] double dye_luminance(const SmokeCellGpu& cell) {
    return (static_cast<double>(cell.dye[0]) * 0.2126) +
           (static_cast<double>(cell.dye[1]) * 0.7152) +
           (static_cast<double>(cell.dye[2]) * 0.0722);
}

[[nodiscard]] double velocity_speed(const SmokeCellGpu& cell) {
    const double x = static_cast<double>(cell.velocity[0]);
    const double y = static_cast<double>(cell.velocity[1]);
    return std::sqrt((x * x) + (y * y));
}

void record_metric(cubey::profiling::ProfileRecorder& recorder, std::uint64_t frame_index,
                   std::string_view category, std::string_view name, double value) {
    recorder.record_metric(frame_index, category, name, value);
}

} // namespace

void record_smoke_2d_diagnostics(cubey::profiling::ProfileRecorder& recorder,
                                 std::uint64_t frame_index, const Smoke2DConfig& config,
                                 const std::vector<std::uint8_t>& field_bytes,
                                 const std::vector<std::uint8_t>& divergence_bytes,
                                 const std::vector<std::uint8_t>& curl_bytes) {
    const std::size_t cells = field_cell_count(config);
    require_min_byte_size(field_bytes, cells * sizeof(SmokeCellGpu),
                          "smoke 2D field diagnostics readback is too small");
    require_min_byte_size(divergence_bytes, cells * sizeof(float),
                          "smoke 2D divergence diagnostics readback is too small");
    require_min_byte_size(curl_bytes, cells * sizeof(float),
                          "smoke 2D curl diagnostics readback is too small");

    double active_cells = 0.0;
    double dye_sum = 0.0;
    double dye_max = 0.0;
    double speed_sum = 0.0;
    double speed_max = 0.0;
    double divergence_abs_sum = 0.0;
    double divergence_abs_max = 0.0;
    double curl_abs_sum = 0.0;
    double curl_abs_max = 0.0;

    for (std::size_t index = 0; index < cells; ++index) {
        const SmokeCellGpu cell = cell_at(field_bytes, index);
        const double dye = dye_luminance(cell);
        const double speed = velocity_speed(cell);
        if (dye > 0.005 || speed > 0.01) {
            active_cells += 1.0;
        }
        dye_sum += dye;
        dye_max = std::max(dye_max, dye);
        speed_sum += speed;
        speed_max = std::max(speed_max, speed);

        const double divergence =
            std::abs(static_cast<double>(scalar_at(divergence_bytes, index,
                                                   "smoke 2D divergence diagnostics readback is "
                                                   "too small")));
        divergence_abs_sum += divergence;
        divergence_abs_max = std::max(divergence_abs_max, divergence);

        const double curl = std::abs(static_cast<double>(
            scalar_at(curl_bytes, index, "smoke 2D curl diagnostics readback is too small")));
        curl_abs_sum += curl;
        curl_abs_max = std::max(curl_abs_max, curl);
    }

    const double cell_count = static_cast<double>(cells);
    record_metric(recorder, frame_index, "smoke_2d.field", "active_cells", active_cells);
    record_metric(recorder, frame_index, "smoke_2d.field", "active_cell_ratio",
                  active_cells / cell_count);
    record_metric(recorder, frame_index, "smoke_2d.field", "dye_luminance_sum", dye_sum);
    record_metric(recorder, frame_index, "smoke_2d.field", "dye_luminance_avg",
                  dye_sum / cell_count);
    record_metric(recorder, frame_index, "smoke_2d.field", "dye_luminance_max", dye_max);
    record_metric(recorder, frame_index, "smoke_2d.field", "velocity_speed_sum", speed_sum);
    record_metric(recorder, frame_index, "smoke_2d.field", "velocity_speed_avg",
                  speed_sum / cell_count);
    record_metric(recorder, frame_index, "smoke_2d.field", "velocity_speed_max", speed_max);
    record_metric(recorder, frame_index, "smoke_2d.solver", "divergence_abs_sum",
                  divergence_abs_sum);
    record_metric(recorder, frame_index, "smoke_2d.solver", "divergence_abs_avg",
                  divergence_abs_sum / cell_count);
    record_metric(recorder, frame_index, "smoke_2d.solver", "divergence_abs_max",
                  divergence_abs_max);
    record_metric(recorder, frame_index, "smoke_2d.solver", "curl_abs_sum", curl_abs_sum);
    record_metric(recorder, frame_index, "smoke_2d.solver", "curl_abs_avg",
                  curl_abs_sum / cell_count);
    record_metric(recorder, frame_index, "smoke_2d.solver", "curl_abs_max", curl_abs_max);
}

} // namespace cubey::projects::fluid::smoke_2d
