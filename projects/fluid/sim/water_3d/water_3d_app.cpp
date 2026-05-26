#include "water_3d_app.h"

#include "water_3d_commands.h"
#include "water_3d_config.h"
#include "water_3d_gpu_resources.h"

#include <cubey/asset/hdr_image.h>
#include <cubey/core/profiling.h>
#include <cubey/engine/project_gpu_services.h>
#include <cubey/engine/project_runtime.h>
#include <cubey/host/frame_stats.h>
#include <cubey/host/headless_png_host.h>
#include <cubey/host/windowed_app.h>
#include <cubey/input/input.h>
#include <cubey/input/orbit_controller.h>
#include <cubey/render/generated_ibl.h>
#include <cubey/render/pass.h>
#include <cubey/render/render_graph.h>
#include <cubey/scene/camera_3d.h>
#include <cubey/vulkan/command_recorder.h>
#include <cubey/vulkan/device.h>
#include <cubey/vulkan/gpu_runtime.h>
#include <cubey/vulkan/immediate_commands.h>

#include <imgui.h>
#include <vulkan/vulkan.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

namespace cubey::projects::fluid::water_3d {
namespace {

using cubey::FrameTiming;
using cubey::ProjectFrame;
using cubey::host::FrameStatsSample;
using cubey::host::FrameStatsSnapshot;

constexpr float kCameraDistance = 4.0F;
constexpr float kCameraBaseYaw = -0.45F;
constexpr float kCameraBasePitch = -0.34F;
constexpr cubey::math::Vec3 kVolumeCenter{0.5F, 0.5F, 0.5F};

constexpr std::array<Water3DRenderView, 12> kRenderViews{
    Water3DRenderView::Surface,
    Water3DRenderView::Particles,
    Water3DRenderView::Cells,
    Water3DRenderView::Velocity,
    Water3DRenderView::Pressure,
    Water3DRenderView::Solid,
    Water3DRenderView::Overpack,
    Water3DRenderView::SurfaceDepth,
    Water3DRenderView::SurfaceThickness,
    Water3DRenderView::SurfaceNormals,
    Water3DRenderView::SurfaceFoam,
    Water3DRenderView::Whitewater,
};

constexpr std::array<Water3DTransferMode, 2> kTransferModes{
    Water3DTransferMode::Apic,
    Water3DTransferMode::PicFlip,
};

[[nodiscard]] double bytes_to_mib(VkDeviceSize bytes) {
    return static_cast<double>(bytes) / (1024.0 * 1024.0);
}

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

[[nodiscard]] std::uint32_t diagnostic_slot_value(const std::vector<std::uint8_t>& bytes,
                                                  Water3DDiagnosticSlot slot) {
    const std::size_t offset = static_cast<std::size_t>(slot) * sizeof(std::uint32_t);
    if (offset > bytes.size() || sizeof(std::uint32_t) > bytes.size() - offset) {
        throw std::runtime_error("water 3D diagnostics readback is too small");
    }
    std::uint32_t value = 0;
    std::memcpy(&value, bytes.data() + offset, sizeof(value));
    return value;
}

[[nodiscard]] std::uint64_t diagnostic_slot_u64(const std::vector<std::uint8_t>& bytes,
                                                Water3DDiagnosticSlot low_slot,
                                                Water3DDiagnosticSlot high_slot) {
    return static_cast<std::uint64_t>(diagnostic_slot_value(bytes, low_slot)) |
           (static_cast<std::uint64_t>(diagnostic_slot_value(bytes, high_slot)) << 32U);
}

[[nodiscard]] bool should_record_water_3d_diagnostics(cubey::profiling::ProfileRecorder* recorder,
                                                      const Water3DConfig& config,
                                                      std::uint64_t frame_index) {
    if (recorder == nullptr || !config.profile_diagnostics ||
        config.profile_diagnostic_interval == 0U) {
        return false;
    }
    return recorder->should_record_frame(frame_index) &&
           (frame_index % config.profile_diagnostic_interval) == 0U;
}

void record_metric(cubey::profiling::ProfileRecorder& recorder, std::uint64_t frame_index,
                   std::string_view category, std::string_view name, double value) {
    recorder.record_metric(frame_index, category, name, value);
}

void record_water_3d_diagnostics(cubey::profiling::ProfileRecorder& recorder,
                                 std::uint64_t frame_index, const Water3DConfig& config,
                                 const std::vector<std::uint8_t>& bytes) {
    const auto slot = [&bytes](Water3DDiagnosticSlot slot_index) {
        return diagnostic_slot_value(bytes, slot_index);
    };
    const double active_particles =
        static_cast<double>(slot(Water3DDiagnosticSlot::ActiveParticles));
    const double inactive_scan_particles =
        static_cast<double>(slot(Water3DDiagnosticSlot::InactiveScanParticles));
    const double out_of_bounds_particles =
        static_cast<double>(slot(Water3DDiagnosticSlot::OutOfBoundsParticles));
    const double nonempty_cells = static_cast<double>(slot(Water3DDiagnosticSlot::NonemptyCells));
    const double overpacked_cells =
        static_cast<double>(slot(Water3DDiagnosticSlot::OverpackedCells));
    const double overpacked_particles =
        static_cast<double>(slot(Water3DDiagnosticSlot::OverpackedParticles));
    const double max_cell_count = static_cast<double>(slot(Water3DDiagnosticSlot::MaxCellCount));
    const double active_faces = static_cast<double>(slot(Water3DDiagnosticSlot::ActiveFaces));
    const double active_face_dispatch_groups =
        static_cast<double>(slot(Water3DDiagnosticSlot::ActiveFaceDispatchGroups));
    const double particle_scan_count =
        static_cast<double>(slot(Water3DDiagnosticSlot::ParticleScanCount));
    const std::uint64_t divergence_abs_sum_fixed =
        static_cast<std::uint64_t>(slot(Water3DDiagnosticSlot::DivergenceAbsSumFixed)) |
        (static_cast<std::uint64_t>(slot(Water3DDiagnosticSlot::DivergenceAbsSumFixedHigh)) << 32U);
    const double divergence_abs_sum = static_cast<double>(divergence_abs_sum_fixed) /
                                      static_cast<double>(kWater3DDiagnosticDivergenceScale);
    const double divergence_abs_max =
        static_cast<double>(slot(Water3DDiagnosticSlot::DivergenceAbsMaxFixed)) /
        static_cast<double>(kWater3DDiagnosticDivergenceScale);
    const double divergent_cells = static_cast<double>(slot(Water3DDiagnosticSlot::DivergentCells));
    const double whitewater_emitted =
        static_cast<double>(slot(Water3DDiagnosticSlot::WhitewaterEmitted));
    const double whitewater_active =
        static_cast<double>(slot(Water3DDiagnosticSlot::WhitewaterActive));
    const double whitewater_capacity =
        static_cast<double>(slot(Water3DDiagnosticSlot::WhitewaterCapacity));
    const double p2g_active_faces =
        static_cast<double>(slot(Water3DDiagnosticSlot::P2GActiveFaces));
    const double p2g_faces_processed =
        static_cast<double>(slot(Water3DDiagnosticSlot::P2GFacesProcessed));
    const double p2g_blocked_faces =
        static_cast<double>(slot(Water3DDiagnosticSlot::P2GBlockedFaces));
    const double p2g_u_faces_processed =
        static_cast<double>(slot(Water3DDiagnosticSlot::P2GUFacesProcessed));
    const double p2g_v_faces_processed =
        static_cast<double>(slot(Water3DDiagnosticSlot::P2GVFacesProcessed));
    const double p2g_w_faces_processed =
        static_cast<double>(slot(Water3DDiagnosticSlot::P2GWFacesProcessed));
    const double p2g_neighbor_cells_tested =
        static_cast<double>(slot(Water3DDiagnosticSlot::P2GNeighborCellsTested));
    const double p2g_neighbor_cells_in_bounds =
        static_cast<double>(slot(Water3DDiagnosticSlot::P2GNeighborCellsInBounds));
    const double p2g_empty_cell_visits =
        static_cast<double>(slot(Water3DDiagnosticSlot::P2GEmptyCellVisits));
    const double p2g_cell_particle_slots_scanned = static_cast<double>(
        diagnostic_slot_u64(bytes, Water3DDiagnosticSlot::P2GCellParticleSlotsScanned,
                            Water3DDiagnosticSlot::P2GCellParticleSlotsScannedHigh));
    const double p2g_inactive_particles_seen =
        static_cast<double>(slot(Water3DDiagnosticSlot::P2GInactiveParticlesSeen));
    const double p2g_weight_positive_particles = static_cast<double>(
        diagnostic_slot_u64(bytes, Water3DDiagnosticSlot::P2GWeightPositiveParticles,
                            Water3DDiagnosticSlot::P2GWeightPositiveParticlesHigh));
    const double p2g_weight_zero_particles = static_cast<double>(
        diagnostic_slot_u64(bytes, Water3DDiagnosticSlot::P2GWeightZeroParticles,
                            Water3DDiagnosticSlot::P2GWeightZeroParticlesHigh));
    const double p2g_zero_weight_faces =
        static_cast<double>(slot(Water3DDiagnosticSlot::P2GZeroWeightFaces));
    const double p2g_max_cell_count_seen =
        static_cast<double>(slot(Water3DDiagnosticSlot::P2GMaxCellCountSeen));
    const double p2g_overpacked_cell_visits =
        static_cast<double>(slot(Water3DDiagnosticSlot::P2GOverpackedCellVisits));
    const double p2g_apic_particle_samples = static_cast<double>(
        diagnostic_slot_u64(bytes, Water3DDiagnosticSlot::P2GApicParticleSamples,
                            Water3DDiagnosticSlot::P2GApicParticleSamplesHigh));
    const double p2g_invalid_face_ids =
        static_cast<double>(slot(Water3DDiagnosticSlot::P2GInvalidFaceIds));
    const double p2g_candidate_slots_0 =
        static_cast<double>(slot(Water3DDiagnosticSlot::P2GCandidateSlotsBin0));
    const double p2g_candidate_slots_1_to_32 =
        static_cast<double>(slot(Water3DDiagnosticSlot::P2GCandidateSlotsBin1To32));
    const double p2g_candidate_slots_33_to_64 =
        static_cast<double>(slot(Water3DDiagnosticSlot::P2GCandidateSlotsBin33To64));
    const double p2g_candidate_slots_65_to_96 =
        static_cast<double>(slot(Water3DDiagnosticSlot::P2GCandidateSlotsBin65To96));
    const double p2g_candidate_slots_97_to_128 =
        static_cast<double>(slot(Water3DDiagnosticSlot::P2GCandidateSlotsBin97To128));
    const double p2g_candidate_slots_129_to_192 =
        static_cast<double>(slot(Water3DDiagnosticSlot::P2GCandidateSlotsBin129To192));
    const double p2g_candidate_slots_193_to_384 =
        static_cast<double>(slot(Water3DDiagnosticSlot::P2GCandidateSlotsBin193To384));
    const double p2g_candidate_slots_385_plus =
        static_cast<double>(slot(Water3DDiagnosticSlot::P2GCandidateSlotsBin385Plus));
    const double p2g_positive_candidates_0 =
        static_cast<double>(slot(Water3DDiagnosticSlot::P2GPositiveCandidatesBin0));
    const double p2g_positive_candidates_1_to_3 =
        static_cast<double>(slot(Water3DDiagnosticSlot::P2GPositiveCandidatesBin1To3));
    const double p2g_positive_candidates_4_to_7 =
        static_cast<double>(slot(Water3DDiagnosticSlot::P2GPositiveCandidatesBin4To7));
    const double p2g_positive_candidates_8_to_15 =
        static_cast<double>(slot(Water3DDiagnosticSlot::P2GPositiveCandidatesBin8To15));
    const double p2g_positive_candidates_16_to_31 =
        static_cast<double>(slot(Water3DDiagnosticSlot::P2GPositiveCandidatesBin16To31));
    const double p2g_positive_candidates_32_to_63 =
        static_cast<double>(slot(Water3DDiagnosticSlot::P2GPositiveCandidatesBin32To63));
    const double p2g_positive_candidates_64_to_127 =
        static_cast<double>(slot(Water3DDiagnosticSlot::P2GPositiveCandidatesBin64To127));
    const double p2g_positive_candidates_128_plus =
        static_cast<double>(slot(Water3DDiagnosticSlot::P2GPositiveCandidatesBin128Plus));
    const double p2g_overpacked_neighbors_0 =
        static_cast<double>(slot(Water3DDiagnosticSlot::P2GOverpackedNeighborCellsBin0));
    const double p2g_overpacked_neighbors_1 =
        static_cast<double>(slot(Water3DDiagnosticSlot::P2GOverpackedNeighborCellsBin1));
    const double p2g_overpacked_neighbors_2_to_3 =
        static_cast<double>(slot(Water3DDiagnosticSlot::P2GOverpackedNeighborCellsBin2To3));
    const double p2g_overpacked_neighbors_4_to_7 =
        static_cast<double>(slot(Water3DDiagnosticSlot::P2GOverpackedNeighborCellsBin4To7));
    const double p2g_overpacked_neighbors_8_plus =
        static_cast<double>(slot(Water3DDiagnosticSlot::P2GOverpackedNeighborCellsBin8Plus));
    const double p2g_max_candidate_slots_per_face =
        static_cast<double>(slot(Water3DDiagnosticSlot::P2GMaxCandidateSlotsPerFace));
    const double p2g_zero_weight_u_faces =
        static_cast<double>(slot(Water3DDiagnosticSlot::P2GZeroWeightUFaces));
    const double p2g_zero_weight_v_faces =
        static_cast<double>(slot(Water3DDiagnosticSlot::P2GZeroWeightVFaces));
    const double p2g_zero_weight_w_faces =
        static_cast<double>(slot(Water3DDiagnosticSlot::P2GZeroWeightWFaces));
    const double p2g_active_tiles =
        static_cast<double>(slot(Water3DDiagnosticSlot::P2GActiveTiles));
    const double p2g_tile_face_slots =
        static_cast<double>(slot(Water3DDiagnosticSlot::P2GTileFaceSlots));
    const double p2g_tile_inactive_face_slots =
        static_cast<double>(slot(Water3DDiagnosticSlot::P2GTileInactiveFaceSlots));
    const double p2g_tile_dispatch_groups =
        static_cast<double>(slot(Water3DDiagnosticSlot::P2GTileDispatchGroups));
    const double liquid_particles =
        static_cast<double>(slot(Water3DDiagnosticSlot::LiquidParticles));
    const double rain_particles = static_cast<double>(slot(Water3DDiagnosticSlot::RainParticles));
    const double transfer_truncated_particles =
        static_cast<double>(slot(Water3DDiagnosticSlot::TransferTruncatedParticles));

    record_metric(recorder, frame_index, "water_3d.workload", "active_particles", active_particles);
    record_metric(recorder, frame_index, "water_3d.workload", "liquid_particles", liquid_particles);
    record_metric(recorder, frame_index, "water_3d.workload", "rain_particles", rain_particles);
    record_metric(recorder, frame_index, "water_3d.workload", "inactive_scan_particles",
                  inactive_scan_particles);
    record_metric(recorder, frame_index, "water_3d.workload", "particle_scan_count",
                  particle_scan_count);
    record_metric(recorder, frame_index, "water_3d.workload", "out_of_bounds_particles",
                  out_of_bounds_particles);
    record_metric(recorder, frame_index, "water_3d.workload", "nonempty_cells", nonempty_cells);
    record_metric(recorder, frame_index, "water_3d.workload", "overpacked_cells", overpacked_cells);
    record_metric(recorder, frame_index, "water_3d.workload", "overpacked_particles",
                  overpacked_particles);
    record_metric(recorder, frame_index, "water_3d.workload", "transfer_truncated_particles",
                  transfer_truncated_particles);
    record_metric(recorder, frame_index, "water_3d.workload", "max_cell_count", max_cell_count);
    record_metric(recorder, frame_index, "water_3d.workload", "avg_particles_per_nonempty_cell",
                  nonempty_cells > 0.0 ? active_particles / nonempty_cells : 0.0);
    record_metric(recorder, frame_index, "water_3d.workload", "active_faces", active_faces);
    record_metric(recorder, frame_index, "water_3d.workload", "active_face_ratio",
                  active_faces / static_cast<double>(total_face_count(config)));
    record_metric(recorder, frame_index, "water_3d.workload", "active_face_dispatch_groups",
                  active_face_dispatch_groups);
    record_metric(recorder, frame_index, "water_3d.solver", "divergence_abs_sum",
                  divergence_abs_sum);
    record_metric(recorder, frame_index, "water_3d.solver", "divergence_abs_max",
                  divergence_abs_max);
    record_metric(recorder, frame_index, "water_3d.solver", "divergent_cells", divergent_cells);
    record_metric(recorder, frame_index, "water_3d.solver", "divergence_abs_avg",
                  divergent_cells > 0.0 ? divergence_abs_sum / divergent_cells : 0.0);
    record_metric(recorder, frame_index, "water_3d.whitewater", "emitted", whitewater_emitted);
    record_metric(recorder, frame_index, "water_3d.whitewater", "active", whitewater_active);
    record_metric(recorder, frame_index, "water_3d.whitewater", "capacity", whitewater_capacity);
    record_metric(recorder, frame_index, "water_3d.whitewater", "active_ratio",
                  whitewater_capacity > 0.0 ? whitewater_active / whitewater_capacity : 0.0);
    record_metric(recorder, frame_index, "water_3d.p2g", "active_faces", p2g_active_faces);
    record_metric(recorder, frame_index, "water_3d.p2g", "faces_processed", p2g_faces_processed);
    record_metric(recorder, frame_index, "water_3d.p2g", "blocked_faces", p2g_blocked_faces);
    record_metric(recorder, frame_index, "water_3d.p2g", "u_faces_processed",
                  p2g_u_faces_processed);
    record_metric(recorder, frame_index, "water_3d.p2g", "v_faces_processed",
                  p2g_v_faces_processed);
    record_metric(recorder, frame_index, "water_3d.p2g", "w_faces_processed",
                  p2g_w_faces_processed);
    record_metric(recorder, frame_index, "water_3d.p2g", "neighbor_cells_tested",
                  p2g_neighbor_cells_tested);
    record_metric(recorder, frame_index, "water_3d.p2g", "neighbor_cells_in_bounds",
                  p2g_neighbor_cells_in_bounds);
    record_metric(recorder, frame_index, "water_3d.p2g", "empty_cell_visits",
                  p2g_empty_cell_visits);
    record_metric(recorder, frame_index, "water_3d.p2g", "cell_particle_slots_scanned",
                  p2g_cell_particle_slots_scanned);
    record_metric(recorder, frame_index, "water_3d.p2g", "inactive_particles_seen",
                  p2g_inactive_particles_seen);
    record_metric(recorder, frame_index, "water_3d.p2g", "weight_positive_particles",
                  p2g_weight_positive_particles);
    record_metric(recorder, frame_index, "water_3d.p2g", "weight_zero_particles",
                  p2g_weight_zero_particles);
    record_metric(recorder, frame_index, "water_3d.p2g", "zero_weight_faces",
                  p2g_zero_weight_faces);
    record_metric(recorder, frame_index, "water_3d.p2g", "max_cell_count_seen",
                  p2g_max_cell_count_seen);
    record_metric(recorder, frame_index, "water_3d.p2g", "overpacked_cell_visits",
                  p2g_overpacked_cell_visits);
    record_metric(recorder, frame_index, "water_3d.p2g", "apic_particle_samples",
                  p2g_apic_particle_samples);
    record_metric(recorder, frame_index, "water_3d.p2g", "invalid_face_ids", p2g_invalid_face_ids);
    record_metric(recorder, frame_index, "water_3d.p2g", "max_candidate_slots_per_face",
                  p2g_max_candidate_slots_per_face);
    record_metric(recorder, frame_index, "water_3d.p2g", "zero_weight_u_faces",
                  p2g_zero_weight_u_faces);
    record_metric(recorder, frame_index, "water_3d.p2g", "zero_weight_v_faces",
                  p2g_zero_weight_v_faces);
    record_metric(recorder, frame_index, "water_3d.p2g", "zero_weight_w_faces",
                  p2g_zero_weight_w_faces);
    record_metric(recorder, frame_index, "water_3d.p2g", "active_tiles", p2g_active_tiles);
    record_metric(recorder, frame_index, "water_3d.p2g", "tile_face_slots", p2g_tile_face_slots);
    record_metric(recorder, frame_index, "water_3d.p2g", "tile_inactive_face_slots",
                  p2g_tile_inactive_face_slots);
    record_metric(recorder, frame_index, "water_3d.p2g", "tile_dispatch_groups",
                  p2g_tile_dispatch_groups);
    record_metric(recorder, frame_index, "water_3d.p2g", "tile_slot_to_active_face_ratio",
                  p2g_active_faces > 0.0 ? p2g_tile_face_slots / p2g_active_faces : 0.0);
    record_metric(recorder, frame_index, "water_3d.p2g", "avg_slots_per_face",
                  p2g_faces_processed > 0.0 ? p2g_cell_particle_slots_scanned / p2g_faces_processed
                                            : 0.0);
    record_metric(recorder, frame_index, "water_3d.p2g", "positive_weight_ratio",
                  (p2g_weight_positive_particles + p2g_weight_zero_particles) > 0.0
                      ? p2g_weight_positive_particles /
                            (p2g_weight_positive_particles + p2g_weight_zero_particles)
                      : 0.0);
    record_metric(recorder, frame_index, "water_3d.p2g", "blocked_face_ratio",
                  p2g_active_faces > 0.0 ? p2g_blocked_faces / p2g_active_faces : 0.0);
    record_metric(recorder, frame_index, "water_3d.p2g", "in_bounds_neighbor_ratio",
                  p2g_neighbor_cells_tested > 0.0
                      ? p2g_neighbor_cells_in_bounds / p2g_neighbor_cells_tested
                      : 0.0);
    record_metric(recorder, frame_index, "water_3d.p2g", "high_candidate_face_ratio",
                  p2g_faces_processed > 0.0
                      ? (p2g_candidate_slots_129_to_192 + p2g_candidate_slots_193_to_384 +
                         p2g_candidate_slots_385_plus) /
                            p2g_faces_processed
                      : 0.0);
    record_metric(recorder, frame_index, "water_3d.p2g", "overpacked_neighbor_face_ratio",
                  p2g_faces_processed > 0.0
                      ? (p2g_overpacked_neighbors_1 + p2g_overpacked_neighbors_2_to_3 +
                         p2g_overpacked_neighbors_4_to_7 + p2g_overpacked_neighbors_8_plus) /
                            p2g_faces_processed
                      : 0.0);
    record_metric(recorder, frame_index, "water_3d.p2g.histogram", "candidate_slots_0",
                  p2g_candidate_slots_0);
    record_metric(recorder, frame_index, "water_3d.p2g.histogram", "candidate_slots_1_32",
                  p2g_candidate_slots_1_to_32);
    record_metric(recorder, frame_index, "water_3d.p2g.histogram", "candidate_slots_33_64",
                  p2g_candidate_slots_33_to_64);
    record_metric(recorder, frame_index, "water_3d.p2g.histogram", "candidate_slots_65_96",
                  p2g_candidate_slots_65_to_96);
    record_metric(recorder, frame_index, "water_3d.p2g.histogram", "candidate_slots_97_128",
                  p2g_candidate_slots_97_to_128);
    record_metric(recorder, frame_index, "water_3d.p2g.histogram", "candidate_slots_129_192",
                  p2g_candidate_slots_129_to_192);
    record_metric(recorder, frame_index, "water_3d.p2g.histogram", "candidate_slots_193_384",
                  p2g_candidate_slots_193_to_384);
    record_metric(recorder, frame_index, "water_3d.p2g.histogram", "candidate_slots_385_plus",
                  p2g_candidate_slots_385_plus);
    record_metric(recorder, frame_index, "water_3d.p2g.histogram", "positive_candidates_0",
                  p2g_positive_candidates_0);
    record_metric(recorder, frame_index, "water_3d.p2g.histogram", "positive_candidates_1_3",
                  p2g_positive_candidates_1_to_3);
    record_metric(recorder, frame_index, "water_3d.p2g.histogram", "positive_candidates_4_7",
                  p2g_positive_candidates_4_to_7);
    record_metric(recorder, frame_index, "water_3d.p2g.histogram", "positive_candidates_8_15",
                  p2g_positive_candidates_8_to_15);
    record_metric(recorder, frame_index, "water_3d.p2g.histogram", "positive_candidates_16_31",
                  p2g_positive_candidates_16_to_31);
    record_metric(recorder, frame_index, "water_3d.p2g.histogram", "positive_candidates_32_63",
                  p2g_positive_candidates_32_to_63);
    record_metric(recorder, frame_index, "water_3d.p2g.histogram", "positive_candidates_64_127",
                  p2g_positive_candidates_64_to_127);
    record_metric(recorder, frame_index, "water_3d.p2g.histogram", "positive_candidates_128_plus",
                  p2g_positive_candidates_128_plus);
    record_metric(recorder, frame_index, "water_3d.p2g.histogram", "overpacked_neighbors_0",
                  p2g_overpacked_neighbors_0);
    record_metric(recorder, frame_index, "water_3d.p2g.histogram", "overpacked_neighbors_1",
                  p2g_overpacked_neighbors_1);
    record_metric(recorder, frame_index, "water_3d.p2g.histogram", "overpacked_neighbors_2_3",
                  p2g_overpacked_neighbors_2_to_3);
    record_metric(recorder, frame_index, "water_3d.p2g.histogram", "overpacked_neighbors_4_7",
                  p2g_overpacked_neighbors_4_to_7);
    record_metric(recorder, frame_index, "water_3d.p2g.histogram", "overpacked_neighbors_8_plus",
                  p2g_overpacked_neighbors_8_plus);
}

[[nodiscard]] std::filesystem::path bundled_sample_environment_path() {
#ifdef CUBEY_HDR_SAMPLE_ASSETS_DIR
    return std::filesystem::path(CUBEY_HDR_SAMPLE_ASSETS_DIR) / "lightroom_14b.hdr";
#else
    return {};
#endif
}

class Water3DApp {
  public:
    Water3DApp(RunConfig config, Water3DAppInfo app_info)
        : config_(std::move(config)), app_info_(app_info), runtime_(1),
          water_config_(water_3d_config_from_run_config(config_)),
          render_view_(water_3d_render_view_from_name(config_.debug_view)) {
        orbit_controller_.set_home_distance(kCameraDistance);
        orbit_controller_.set_auto_rotation_speed(0.0F);
    }

