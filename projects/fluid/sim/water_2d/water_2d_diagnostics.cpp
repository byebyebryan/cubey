#include "water_2d_diagnostics.h"

#include <cstring>
#include <stdexcept>
#include <string_view>

namespace cubey::projects::fluid::water_2d {

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

[[nodiscard]] bool should_record_water_2d_diagnostics(cubey::profiling::ProfileRecorder* recorder,
                                                      const Water2DConfig& config,
                                                      std::uint64_t frame_index) {
    if (recorder == nullptr || !config.profile_diagnostics ||
        config.profile_diagnostic_interval == 0U) {
        return false;
    }
    return recorder->should_record_frame(frame_index) &&
           (frame_index % config.profile_diagnostic_interval) == 0U;
}

namespace {

[[nodiscard]] std::uint32_t diagnostic_slot_value(const std::vector<std::uint8_t>& bytes,
                                                  Water2DDiagnosticSlot slot) {
    const std::size_t offset = static_cast<std::size_t>(slot) * sizeof(std::uint32_t);
    if (offset > bytes.size() || sizeof(std::uint32_t) > bytes.size() - offset) {
        throw std::runtime_error("water 2D diagnostics readback is too small");
    }
    std::uint32_t value = 0;
    std::memcpy(&value, bytes.data() + offset, sizeof(value));
    return value;
}

void record_metric(cubey::profiling::ProfileRecorder& recorder, std::uint64_t frame_index,
                   std::string_view category, std::string_view name, double value) {
    recorder.record_metric(frame_index, category, name, value);
}

} // namespace

void record_water_2d_diagnostics(cubey::profiling::ProfileRecorder& recorder,
                                 std::uint64_t frame_index, const Water2DConfig& config,
                                 const std::vector<std::uint8_t>& bytes) {
    const auto slot = [&bytes](Water2DDiagnosticSlot slot_index) {
        return diagnostic_slot_value(bytes, slot_index);
    };
    const double active_particles =
        static_cast<double>(slot(Water2DDiagnosticSlot::ActiveParticles));
    const double inactive_scan_particles =
        static_cast<double>(slot(Water2DDiagnosticSlot::InactiveScanParticles));
    const double nonempty_cells = static_cast<double>(slot(Water2DDiagnosticSlot::NonemptyCells));
    const double overpacked_cells =
        static_cast<double>(slot(Water2DDiagnosticSlot::OverpackedCells));
    const double overpacked_particles =
        static_cast<double>(slot(Water2DDiagnosticSlot::OverpackedParticles));
    const double transfer_truncated_particles =
        static_cast<double>(slot(Water2DDiagnosticSlot::TransferTruncatedParticles));
    const double max_cell_count = static_cast<double>(slot(Water2DDiagnosticSlot::MaxCellCount));
    const double particle_scan_count =
        static_cast<double>(slot(Water2DDiagnosticSlot::ParticleScanCount));

    record_metric(recorder, frame_index, "water_2d.workload", "active_particles", active_particles);
    record_metric(recorder, frame_index, "water_2d.workload", "inactive_scan_particles",
                  inactive_scan_particles);
    record_metric(recorder, frame_index, "water_2d.workload", "particle_scan_count",
                  particle_scan_count);
    record_metric(recorder, frame_index, "water_2d.workload", "nonempty_cells", nonempty_cells);
    record_metric(recorder, frame_index, "water_2d.workload", "overpacked_cells", overpacked_cells);
    record_metric(recorder, frame_index, "water_2d.workload", "overpacked_particles",
                  overpacked_particles);
    record_metric(recorder, frame_index, "water_2d.workload", "transfer_truncated_particles",
                  transfer_truncated_particles);
    record_metric(recorder, frame_index, "water_2d.workload", "max_cell_count", max_cell_count);
    record_metric(recorder, frame_index, "water_2d.workload", "avg_particles_per_nonempty_cell",
                  nonempty_cells > 0.0 ? active_particles / nonempty_cells : 0.0);
    record_metric(recorder, frame_index, "water_2d.workload", "nonempty_cell_ratio",
                  nonempty_cells / static_cast<double>(cell_count(config)));
}

} // namespace cubey::projects::fluid::water_2d
