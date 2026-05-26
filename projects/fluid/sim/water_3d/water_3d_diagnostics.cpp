#include "water_3d_diagnostics.h"

#include <cstring>
#include <stdexcept>
#include <string_view>

namespace cubey::projects::fluid::water_3d {

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

namespace {

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

} // namespace

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

namespace {

void record_metric(cubey::profiling::ProfileRecorder& recorder, std::uint64_t frame_index,
                   std::string_view category, std::string_view name, double value) {
    recorder.record_metric(frame_index, category, name, value);
}

} // namespace

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

} // namespace cubey::projects::fluid::water_3d