    Water3DApp(const Water3DApp&) = delete;
    Water3DApp& operator=(const Water3DApp&) = delete;

    int run() {
        if (config_.headless) {
            return run_headless();
        }

        cubey::host::WindowedAppCallbacks callbacks;
        callbacks.create_global_resources = [this](cubey::host::WindowedAppContext& context) {
            create_global_resources_if_needed(context.device(), context.gpu(),
                                              context.frame_slot_count());
        };
        callbacks.create_swapchain_resources = [this](cubey::host::WindowedAppContext& context) {
            create_render_pipeline(context.device(), context.swapchain().format(),
                                   context.swapchain().extent());
        };
        callbacks.destroy_swapchain_resources = [this](cubey::host::WindowedAppContext& context) {
            (void)context;
            destroy_swapchain_resources();
        };
        callbacks.update = [this](cubey::host::WindowedAppContext& context,
                                  const FrameTiming& timing) {
            const ProjectFrame& project_frame = runtime_.frame_for_timing(timing);
            update_interaction(context, project_frame);
        };
        callbacks.draw_ui = [this](cubey::host::WindowedAppContext& context) { draw_ui(context); };
        callbacks.record_frame = [this](cubey::host::WindowedAppContext& context,
                                        const cubey::host::WindowedRenderFrame& frame) {
            const ProjectFrame& project_frame = runtime_.frame_for_timing(frame.timing);
            record_frame(context, frame, project_frame);
        };
        callbacks.frame_stats_sample =
            [this](cubey::host::WindowedAppContext& context,
                   const FrameTiming& timing) -> std::optional<FrameStatsSample> {
            return record_frame_stats(context, timing);
        };
        callbacks.shutdown = [this](cubey::host::WindowedAppContext& context) {
            (void)context;
            destroy_all_resources();
            retire_project_gpu_work();
            detach_project_gpu();
        };

        return cubey::host::run_windowed_app(
            {
                .run_config = config_,
                .app_name = app_info_.app_name,
                .ready_status = app_info_.ready_status,
                .required_queue_flags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT,
                .swapchain_image_usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
                .require_dynamic_rendering = true,
                .close_on_escape = true,
            },
            std::move(callbacks));
    }

  private:
    void update_interaction(cubey::host::WindowedAppContext& context,
                            const ProjectFrame& project_frame) {
        const auto input = context.filtered_input();
        if (input.key_pressed(cubey::input::Key::Space)) {
            paused_ = !paused_;
        }
        if (input.key_pressed(cubey::input::Key::R)) {
            reset_simulation();
        }
        if (input.key_pressed(cubey::input::Key::D)) {
            render_view_ = next_render_view(render_view_);
        }

        update_camera_input(context, project_frame.delta_seconds);
    }

    void update_camera_input(cubey::host::WindowedAppContext& context, double delta_seconds) {
        const auto input = context.filtered_input();
        if (input.mouse_enabled()) {
            orbit_controller_.zoom_by_scroll(input.scroll_delta().y);
            if (input.has_cursor()) {
                const cubey::input::CursorPosition cursor = input.cursor();
                if (input.mouse_button_pressed(cubey::input::MouseButton::Left)) {
                    orbit_controller_.begin_drag(cursor.x, cursor.y);
                }
                if (input.mouse_button_down(cubey::input::MouseButton::Left)) {
                    orbit_controller_.drag_to(cursor.x, cursor.y);
                }
            }
        }
        if (!input.mouse_enabled() ||
            input.mouse_button_released(cubey::input::MouseButton::Left) ||
            !input.mouse_button_down(cubey::input::MouseButton::Left)) {
            orbit_controller_.end_drag();
        }
        orbit_controller_.update(delta_seconds);
    }

    std::optional<FrameStatsSample> record_frame_stats(cubey::host::WindowedAppContext& context,
                                                       const FrameTiming& timing) {
        const VkExtent2D extent = context.swapchain().extent();
        latest_frame_ms_ = timing.delta_seconds * 1000.0;
        latest_fps_ = timing.delta_seconds > 0.0 ? 1.0 / timing.delta_seconds : 0.0;

        const std::uint32_t particle_scan_count =
            water_3d_runtime_particle_scan_count(water_config_, runtime_state_);
        const FrameStatsSample sample{
            .delta_seconds = timing.delta_seconds,
            .width = extent.width,
            .height = extent.height,
            .triangles =
                render_view_ == Water3DRenderView::Particles
                    ? particle_scan_count * 2U
                    : (is_water_3d_surface_view(render_view_) ? particle_scan_count * 4U + 8U : 2U),
        };
        if (std::optional<FrameStatsSnapshot> stats = ui_frame_stats_.record_frame(sample);
            stats.has_value()) {
            latest_frame_stats_ = stats.value();
        }
        return sample;
    }

    void draw_ui(cubey::host::WindowedAppContext& context) {
        ImGui::SetNextWindowPos(ImVec2(16.0F, 16.0F), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(430.0F, 0.0F), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin(app_info_.ui_title)) {
            ImGui::End();
            return;
        }

        ImGui::Checkbox("Paused", &paused_);
        ImGui::SameLine();
        if (ImGui::Button("Reset")) {
            reset_simulation();
        }

        if (ImGui::BeginCombo("Render view", water_3d_render_view_name(render_view_))) {
            for (Water3DRenderView view : kRenderViews) {
                const bool selected = view == render_view_;
                if (ImGui::Selectable(water_3d_render_view_name(view), selected)) {
                    render_view_ = view;
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }

        const auto section = [](const char* label, bool default_open) {
            const ImGuiTreeNodeFlags flags =
                default_open ? ImGuiTreeNodeFlags_DefaultOpen : ImGuiTreeNodeFlags_None;
            return ImGui::CollapsingHeader(label, flags);
        };

        ImGui::Spacing();
        if (section("Simulation", true)) {
            if (ImGui::BeginCombo("Transfer",
                                  water_3d_transfer_mode_name(water_config_.transfer_mode))) {
                for (Water3DTransferMode mode : kTransferModes) {
                    const bool selected = mode == water_config_.transfer_mode;
                    if (ImGui::Selectable(water_3d_transfer_mode_name(mode), selected)) {
                        water_config_.transfer_mode = mode;
                    }
                    if (selected) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }

            int pressure_iterations = static_cast<int>(water_config_.pressure_iterations);
            if (ImGui::SliderInt("Pressure iterations", &pressure_iterations, 1, 128)) {
                water_config_.pressure_iterations = static_cast<std::uint32_t>(pressure_iterations);
            }
            int substeps = static_cast<int>(water_config_.substeps);
            if (ImGui::SliderInt("Substeps", &substeps, 1, 4)) {
                water_config_.substeps = static_cast<std::uint32_t>(substeps);
            }
            ImGui::SliderFloat("PIC/FLIP blend", &water_config_.flip_ratio, 0.0F, 1.0F, "%.2f");
            ImGui::SliderFloat("Velocity limit", &water_config_.velocity_limit, 1.0F, 8.0F, "%.2f");
            ImGui::SliderFloat("Particle damping", &water_config_.particle_damping, 0.980F, 1.000F,
                               "%.3f");
            ImGui::SliderFloat("Particle volume strength", &water_config_.particle_volume_strength,
                               0.0F, 48.0F, "%.1f");
            int transfer_limit = static_cast<int>(water_config_.max_particles_per_cell);
            if (ImGui::SliderInt("Transfer limit/cell", &transfer_limit, 8, 256)) {
                water_config_.max_particles_per_cell = static_cast<std::uint32_t>(transfer_limit);
            }
            ImGui::SliderFloat("Gravity", &water_config_.gravity, -4.0F, 0.0F, "%.2f");
            ImGui::SliderFloat("Boundary bounce", &water_config_.boundary_restitution, 0.0F, 0.8F,
                               "%.2f");
        }

        if (section("Initial volume", false)) {
            if (ImGui::SliderFloat("Fill width", &water_config_.initial_fill_width,
                                   kWater3DMinFillFraction, kWater3DMaxFillFraction, "%.2f")) {
                refresh_particle_counts(water_config_);
                reset_simulation();
            }
            if (ImGui::SliderFloat("Fill height", &water_config_.initial_fill_height,
                                   kWater3DMinFillFraction, kWater3DMaxFillFraction, "%.2f")) {
                refresh_particle_counts(water_config_);
                reset_simulation();
            }
            if (ImGui::SliderFloat("Fill depth", &water_config_.initial_fill_depth,
                                   kWater3DMinFillFraction, kWater3DMaxFillFraction, "%.2f")) {
                refresh_particle_counts(water_config_);
                reset_simulation();
            }
            bool fill_center_changed = false;
            fill_center_changed |= ImGui::SliderFloat(
                "Fill center X", &water_config_.initial_fill_center[0], 0.05F, 0.95F, "%.2f");
            fill_center_changed |= ImGui::SliderFloat(
                "Fill center Z", &water_config_.initial_fill_center[1], 0.05F, 0.95F, "%.2f");
            if (fill_center_changed) {
                reset_simulation();
            }
            ImGui::SliderFloat3("Domain scale", water_config_.domain.scale.data(), 0.25F, 3.0F,
                                "%.2f");
        }

        if (section("Sources and forces", true)) {
            ImGui::SeparatorText("Hose");
            ImGui::PushID("hose");
            ImGui::Checkbox("Enabled", &water_config_.hose.enabled);
            ImGui::SliderFloat3("Position", water_config_.hose.position.data(), 0.02F, 0.98F,
                                "%.2f");
            ImGui::SliderFloat("Yaw", &water_config_.hose.yaw_degrees, -180.0F, 180.0F, "%.1f deg");
            ImGui::SliderFloat("Pitch", &water_config_.hose.pitch_degrees, -70.0F, 20.0F,
                               "%.1f deg");
            ImGui::SliderFloat("Speed", &water_config_.hose.speed, 0.2F, 6.0F, "%.2f");
            ImGui::SliderFloat("Radius", &water_config_.hose.radius, 0.004F, 0.080F, "%.3f");
            ImGui::SliderFloat("Rate", &water_config_.hose.particles_per_second, 0.0F, 60000.0F,
                               "%.0f/s");
            ImGui::SliderFloat("Spread", &water_config_.hose.spread_degrees, 0.0F, 45.0F,
                               "%.1f deg");
            ImGui::PopID();

            ImGui::SeparatorText("Drain");
            ImGui::PushID("drain");
            ImGui::Checkbox("Enabled", &water_config_.drain.enabled);
            ImGui::SliderFloat3("Center", water_config_.drain.center.data(), 0.02F, 0.98F, "%.2f");
            ImGui::SliderFloat3("Half size", water_config_.drain.half_size.data(), 0.005F, 0.35F,
                                "%.3f");
            ImGui::SliderFloat("Pull speed", &water_config_.drain.pull_speed, 0.0F, 6.0F, "%.2f");
            ImGui::SliderFloat("Pull radius", &water_config_.drain.pull_radius, 0.05F, 1.5F,
                               "%.2f");
            ImGui::PopID();

            ImGui::SeparatorText("Wave");
            ImGui::PushID("wave");
            ImGui::Checkbox("Enabled", &water_config_.wave.enabled);
            ImGui::SliderFloat3("Center", water_config_.wave.center.data(), 0.02F, 0.98F, "%.2f");
            ImGui::SliderFloat3("Half size", water_config_.wave.half_size.data(), 0.01F, 0.55F,
                                "%.2f");
            ImGui::SliderFloat("Amplitude", &water_config_.wave.amplitude, 0.0F, 4.0F, "%.2f");
            ImGui::SliderFloat("Frequency", &water_config_.wave.frequency_hz, 0.0F, 2.0F,
                               "%.2f Hz");
            ImGui::PopID();

            ImGui::SeparatorText("Rain");
            ImGui::PushID("rain");
            ImGui::Checkbox("Enabled", &water_config_.rain.enabled);
            ImGui::SliderFloat3("Center", water_config_.rain.center.data(), 0.02F, 0.98F, "%.2f");
            ImGui::SliderFloat3("Half size", water_config_.rain.half_size.data(), 0.005F, 0.50F,
                                "%.2f");
            ImGui::SliderFloat("Speed", &water_config_.rain.speed, 0.2F, 6.0F, "%.2f");
            ImGui::SliderFloat("Radius", &water_config_.rain.radius, 0.002F, 0.060F, "%.3f");
            ImGui::SliderFloat("Rate", &water_config_.rain.particles_per_second, 0.0F, 30000.0F,
                               "%.0f/s");
            ImGui::SliderFloat("Spread", &water_config_.rain.spread_degrees, 0.0F, 30.0F,
                               "%.1f deg");
            ImGui::PopID();
        }

        if (section("Surface and lighting", false)) {
            ImGui::SliderFloat("Particle radius", &water_config_.particle_radius, 0.004F, 0.040F,
                               "%.4f");
            ImGui::SliderFloat("Surface thickness", &water_config_.surface_thickness_scale, 0.1F,
                               4.0F, "%.2f");
            ImGui::SliderFloat("Surface fill px", &water_config_.surface_gap_fill_radius_px, 0.0F,
                               3.0F, "%.1f");
            ImGui::SliderFloat("Surface smooth world",
                               &water_config_.surface_smoothing_radius_world, 0.0F, 0.04F, "%.3f");
            int surface_smoothing_iterations =
                static_cast<int>(water_config_.surface_smoothing_iterations);
            if (ImGui::SliderInt("Surface smooth passes", &surface_smoothing_iterations, 0, 8)) {
                water_config_.surface_smoothing_iterations =
                    static_cast<std::uint32_t>(surface_smoothing_iterations);
            }
            ImGui::SliderFloat("Surface depth sigma", &water_config_.surface_depth_sigma, 0.005F,
                               0.120F, "%.3f");
            ImGui::SliderFloat("Thickness smoothing", &water_config_.surface_thickness_smoothing,
                               0.0F, 1.0F, "%.2f");
            ImGui::SliderFloat("Surface absorption", &water_config_.surface_absorption, 0.0F, 5.0F,
                               "%.2f");
            ImGui::SliderFloat("Surface refraction", &water_config_.surface_refraction_strength,
                               0.0F, 0.12F, "%.3f");
            ImGui::SliderFloat("Environment intensity", &water_config_.environment_intensity, 0.0F,
                               4.0F, "%.2f");
            ImGui::SliderFloat("Environment rotation", &water_config_.environment_rotation_degrees,
                               -180.0F, 180.0F, "%.0f deg");
            ImGui::SliderFloat("Exposure", &water_config_.exposure, -4.0F, 4.0F, "%.2f");
            ImGui::SliderFloat("Slice depth", &water_config_.slice_depth, 0.02F, 0.98F, "%.2f");
        }

        if (section("Foam and whitewater", false)) {
            ImGui::SliderFloat("Foam amount", &water_config_.foam_amount, 0.0F, 1.0F, "%.2f");
            ImGui::SliderFloat("Foam sharpness", &water_config_.foam_sharpness, 0.2F, 4.0F, "%.2f");
            ImGui::Checkbox("Whitewater", &water_config_.whitewater_enabled);
            ImGui::SliderFloat("Whitewater intensity", &water_config_.whitewater_intensity, 0.0F,
                               3.0F, "%.2f");
            int whitewater_max_emit = static_cast<int>(water_config_.whitewater_max_emit_per_frame);
            if (ImGui::SliderInt("Whitewater emit/frame", &whitewater_max_emit, 0, 8192)) {
                water_config_.whitewater_max_emit_per_frame =
                    static_cast<std::uint32_t>(whitewater_max_emit);
            }
            ImGui::SliderFloat("Whitewater speed", &water_config_.whitewater_speed_threshold, 0.05F,
                               3.5F, "%.2f");
            ImGui::SliderFloat("Whitewater radius", &water_config_.whitewater_radius, 0.002F,
                               0.035F, "%.3f");
            ImGui::SliderFloat("Whitewater lifetime", &water_config_.whitewater_lifetime, 0.15F,
                               5.0F, "%.2f");
            ImGui::SliderFloat("Whitewater drag", &water_config_.whitewater_drag, 0.50F, 1.0F,
                               "%.2f");
            ImGui::SliderFloat("Whitewater gravity", &water_config_.whitewater_gravity_scale, 0.0F,
                               1.5F, "%.2f");
        }

        if (section("Diagnostics", true)) {
            ImGui::Text("Grid: %u x %u x %u", water_config_.grid_width, water_config_.grid_height,
                        water_config_.grid_depth);
            const std::uint32_t scanned_particles =
                water_3d_runtime_particle_scan_count(water_config_, runtime_state_);
            ImGui::Text("Particles: %u reset / %u capacity", water_config_.active_particle_count,
                        water_config_.particle_capacity);
            ImGui::Text("Compute particles: %u scanned", scanned_particles);
            ImGui::Text("Emitter pool: %u touched / %u available",
                        scanned_particles - water_config_.active_particle_count,
                        emitter_particle_pool_capacity_for_config(water_config_));
            ImGui::Text("Whitewater: %u capacity / %u max emit", water_config_.whitewater_capacity,
                        water_config_.whitewater_max_emit_per_frame);
            if (latest_frame_stats_.has_value()) {
                ImGui::Text("Frame: %.1f fps / %.2f ms avg (%.2f ms last)",
                            latest_frame_stats_->fps, latest_frame_stats_->frame_ms,
                            latest_frame_ms_);
            } else if (latest_fps_ > 0.0) {
                ImGui::Text("Frame: %.1f fps / %.2f ms", latest_fps_, latest_frame_ms_);
            } else {
                ImGui::TextUnformatted("Frame: collecting...");
            }

            const std::vector<cubey::vulkan::GpuPassTiming>& timings = resources_.latest_timings();
            if (!timings.empty()) {
                ImGui::SeparatorText("GPU timings");
                for (const cubey::vulkan::GpuPassTiming& timing : timings) {
                    ImGui::Text("%s: %.3f ms", timing.label.c_str(), timing.milliseconds);
                }
            }

            const VkDeviceSize water_bytes = resources_.allocated_buffer_bytes();
            const cubey::vulkan::DeviceMemoryBudgetInfo memory_budget =
                context.device().device_memory_budget();
            ImGui::Text("Water GPU buffers: %.1f MiB", bytes_to_mib(water_bytes));
            if (memory_budget.available && memory_budget.device_local_budget > 0) {
                ImGui::Text("VRAM: %.0f / %.0f MiB used",
                            bytes_to_mib(memory_budget.device_local_usage),
                            bytes_to_mib(memory_budget.device_local_budget));
            } else {
                ImGui::Text("VRAM heap: %.0f MiB (usage unavailable)",
                            bytes_to_mib(memory_budget.device_local_heap_size));
            }
        }
        ImGui::End();
    }

    void reset_simulation() {
        reset_requested_ = true;
        runtime_state_ = {};
    }

    [[nodiscard]] Water3DRenderCamera render_camera(VkExtent2D extent) const {
        const float aspect = static_cast<float>(extent.width) / static_cast<float>(extent.height);
        const cubey::Transform3D transform = cubey::orbit_camera_transform(cubey::OrbitCameraState{
            .target = kVolumeCenter,
            .distance = orbit_controller_.distance(),
            .yaw = kCameraBaseYaw + orbit_controller_.yaw(),
            .pitch = kCameraBasePitch + orbit_controller_.pitch(),
        });
        const cubey::math::Quat rotation = transform.rotation;
        return {
            .view_projection = camera_.view_projection_matrix(transform, aspect),
            .position = transform.translation,
            .right = rotation * cubey::math::Vec3{1.0F, 0.0F, 0.0F},
            .up = rotation * cubey::math::Vec3{0.0F, 1.0F, 0.0F},
            .forward = rotation * cubey::math::Vec3{0.0F, 0.0F, -1.0F},
            .fovy_radians = camera_.fovy_radians(),
        };
    }

    void destroy_swapchain_resources() {
        resources_.destroy_swapchain_resources();
    }

    void destroy_all_resources() {
        surface_graph_executor_.clear();
        resources_.destroy_all_resources();
        ibl_environment_.reset();
    }

    [[nodiscard]] std::filesystem::path resolved_environment_path() const {
        if (!config_.pbr.environment_path.empty()) {
            if (!std::filesystem::exists(config_.pbr.environment_path)) {
                throw std::runtime_error("environment HDR does not exist: " +
                                         config_.pbr.environment_path.string());
            }
            return config_.pbr.environment_path;
        }

        const std::filesystem::path sample = bundled_sample_environment_path();
        if (!sample.empty() && std::filesystem::exists(sample)) {
            return sample;
        }
        return {};
    }

    void create_environment_resources_if_needed(cubey::vulkan::Device& device,
                                                cubey::vulkan::GpuRuntime& gpu) {
        if (ibl_environment_.has_value()) {
            return;
        }

        cubey::render::GeneratedPbrEnvironmentConfig ibl_config;
        ibl_config.intensity = 1.0F;
        const std::filesystem::path environment = resolved_environment_path();
        if (!environment.empty()) {
            const cubey::asset::HdrImage image = cubey::asset::load_hdr_image(environment);
            ibl_environment_.emplace(cubey::render::create_pbr_environment_from_equirectangular(
                device, gpu,
                cubey::render::PbrEquirectangularImage{
                    .width = image.width,
                    .height = image.height,
                    .rgba32f = image.rgba32f,
                },
                ibl_config));
            return;
        }

        ibl_environment_.emplace(
            cubey::render::create_generated_pbr_environment(device, gpu, ibl_config));
    }

    [[nodiscard]] const cubey::render::GeneratedPbrEnvironment& ibl_environment() const {
        if (!ibl_environment_.has_value()) {
            throw std::runtime_error("water 3D IBL environment is not initialized");
        }
        return ibl_environment_.value();
    }

    void create_global_resources_if_needed(cubey::vulkan::Device& device,
                                           cubey::vulkan::GpuRuntime& gpu,
                                           std::uint32_t frame_slot_count) {
        create_environment_resources_if_needed(device, gpu);
        attach_project_gpu(gpu);
        resources_.create_global_resources_if_needed(device, runtime_.gpu(), water_config_,
                                                     frame_slot_count);
        surface_graph_executor_.resize(frame_slot_count);
    }

    void attach_project_gpu(cubey::vulkan::GpuRuntime& gpu) {
        runtime_.attach_gpu_if_needed(gpu);
    }

    void detach_project_gpu() {
        runtime_.detach_gpu_if_attached();
    }

    void retire_project_gpu_work() {
        static_cast<void>(runtime_.retire_completed_gpu_work());
    }

    void create_render_pipeline(cubey::vulkan::Device& device, VkFormat color_format,
                                VkExtent2D extent) {
        resources_.create_render_pipeline(device, color_format, extent, ibl_environment());
    }

    void record_frame(cubey::host::WindowedAppContext& context,
                      const cubey::host::WindowedRenderFrame& render_frame,
                      const ProjectFrame& frame) {
        cubey::vulkan::GpuTimestampProfiler* profiler = resources_.profiler();
        if (profiler != nullptr) {
            profiler->collect(render_frame.frame_slot.index);
            record_gpu_timings(context.profile_recorder(),
                               collected_profile_frame_index(frame, render_frame.frame_slot),
                               resources_.latest_timings());
            maybe_print_gpu_timings(frame);
        }
        const cubey::vulkan::CommandRecorder recorder(render_frame.command_buffer);
        recorder.begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
        if (profiler != nullptr) {
            profiler->begin_frame(render_frame.command_buffer, render_frame.frame_slot.index);
        }
        record_water_3d_compute(render_frame.command_buffer, resources_, water_config_,
                                runtime_state_, render_frame.frame_slot, paused_, reset_requested_,
                                frame, true, profiler);
        if (is_water_3d_surface_view(render_view_)) {
            record_water_3d_surface_draw(
                render_frame.command_buffer, context.device(), surface_graph_executor_, resources_,
                water_config_, render_frame.frame_slot, runtime_state_, render_view_,
                render_camera(render_frame.color_target.extent), render_frame.color_target,
                Water3DRenderTargetMode::Present, ibl_environment(), profiler);
        } else {
            cubey::render::record_present_render_target(
                recorder, cubey::render::render_target_view(render_frame.color_target),
                [this, &render_frame,
                 profiler](const cubey::vulkan::CommandRecorder& present_recorder) {
                    record_water_3d_draw(present_recorder.handle(), resources_, water_config_,
                                         render_frame.frame_slot, runtime_state_, render_view_,
                                         render_camera(render_frame.color_target.extent),
                                         render_frame.color_target, profiler);
                });
        }
        recorder.end("vkEndCommandBuffer water_3d");
    }

    void maybe_print_gpu_timings(const ProjectFrame& frame) {
        if (!config_.print_frame_stats) {
            return;
        }
        const std::vector<cubey::vulkan::GpuPassTiming>& timings = resources_.latest_timings();
        if (timings.empty()) {
            return;
        }
        if (last_gpu_timing_print_seconds_ >= 0.0 &&
            frame.elapsed_seconds - last_gpu_timing_print_seconds_ < 1.0) {
            return;
        }
        last_gpu_timing_print_seconds_ = frame.elapsed_seconds;
        std::printf("water_3d_gpu:");
        for (const cubey::vulkan::GpuPassTiming& timing : timings) {
            std::printf(" %s=%.3fms", timing.label.c_str(), timing.milliseconds);
        }
        std::printf("\n");
    }

    void record_headless_simulation_frame(cubey::ProjectGpuServices& gpu,
                                          cubey::render::FrameSlot frame_slot,
                                          const ProjectFrame& frame,
                                          cubey::profiling::ProfileRecorder* profile_recorder) {
        const std::uint64_t frame_index = profile_frame_index(frame);
        static_cast<void>(gpu.submit_and_wait({
            .label = "water_3d headless simulation frame",
            .work =
                [this, frame_slot, frame, profile_recorder,
                 frame_index](cubey::vulkan::GpuOwnerContext& gpu_context) {
                    cubey::vulkan::ImmediateCommands commands(gpu_context);
                    cubey::vulkan::GpuTimestampProfiler* profiler = resources_.profiler();
                    if (profiler != nullptr) {
                        profiler->begin_frame(commands.command_buffer(), frame_slot.index);
                    }
                    record_water_3d_compute(commands.command_buffer(), resources_, water_config_,
                                            runtime_state_, frame_slot, paused_, reset_requested_,
                                            frame, true, profiler);
                    commands.submit_and_wait();
                    if (profiler != nullptr) {
                        profiler->collect(frame_slot.index);
                        record_gpu_timings(profile_recorder, frame_index,
                                           resources_.latest_timings());
                    }
                },
        }));
        if (should_record_water_3d_diagnostics(profile_recorder, water_config_, frame_index)) {
            const std::vector<std::uint8_t> diagnostics = gpu.readback_buffer(
                resources_.diagnostics().handle(), resources_.diagnostics().size(),
                "water_3d diagnostics readback");
            record_water_3d_diagnostics(*profile_recorder, frame_index, water_config_, diagnostics);
        }
    }

    int run_headless() {
        cubey::host::HeadlessPngHostConfig host_config;
        host_config.run_config = config_;
        host_config.required_queue_flags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT;

        cubey::host::HeadlessPngHostCallbacks callbacks;
        callbacks.create_resources = [this](cubey::host::HeadlessPngContext& context) {
            const cubey::host::HeadlessRenderTarget& target = context.render_target();
            create_global_resources_if_needed(
                context.device(), context.gpu(),
                cubey::host::headless_capture_frame_slot_count(config_));
            create_render_pipeline(context.device(), target.format, target.extent);
        };
        cubey::host::install_headless_simulation_driver(
            callbacks, config_,
            {
                .png_frame_count = water_3d_headless_frame_count(config_),
                .png_timing =
                    [this](std::uint64_t simulation_frame) {
                        return fixed_water_3d_headless_timing(water_config_, simulation_frame);
                    },
                .simulate_frame =
                    [this](cubey::host::HeadlessPngContext& context,
                           const cubey::host::HeadlessCaptureFrame& frame) {
                        const ProjectFrame project_frame = runtime_.frame_for_timing(frame.timing);
                        record_headless_simulation_frame(runtime_.gpu(), frame.frame_slot,
                                                         project_frame, context.profile_recorder());
                    },
            });
        callbacks.record_frame = [this](cubey::host::HeadlessPngContext& context,
                                        const cubey::host::HeadlessCaptureFrame& frame,
                                        VkCommandBuffer command_buffer,
                                        const cubey::host::HeadlessRenderTarget& target) {
            if (is_water_3d_surface_view(render_view_)) {
                record_water_3d_surface_draw(
                    command_buffer, context.device(), surface_graph_executor_, resources_,
                    water_config_, frame.frame_slot, runtime_state_, render_view_,
                    render_camera(target.extent), target, Water3DRenderTargetMode::ColorAttachment,
                    ibl_environment());
            } else {
                record_water_3d_draw(command_buffer, resources_, water_config_, frame.frame_slot,
                                     runtime_state_, render_view_, render_camera(target.extent),
                                     target);
            }
        };
        callbacks.shutdown = [this](cubey::host::HeadlessPngContext&) {
            destroy_all_resources();
            retire_project_gpu_work();
            detach_project_gpu();
        };

        cubey::host::HeadlessPngHost host(std::move(host_config), std::move(callbacks));
        return host.run();
    }

    RunConfig config_;
    Water3DAppInfo app_info_;
    cubey::ProjectRuntimeAdapter runtime_;
    Water3DConfig water_config_;
    Water3DRuntimeState runtime_state_;
    Water3DGpuResources resources_;
    cubey::render::RenderGraphFrameExecutor surface_graph_executor_;
    std::optional<cubey::render::GeneratedPbrEnvironment> ibl_environment_;
    cubey::Camera3D camera_;
    cubey::OrbitController orbit_controller_;
    cubey::host::FrameStats ui_frame_stats_{0.25};
    std::optional<FrameStatsSnapshot> latest_frame_stats_;
    Water3DRenderView render_view_ = Water3DRenderView::Surface;
    double latest_fps_ = 0.0;
    double latest_frame_ms_ = 0.0;
    double last_gpu_timing_print_seconds_ = -1.0;
    bool paused_ = false;
    bool reset_requested_ = true;
};

} // namespace

int run_water_3d(const RunConfig& config, Water3DAppInfo app_info) {
    Water3DApp app(config, app_info);
    return app.run();
}

} // namespace cubey::projects::fluid::water_3d
