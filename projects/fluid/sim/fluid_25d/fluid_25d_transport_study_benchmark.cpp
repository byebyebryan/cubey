#include "fluid_25d_finite_volume_oracle.h"
#include "fluid_25d_transport_study.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <exception>
#include <iostream>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace cubey::projects::fluid::fluid_25d {
namespace {

using Json = nlohmann::json;

constexpr double kGravity = 9.81;
constexpr double kDamping = 0.15;
constexpr double kDomainWidthM = 1920.0;
constexpr double kDomainHeightM = 240.0;
constexpr double kSampleXMinimumM = 480.0;
constexpr double kSampleXMaximumM = 1440.0;
constexpr double kSampleYMinimumM = 60.0;
constexpr double kSampleYMaximumM = 180.0;
constexpr double kDepthRelativeTolerance = 0.01;
constexpr double kVelocityRelativeTolerance = 0.05;
constexpr double kFaceTransferRelativeTolerance = 0.05;
constexpr double kZeroVelocityTolerance = 1.0e-6;
constexpr double kLedgerRelativeTolerance = 2.0e-6;
constexpr double kLedgerMinimumToleranceM3 = 1.0e-5;
constexpr double kBoundaryDepthToleranceM = 1.0e-8;
constexpr double kBoundaryVelocityToleranceMPerS = 1.0e-8;
constexpr std::uint32_t kSubsteps = 64U;
constexpr std::array<double, 5U> kSampleTimes{2.0, 4.0, 6.0, 8.0, 10.0};
constexpr std::array<double, 3U> kCellSizes{30.0, 15.0, 7.5};
constexpr std::array<double, 4U> kSlopes{0.0, 0.005, 0.05, 0.25};
constexpr std::array<double, 3U> kDepths{0.02, 0.2, 2.0};

struct TransportCase {
    std::string id;
    double cell_size_m = 30.0;
    double slope = 0.0;
    double depth_m = 0.2;
    double damping_per_s = kDamping;
    double fixed_step_s = 2.0;
    std::uint32_t substeps = kSubsteps;
    double domain_width_m = kDomainWidthM;
    double domain_height_m = kDomainHeightM;
    double origin_x_m = 0.0;
    double origin_y_m = 0.0;
};

struct SampleSnapshot {
    double time_s = 0.0;
    std::vector<double> depth_m;
    std::vector<double> velocity_x_m_per_s;
};

struct CandidateRun {
    Json report;
    std::vector<SampleSnapshot> snapshots;
};

[[nodiscard]] double analytic_velocity(double slope, double damping, double time_s) {
    if (!std::isfinite(slope) || slope < 0.0 || !std::isfinite(damping) || damping < 0.0 ||
        !std::isfinite(time_s) || time_s < 0.0) {
        throw std::invalid_argument("analytic incline inputs must be finite and nonnegative");
    }
    if (damping == 0.0) {
        return kGravity * slope * time_s;
    }
    return (kGravity * slope / damping) * -std::expm1(-damping * time_s);
}

[[nodiscard]] double analytic_integrated_velocity(double slope, double damping, double time_s) {
    if (damping == 0.0) {
        return 0.5 * kGravity * slope * time_s * time_s;
    }
    return (kGravity * slope / damping) * (time_s + std::expm1(-damping * time_s) / damping);
}

[[nodiscard]] double ledger_tolerance(double volume_m3) {
    return std::max(kLedgerMinimumToleranceM3, volume_m3 * kLedgerRelativeTolerance);
}

[[nodiscard]] bool finite_ledger(const Fluid25DStepLedger& ledger) {
    return std::isfinite(ledger.volume_before_m3) && std::isfinite(ledger.volume_after_m3) &&
           std::isfinite(ledger.source_volume_m3) && std::isfinite(ledger.sink_volume_m3) &&
           std::isfinite(ledger.boundary_outflow_volume_m3) &&
           std::isfinite(ledger.conservation_error_m3());
}

[[nodiscard]] Fluid25DConfig make_config(std::uint32_t width, std::uint32_t height,
                                         double cell_size_m, double fixed_step_s,
                                         std::uint32_t substeps, double damping,
                                         double minimum_wet_depth_m = 1.0e-4) {
    Fluid25DConfig config;
    config.grid_width = width;
    config.grid_height = height;
    config.cell_size_m = static_cast<float>(cell_size_m);
    config.fixed_delta_seconds = static_cast<float>(fixed_step_s);
    config.simulation_substeps = substeps;
    config.gravity_m_per_s2 = static_cast<float>(kGravity);
    config.flow_damping_per_second = static_cast<float>(damping);
    config.minimum_wet_depth_m = static_cast<float>(minimum_wet_depth_m);
    config.scenario = Fluid25DScenario::DryBed;
    config.solver = Fluid25DSolver::FiniteVolume;
    validate_fluid_25d_config(config);
    return config;
}

[[nodiscard]] Fluid25DScenarioData make_scenario(const Fluid25DConfig& config) {
    Fluid25DScenarioData scenario;
    scenario.width = config.grid_width;
    scenario.height = config.grid_height;
    scenario.cell_size_m = config.cell_size_m;
    const std::size_t count = fluid_25d_cell_count(config);
    scenario.terrain_height_m.assign(count, 0.0F);
    scenario.initial_water_depth_m.assign(count, 0.0F);
    scenario.source_depth_rate_m_per_s.assign(count, 0.0F);
    scenario.sink_depth_rate_m_per_s.assign(count, 0.0F);
    scenario.boundary_outflow_face_mask.assign(count, 0U);
    return scenario;
}

[[nodiscard]] std::pair<std::uint32_t, std::uint32_t> grid_for(const TransportCase& parameters) {
    const auto cells_for_length = [dx = parameters.cell_size_m](double length) {
        const double cells = length / dx;
        const auto rounded = static_cast<std::uint32_t>(std::llround(cells));
        if (rounded < 2U || std::abs(static_cast<double>(rounded) * dx - length) > 1.0e-8) {
            throw std::invalid_argument("cell size must divide the physical study domain");
        }
        return rounded;
    };
    return {cells_for_length(parameters.domain_width_m),
            cells_for_length(parameters.domain_height_m)};
}

[[nodiscard]] Fluid25DScenarioData
make_incline_scenario(const TransportCase& parameters, std::uint32_t width, std::uint32_t height) {
    const auto config = make_config(width, height, parameters.cell_size_m, parameters.fixed_step_s,
                                    parameters.substeps, parameters.damping_per_s);
    auto scenario = make_scenario(config);
    for (std::uint32_t y = 0U; y < height; ++y) {
        for (std::uint32_t x = 0U; x < width; ++x) {
            const double physical_x_m =
                parameters.origin_x_m + (static_cast<double>(x) + 0.5) * parameters.cell_size_m;
            const auto index = fluid_25d_scenario_index(width, height, x, y);
            scenario.terrain_height_m[index] = static_cast<float>(-parameters.slope * physical_x_m);
            scenario.initial_water_depth_m[index] = static_cast<float>(parameters.depth_m);
        }
    }
    return scenario;
}

template <typename StateAt>
[[nodiscard]] SampleSnapshot
sample_state(const TransportCase& parameters, std::uint32_t width, std::uint32_t height,
             double time_s, StateAt state_at, double& max_depth_relative_error,
             double& max_velocity_relative_error, double& max_velocity_absolute_error,
             double& mean_depth_m, double& mean_velocity_m_per_s) {
    SampleSnapshot snapshot;
    snapshot.time_s = time_s;
    const double expected_velocity =
        analytic_velocity(parameters.slope, parameters.damping_per_s, time_s);
    std::size_t count = 0U;
    for (std::uint32_t y = 0U; y < height; ++y) {
        const double py =
            parameters.origin_y_m + (static_cast<double>(y) + 0.5) * parameters.cell_size_m;
        if (py < kSampleYMinimumM || py > kSampleYMaximumM) {
            continue;
        }
        for (std::uint32_t x = 0U; x < width; ++x) {
            const double px =
                parameters.origin_x_m + (static_cast<double>(x) + 0.5) * parameters.cell_size_m;
            if (px < kSampleXMinimumM || px > kSampleXMaximumM) {
                continue;
            }
            const std::size_t index = fluid_25d_scenario_index(width, height, x, y);
            const auto [depth, velocity_x] = state_at(index);
            if (!std::isfinite(depth) || !std::isfinite(velocity_x)) {
                throw std::runtime_error("transport study sampled a nonfinite state");
            }
            snapshot.depth_m.push_back(depth);
            snapshot.velocity_x_m_per_s.push_back(velocity_x);
            mean_depth_m += depth;
            mean_velocity_m_per_s += velocity_x;
            max_depth_relative_error =
                std::max(max_depth_relative_error,
                         std::abs(depth - parameters.depth_m) / parameters.depth_m);
            const double velocity_error = std::abs(velocity_x - expected_velocity);
            max_velocity_absolute_error = std::max(max_velocity_absolute_error, velocity_error);
            if (expected_velocity > 0.0) {
                max_velocity_relative_error =
                    std::max(max_velocity_relative_error, velocity_error / expected_velocity);
            }
            ++count;
        }
    }
    if (count == 0U) {
        throw std::runtime_error("transport study sample window contains no cells");
    }
    const double inverse_count = 1.0 / static_cast<double>(count);
    mean_depth_m *= inverse_count;
    mean_velocity_m_per_s *= inverse_count;
    return snapshot;
}

[[nodiscard]] CandidateRun run_candidate_incline(const TransportCase& parameters) {
    const auto [width, height] = grid_for(parameters);
    const auto config = make_config(width, height, parameters.cell_size_m, parameters.fixed_step_s,
                                    parameters.substeps, parameters.damping_per_s);
    auto scenario = make_incline_scenario(parameters, width, height);
    Fluid25DTransportStudy study(config, scenario);
    const double initial_volume = study.total_water_volume_m3();
    std::map<std::pair<std::size_t, std::size_t>, double> cumulative_face_volume;
    std::vector<SampleSnapshot> snapshots;
    Json samples = Json::array();
    double max_depth_relative_error = 0.0;
    double max_velocity_relative_error = 0.0;
    double max_velocity_absolute_error = 0.0;
    double max_face_transfer_relative_error = 0.0;
    double max_face_transfer_absolute_error_m3 = 0.0;
    double max_step_ledger_residual = 0.0;
    double source_volume = 0.0;
    double sink_volume = 0.0;
    double outflow_volume = 0.0;
    double max_cfl = 0.0;
    std::uint64_t clipped_faces = 0U;
    bool ledger_pass = true;

    const std::uint32_t steps_per_sample =
        static_cast<std::uint32_t>(std::llround(2.0 / parameters.fixed_step_s));
    if (steps_per_sample == 0U ||
        std::abs(parameters.fixed_step_s * steps_per_sample - 2.0) > 1.0e-9) {
        throw std::invalid_argument("fixed step must divide the two-second sample interval");
    }

    for (std::size_t sample_index = 0U; sample_index < kSampleTimes.size(); ++sample_index) {
        for (std::uint32_t step_index = 0U; step_index < steps_per_sample; ++step_index) {
            const Fluid25DTracerStepResult result = study.step();
            const auto& ledger = result.water;
            const double residual = ledger.conservation_error_m3();
            if (!finite_ledger(ledger) || !std::isfinite(study.last_cfl_number()) ||
                study.last_cfl_number() > Fluid25DTransportStudy::kTargetCfl + 1.0e-12) {
                throw std::runtime_error("candidate produced invalid ledger or reconstructed CFL");
            }
            max_step_ledger_residual = std::max(max_step_ledger_residual, std::abs(residual));
            ledger_pass =
                ledger_pass && std::abs(residual) <= ledger_tolerance(ledger.volume_after_m3);
            source_volume += ledger.source_volume_m3;
            sink_volume += ledger.sink_volume_m3;
            outflow_volume += ledger.boundary_outflow_volume_m3;
            max_cfl = std::max(max_cfl, study.last_cfl_number());
            clipped_faces += study.last_clipped_faces();

            for (const auto& transfer : study.last_face_transfers()) {
                if (transfer.normal_x < 0.5 || transfer.right_cell == kFluid25DNoCell) {
                    continue;
                }
                const std::uint32_t left_x = static_cast<std::uint32_t>(transfer.left_cell % width);
                const std::uint32_t left_y = static_cast<std::uint32_t>(transfer.left_cell / width);
                const double face_x = parameters.origin_x_m +
                                      (static_cast<double>(left_x) + 1.0) * parameters.cell_size_m;
                const double face_y = parameters.origin_y_m +
                                      (static_cast<double>(left_y) + 0.5) * parameters.cell_size_m;
                if (face_x >= kSampleXMinimumM && face_x <= kSampleXMaximumM &&
                    face_y >= kSampleYMinimumM && face_y <= kSampleYMaximumM) {
                    cumulative_face_volume[{transfer.left_cell, transfer.right_cell}] +=
                        transfer.water_volume_m3;
                }
            }
        }

        const double time_s = static_cast<double>(sample_index + 1U) * 2.0;
        double sample_max_depth_relative = 0.0;
        double sample_max_velocity_relative = 0.0;
        double sample_max_velocity_absolute = 0.0;
        double mean_depth = 0.0;
        double mean_velocity = 0.0;
        auto snapshot = sample_state(
            parameters, width, height, time_s,
            [&study](std::size_t index) {
                const auto& cell = study.cells()[index];
                const double velocity_x =
                    cell.depth_m > 0.0 ? cell.momentum_x_m2_per_s / cell.depth_m : 0.0;
                return std::pair{cell.depth_m, velocity_x};
            },
            sample_max_depth_relative, sample_max_velocity_relative, sample_max_velocity_absolute,
            mean_depth, mean_velocity);
        max_depth_relative_error = std::max(max_depth_relative_error, sample_max_depth_relative);
        max_velocity_relative_error =
            std::max(max_velocity_relative_error, sample_max_velocity_relative);
        max_velocity_absolute_error =
            std::max(max_velocity_absolute_error, sample_max_velocity_absolute);

        const double expected_face_volume =
            parameters.depth_m * parameters.cell_size_m *
            analytic_integrated_velocity(parameters.slope, parameters.damping_per_s, time_s);
        double sample_face_relative_error = 0.0;
        double sample_face_absolute_error = 0.0;
        std::size_t face_count = 0U;
        for (const auto& [key, volume] : cumulative_face_volume) {
            static_cast<void>(key);
            const double error = std::abs(volume - expected_face_volume);
            sample_face_absolute_error = std::max(sample_face_absolute_error, error);
            if (expected_face_volume > 0.0) {
                sample_face_relative_error =
                    std::max(sample_face_relative_error, error / expected_face_volume);
            }
            ++face_count;
        }
        if (face_count == 0U) {
            throw std::runtime_error("candidate has no measured interior x-face transfers");
        }
        max_face_transfer_relative_error =
            std::max(max_face_transfer_relative_error, sample_face_relative_error);
        max_face_transfer_absolute_error_m3 =
            std::max(max_face_transfer_absolute_error_m3, sample_face_absolute_error);
        snapshots.push_back(std::move(snapshot));
        samples.push_back({
            {"time_s", time_s},
            {"measured_mean_depth_m", mean_depth},
            {"analytic_depth_m", parameters.depth_m},
            {"maximum_cell_depth_relative_error", sample_max_depth_relative},
            {"depth_accuracy_pass", sample_max_depth_relative <= kDepthRelativeTolerance},
            {"measured_mean_velocity_x_m_per_s", mean_velocity},
            {"analytic_velocity_x_m_per_s",
             analytic_velocity(parameters.slope, parameters.damping_per_s, time_s)},
            {"maximum_cell_velocity_relative_error",
             parameters.slope > 0.0 ? Json(sample_max_velocity_relative) : Json(nullptr)},
            {"maximum_cell_velocity_absolute_error_m_per_s", sample_max_velocity_absolute},
            {"maximum_cumulative_face_transfer_relative_error",
             parameters.slope > 0.0 ? Json(sample_face_relative_error) : Json(nullptr)},
            {"maximum_cumulative_face_transfer_absolute_error_m3", sample_face_absolute_error},
            {"measured_interior_x_face_count", face_count},
        });
    }

    const double final_volume = study.total_water_volume_m3();
    const double net_residual =
        final_volume - initial_volume - source_volume + sink_volume + outflow_volume;
    const bool ledger_health = ledger_pass &&
                               std::abs(net_residual) <= ledger_tolerance(final_volume) &&
                               max_step_ledger_residual <= ledger_tolerance(final_volume);
    const bool depth_pass = max_depth_relative_error <= kDepthRelativeTolerance;
    const bool velocity_pass = parameters.slope > 0.0
                                   ? max_velocity_relative_error <= kVelocityRelativeTolerance
                                   : max_velocity_absolute_error <= kZeroVelocityTolerance;
    const bool face_pass = parameters.slope > 0.0
                               ? max_face_transfer_relative_error <= kFaceTransferRelativeTolerance
                               : max_face_transfer_absolute_error_m3 <= 1.0e-8;
    return {
        .report =
            {
                {"case_id", parameters.id},
                {"method", "candidate_muscl_hydrostatic_matched_source"},
                {"domain_m",
                 {{"width", parameters.domain_width_m},
                  {"height", parameters.domain_height_m},
                  {"origin_x", parameters.origin_x_m},
                  {"origin_y", parameters.origin_y_m}}},
                {"grid", {{"width_cells", width}, {"height_cells", height}}},
                {"cell_size_m", parameters.cell_size_m},
                {"terrain_slope", parameters.slope},
                {"initial_depth_m", parameters.depth_m},
                {"damping_per_s", parameters.damping_per_s},
                {"gravity_m_per_s2", kGravity},
                {"fixed_step_seconds", parameters.fixed_step_s},
                {"substeps", parameters.substeps},
                {"substep_seconds", parameters.fixed_step_s / parameters.substeps},
                {"candidate_stage_cfl_target", Fluid25DTransportStudy::kTargetCfl},
                {"initial_volume_m3", initial_volume},
                {"final_volume_m3", final_volume},
                {"source_volume_m3", source_volume},
                {"sink_volume_m3", sink_volume},
                {"boundary_outflow_volume_m3", outflow_volume},
                {"net_mass_balance_residual_m3", net_residual},
                {"maximum_absolute_step_ledger_residual_m3", max_step_ledger_residual},
                {"ledger_tolerance_m3", ledger_tolerance(final_volume)},
                {"conservation_health_pass", ledger_health},
                {"max_cfl", max_cfl},
                {"maximum_clipped_face_stages", clipped_faces},
                {"maximum_cell_depth_relative_error", max_depth_relative_error},
                {"depth_accuracy_pass", depth_pass},
                {"maximum_cell_velocity_relative_error",
                 parameters.slope > 0.0 ? Json(max_velocity_relative_error) : Json(nullptr)},
                {"maximum_cell_velocity_absolute_error_m_per_s", max_velocity_absolute_error},
                {"velocity_accuracy_pass", velocity_pass},
                {"maximum_cumulative_face_transfer_relative_error",
                 parameters.slope > 0.0 ? Json(max_face_transfer_relative_error) : Json(nullptr)},
                {"maximum_cumulative_face_transfer_absolute_error_m3",
                 max_face_transfer_absolute_error_m3},
                {"face_transfer_accuracy_pass", face_pass},
                {"accuracy_pass", depth_pass && velocity_pass && face_pass},
                {"execution_ok", true},
                {"samples", std::move(samples)},
            },
        .snapshots = std::move(snapshots),
    };
}

[[nodiscard]] Json run_legacy_incline(const TransportCase& parameters) {
    const auto [width, height] = grid_for(parameters);
    const auto config = make_config(width, height, parameters.cell_size_m, parameters.fixed_step_s,
                                    parameters.substeps, parameters.damping_per_s);
    auto scenario = make_incline_scenario(parameters, width, height);
    Fluid25DFiniteVolumeOracle oracle(config, scenario);
    const double initial_volume = oracle.total_water_volume_m3();
    Json samples = Json::array();
    double max_depth_relative_error = 0.0;
    double max_velocity_relative_error = 0.0;
    double max_velocity_absolute_error = 0.0;
    double max_cfl = 0.0;
    double source_volume = 0.0;
    double sink_volume = 0.0;
    double outflow_volume = 0.0;
    double max_step_residual = 0.0;
    bool ledger_pass = true;
    const std::uint32_t steps_per_sample =
        static_cast<std::uint32_t>(std::llround(2.0 / parameters.fixed_step_s));
    if (steps_per_sample == 0U ||
        std::abs(parameters.fixed_step_s * steps_per_sample - 2.0) > 1.0e-9) {
        throw std::invalid_argument("fixed step must divide the two-second sample interval");
    }
    for (std::size_t sample_index = 0U; sample_index < kSampleTimes.size(); ++sample_index) {
        for (std::uint32_t step = 0U; step < steps_per_sample; ++step) {
            const auto ledger = oracle.step();
            const double residual = ledger.conservation_error_m3();
            ledger_pass = ledger_pass && finite_ledger(ledger) &&
                          std::abs(residual) <= ledger_tolerance(ledger.volume_after_m3);
            max_step_residual = std::max(max_step_residual, std::abs(residual));
            source_volume += ledger.source_volume_m3;
            sink_volume += ledger.sink_volume_m3;
            outflow_volume += ledger.boundary_outflow_volume_m3;
            max_cfl = std::max(max_cfl, static_cast<double>(oracle.last_cfl_number()));
        }
        const double time_s = static_cast<double>(sample_index + 1U) * 2.0;
        double sample_max_depth_relative = 0.0;
        double sample_max_velocity_relative = 0.0;
        double sample_max_velocity_absolute = 0.0;
        double mean_depth = 0.0;
        double mean_velocity = 0.0;
        static_cast<void>(sample_state(
            parameters, width, height, time_s,
            [&oracle](std::size_t index) {
                return std::pair{static_cast<double>(oracle.water_depth_m()[index]),
                                 static_cast<double>(oracle.velocity_m_per_s()[index].x_m_per_s)};
            },
            sample_max_depth_relative, sample_max_velocity_relative, sample_max_velocity_absolute,
            mean_depth, mean_velocity));
        max_depth_relative_error = std::max(max_depth_relative_error, sample_max_depth_relative);
        max_velocity_relative_error =
            std::max(max_velocity_relative_error, sample_max_velocity_relative);
        max_velocity_absolute_error =
            std::max(max_velocity_absolute_error, sample_max_velocity_absolute);
        samples.push_back({
            {"time_s", time_s},
            {"measured_mean_depth_m", mean_depth},
            {"analytic_depth_m", parameters.depth_m},
            {"maximum_cell_depth_relative_error", sample_max_depth_relative},
            {"measured_mean_velocity_x_m_per_s", mean_velocity},
            {"analytic_velocity_x_m_per_s",
             analytic_velocity(parameters.slope, parameters.damping_per_s, time_s)},
            {"maximum_cell_velocity_relative_error",
             parameters.slope > 0.0 ? Json(sample_max_velocity_relative) : Json(nullptr)},
            {"maximum_cell_velocity_absolute_error_m_per_s", sample_max_velocity_absolute},
        });
    }
    const double final_volume = oracle.total_water_volume_m3();
    const double net_residual =
        final_volume - initial_volume - source_volume + sink_volume + outflow_volume;
    const bool ledger_health = ledger_pass &&
                               std::abs(net_residual) <= ledger_tolerance(final_volume) &&
                               max_step_residual <= ledger_tolerance(final_volume);
    const bool depth_pass = max_depth_relative_error <= kDepthRelativeTolerance;
    const bool velocity_pass = parameters.slope > 0.0
                                   ? max_velocity_relative_error <= kVelocityRelativeTolerance
                                   : max_velocity_absolute_error <= kZeroVelocityTolerance;
    return {
        {"case_id", parameters.id},
        {"method", "unchanged_first_order_finite_volume_oracle"},
        {"grid", {{"width_cells", width}, {"height_cells", height}}},
        {"cell_size_m", parameters.cell_size_m},
        {"terrain_slope", parameters.slope},
        {"initial_depth_m", parameters.depth_m},
        {"damping_per_s", parameters.damping_per_s},
        {"fixed_step_seconds", parameters.fixed_step_s},
        {"substeps", parameters.substeps},
        {"substep_seconds", parameters.fixed_step_s / parameters.substeps},
        {"paired_candidate_same_case_and_timestep", true},
        {"initial_volume_m3", initial_volume},
        {"final_volume_m3", final_volume},
        {"source_volume_m3", source_volume},
        {"sink_volume_m3", sink_volume},
        {"boundary_outflow_volume_m3", outflow_volume},
        {"net_mass_balance_residual_m3", net_residual},
        {"maximum_absolute_step_ledger_residual_m3", max_step_residual},
        {"ledger_tolerance_m3", ledger_tolerance(final_volume)},
        {"conservation_health_pass", ledger_health},
        {"max_cfl", max_cfl},
        {"maximum_cell_depth_relative_error", max_depth_relative_error},
        {"depth_accuracy_pass", depth_pass},
        {"maximum_cell_velocity_relative_error",
         parameters.slope > 0.0 ? Json(max_velocity_relative_error) : Json(nullptr)},
        {"maximum_cell_velocity_absolute_error_m_per_s", max_velocity_absolute_error},
        {"velocity_accuracy_pass", velocity_pass},
        {"numerical_face_transfer", nullptr},
        {"face_transfer_observed", false},
        {"execution_ok", true},
        {"samples", std::move(samples)},
    };
}

[[nodiscard]] Json compare_expanded_domain(const std::vector<SampleSnapshot>& baseline,
                                           const std::vector<SampleSnapshot>& expanded) {
    if (baseline.size() != expanded.size()) {
        throw std::runtime_error("expanded-domain control has different sample counts");
    }
    Json samples = Json::array();
    double maximum_depth_delta = 0.0;
    double maximum_velocity_delta = 0.0;
    bool all_cells_pass = true;
    std::size_t compared = 0U;
    for (std::size_t i = 0U; i < baseline.size(); ++i) {
        if (baseline[i].depth_m.size() != expanded[i].depth_m.size() ||
            baseline[i].velocity_x_m_per_s.size() != expanded[i].velocity_x_m_per_s.size()) {
            throw std::runtime_error("expanded-domain control sample topology differs");
        }
        double sample_depth_delta = 0.0;
        double sample_velocity_delta = 0.0;
        for (std::size_t cell = 0U; cell < baseline[i].depth_m.size(); ++cell) {
            const double depth_delta =
                std::abs(baseline[i].depth_m[cell] - expanded[i].depth_m[cell]);
            const double velocity_delta = std::abs(baseline[i].velocity_x_m_per_s[cell] -
                                                   expanded[i].velocity_x_m_per_s[cell]);
            sample_depth_delta = std::max(sample_depth_delta, depth_delta);
            sample_velocity_delta = std::max(sample_velocity_delta, velocity_delta);
            all_cells_pass = all_cells_pass && depth_delta <= kBoundaryDepthToleranceM &&
                             velocity_delta <= kBoundaryVelocityToleranceMPerS;
            ++compared;
        }
        maximum_depth_delta = std::max(maximum_depth_delta, sample_depth_delta);
        maximum_velocity_delta = std::max(maximum_velocity_delta, sample_velocity_delta);
        samples.push_back({{"time_s", baseline[i].time_s},
                           {"maximum_sample_cell_depth_delta_m", sample_depth_delta},
                           {"maximum_sample_cell_velocity_delta_m_per_s", sample_velocity_delta},
                           {"all_sample_cells_pass",
                            sample_depth_delta <= kBoundaryDepthToleranceM &&
                                sample_velocity_delta <= kBoundaryVelocityToleranceMPerS}});
    }
    return {
        {"expanded_x_domain", true},
        {"control_scope", "x-only domain expansion; closed y extent is unchanged"},
        {"comparison", "per-cell values at every sampled physical cell, not sample means"},
        {"sampled_cell_time_comparisons", compared},
        {"maximum_sample_cell_depth_delta_m", maximum_depth_delta},
        {"maximum_sample_cell_velocity_delta_m_per_s", maximum_velocity_delta},
        {"depth_tolerance_m", kBoundaryDepthToleranceM},
        {"velocity_tolerance_m_per_s", kBoundaryVelocityToleranceMPerS},
        {"pass", all_cells_pass},
        {"samples", std::move(samples)},
    };
}

[[nodiscard]] TransportCase expanded_x_case(const TransportCase& base) {
    TransportCase expanded = base;
    expanded.id += "-expanded-x";
    expanded.domain_width_m = 2.0 * base.domain_width_m;
    expanded.origin_x_m = base.origin_x_m - 0.5 * base.domain_width_m;
    return expanded;
}

[[nodiscard]] bool needs_expanded_domain_control(const TransportCase& parameters) {
    return parameters.slope > 0.0;
}

[[nodiscard]] TransportCase make_matrix_case(double dx, double slope, double depth) {
    return {
        .id =
            "dx" + std::to_string(dx) + "-s" + std::to_string(slope) + "-h" + std::to_string(depth),
        .cell_size_m = dx,
        .slope = slope,
        .depth_m = depth,
        .damping_per_s = kDamping,
        .fixed_step_s = 2.0,
        .substeps = kSubsteps,
    };
}

struct BernoulliParameters {
    double length_m = 480.0;
    double amplitude_m = 2.0;
    double discharge_m2_per_s = 0.2;
    double reference_depth_m = 0.02;
    double reference_bed_m = 0.0;
    double energy_head_m = 0.0;
};

[[nodiscard]] double specific_energy(double depth_m, double discharge_m2_per_s) {
    return depth_m + discharge_m2_per_s * discharge_m2_per_s / (2.0 * kGravity * depth_m * depth_m);
}

[[nodiscard]] double critical_depth(double discharge_m2_per_s) {
    return std::cbrt(discharge_m2_per_s * discharge_m2_per_s / kGravity);
}

[[nodiscard]] double bernoulli_bed(const BernoulliParameters& p, double x_m) {
    return p.amplitude_m * std::sin(2.0 * std::acos(-1.0) * x_m / p.length_m);
}

[[nodiscard]] double supercritical_depth(const BernoulliParameters& p, double x_m) {
    const double total_energy = p.energy_head_m - bernoulli_bed(p, x_m);
    double low = 1.0e-10;
    double high = critical_depth(p.discharge_m2_per_s);
    if (specific_energy(high, p.discharge_m2_per_s) > total_energy) {
        throw std::runtime_error("Bernoulli fixture crosses the critical-energy limit");
    }
    for (int iteration = 0; iteration < 100; ++iteration) {
        const double middle = 0.5 * (low + high);
        if (specific_energy(middle, p.discharge_m2_per_s) > total_energy) {
            low = middle;
        } else {
            high = middle;
        }
    }
    return 0.5 * (low + high);
}

[[nodiscard]] double bernoulli_cell_average_depth(const BernoulliParameters& p, double left_x_m,
                                                  double right_x_m) {
    // Composite Simpson quadrature is independent of the candidate reconstruction.
    constexpr int intervals = 64;
    const double dx = (right_x_m - left_x_m) / intervals;
    double sum = supercritical_depth(p, left_x_m) + supercritical_depth(p, right_x_m);
    for (int i = 1; i < intervals; ++i) {
        sum += (i % 2 == 0 ? 2.0 : 4.0) *
               supercritical_depth(p, left_x_m + static_cast<double>(i) * dx);
    }
    return sum * dx / (3.0 * (right_x_m - left_x_m));
}

[[nodiscard]] Json run_bernoulli_case(
    double cell_size_m, double horizon_s, double fixed_step_s = 0.05,
    Fluid25DTransportStudyMethod method = Fluid25DTransportStudyMethod::ReconstructedHydrostatic) {
    BernoulliParameters p;
    p.energy_head_m = p.reference_bed_m + p.reference_depth_m +
                      p.discharge_m2_per_s * p.discharge_m2_per_s /
                          (2.0 * kGravity * p.reference_depth_m * p.reference_depth_m);
    const auto width = static_cast<std::uint32_t>(std::llround(p.length_m / cell_size_m));
    constexpr std::uint32_t height = 2U;
    const auto config = make_config(width, height, cell_size_m, fixed_step_s, 1U, 0.0);
    auto scenario = make_scenario(config);
    std::vector<Fluid25DMomentum> initial_momentum(fluid_25d_cell_count(config));
    for (std::uint32_t y = 0U; y < height; ++y) {
        for (std::uint32_t x = 0U; x < width; ++x) {
            const double center_x = (static_cast<double>(x) + 0.5) * cell_size_m;
            const double mean_depth = bernoulli_cell_average_depth(
                p, static_cast<double>(x) * cell_size_m, static_cast<double>(x + 1U) * cell_size_m);
            const std::size_t index = fluid_25d_scenario_index(width, height, x, y);
            scenario.terrain_height_m[index] = static_cast<float>(bernoulli_bed(p, center_x));
            scenario.initial_water_depth_m[index] = static_cast<float>(mean_depth);
            initial_momentum[index].x_m2_per_s = static_cast<float>(p.discharge_m2_per_s);
        }
    }
    Fluid25DTransportStudy study(config, scenario, Fluid25DTransportStudyBoundary::PeriodicX,
                                 std::move(initial_momentum), {}, method);
    const double initial_volume = study.total_water_volume_m3();
    std::map<std::pair<std::size_t, std::size_t>, double> cumulative_face_volume;
    std::vector<double> sample_times{2.0};
    if (horizon_s >= 4.0) {
        sample_times.push_back(4.0);
    }
    if (horizon_s > (sample_times.empty() ? 0.0 : sample_times.back())) {
        sample_times.push_back(horizon_s);
    }
    Json samples = Json::array();
    double max_depth_relative = 0.0;
    double max_velocity_relative = 0.0;
    double max_face_relative = 0.0;
    double max_step_residual = 0.0;
    double max_cfl = 0.0;
    std::uint64_t clipped_faces = 0U;
    std::size_t completed_steps = 0U;
    for (double sample_time : sample_times) {
        const std::size_t target_steps =
            static_cast<std::size_t>(std::llround(sample_time / fixed_step_s));
        while (completed_steps < target_steps) {
            const auto result = study.step();
            max_step_residual =
                std::max(max_step_residual, std::abs(result.water.conservation_error_m3()));
            if (!finite_ledger(result.water) ||
                std::abs(result.water.conservation_error_m3()) >
                    ledger_tolerance(result.water.volume_after_m3)) {
                throw std::runtime_error("periodic Bernoulli case failed its water ledger");
            }
            max_cfl = std::max(max_cfl, study.last_cfl_number());
            clipped_faces += study.last_clipped_faces();
            for (const auto& transfer : study.last_face_transfers()) {
                if (transfer.normal_x > 0.5 && transfer.right_cell != kFluid25DNoCell) {
                    cumulative_face_volume[{transfer.left_cell, transfer.right_cell}] +=
                        transfer.water_volume_m3;
                }
            }
            ++completed_steps;
        }
        const double time_s = static_cast<double>(completed_steps) * fixed_step_s;
        double sample_depth_relative = 0.0;
        double sample_velocity_relative = 0.0;
        double sample_face_relative = 0.0;
        std::size_t cells_measured = 0U;
        std::size_t faces_measured = 0U;
        for (std::uint32_t y = 0U; y < height; ++y) {
            for (std::uint32_t x = 0U; x < width; ++x) {
                const std::size_t index = fluid_25d_scenario_index(width, height, x, y);
                const double reference_depth =
                    bernoulli_cell_average_depth(p, static_cast<double>(x) * cell_size_m,
                                                 static_cast<double>(x + 1U) * cell_size_m);
                const auto& cell = study.cells()[index];
                const double velocity =
                    cell.depth_m > 0.0 ? cell.momentum_x_m2_per_s / cell.depth_m : 0.0;
                const double reference_velocity = p.discharge_m2_per_s / reference_depth;
                sample_depth_relative =
                    std::max(sample_depth_relative,
                             std::abs(cell.depth_m - reference_depth) / reference_depth);
                sample_velocity_relative =
                    std::max(sample_velocity_relative,
                             std::abs(velocity - reference_velocity) / reference_velocity);
                ++cells_measured;
            }
        }
        const double expected_face_volume = p.discharge_m2_per_s * cell_size_m * time_s;
        for (const auto& [key, volume] : cumulative_face_volume) {
            static_cast<void>(key);
            sample_face_relative =
                std::max(sample_face_relative,
                         std::abs(volume - expected_face_volume) / expected_face_volume);
            ++faces_measured;
        }
        max_depth_relative = std::max(max_depth_relative, sample_depth_relative);
        max_velocity_relative = std::max(max_velocity_relative, sample_velocity_relative);
        max_face_relative = std::max(max_face_relative, sample_face_relative);
        samples.push_back({
            {"time_s", time_s},
            {"maximum_cell_mean_depth_relative_error", sample_depth_relative},
            {"maximum_cell_velocity_relative_error", sample_velocity_relative},
            {"maximum_actual_face_transfer_relative_error", sample_face_relative},
            {"measured_cells", cells_measured},
            {"measured_periodic_x_faces", faces_measured},
            {"expected_face_transfer_m3", expected_face_volume},
        });
    }
    const double final_volume = study.total_water_volume_m3();
    const double net_residual = final_volume - initial_volume;
    const bool depth_pass = max_depth_relative <= kDepthRelativeTolerance;
    const bool velocity_pass = max_velocity_relative <= kVelocityRelativeTolerance;
    const bool face_pass = max_face_relative <= kFaceTransferRelativeTolerance;
    return {
        {"fixture", "periodic_x_supercritical_bernoulli_sinusoidal_bed"},
        {"method", method == Fluid25DTransportStudyMethod::FreeSurfaceUpwind
                       ? "berthon_foucher_free_surface_upwind_first_order_study_prototype"
                       : "candidate_muscl_hydrostatic_matched_source"},
        {"bed",
         {{"equation", "z(x)=2*sin(2*pi*x/480) m"},
          {"length_m", p.length_m},
          {"amplitude_m", p.amplitude_m}}},
        {"reference",
         {{"gravity_m_per_s2", kGravity},
          {"discharge_m2_per_s", p.discharge_m2_per_s},
          {"reference_depth_m_at_z0", p.reference_depth_m},
          {"bernoulli_head_m", p.energy_head_m},
          {"equation", "h+q^2/(2*g*h^2)=B-z(x)"},
          {"root_branch", "unique 0<h<h_critical"},
          {"critical_depth_m", critical_depth(p.discharge_m2_per_s)},
          {"crest_depth_m", supercritical_depth(p, 120.0)},
          {"trough_depth_m", supercritical_depth(p, 360.0)},
          {"cell_mean_reference",
           "64-interval composite Simpson integral; scenario depth cast to float"}}},
        {"grid",
         {{"width_cells", width},
          {"height_cells", height},
          {"cell_size_m", cell_size_m},
          {"cells_per_period", width}}},
        {"boundary_mode", "periodic_x_closed_y"},
        {"fixed_step_seconds", fixed_step_s},
        {"substeps", 1},
        {"damping_per_s", 0.0},
        {"rain_sources_sinks", "disabled"},
        {"initialization_precision",
         "scenario depth and DEM are float-rounded; candidate state is double"},
        {"fallback_datum_padding_m", method == Fluid25DTransportStudyMethod::FreeSurfaceUpwind
                                         ? Json(Fluid25DTransportStudy::kFallbackDatumPaddingM)
                                         : Json(nullptr)},
        {"initial_volume_m3", initial_volume},
        {"final_volume_m3", final_volume},
        {"net_mass_balance_residual_m3", net_residual},
        {"maximum_step_ledger_residual_m3", max_step_residual},
        {"max_cfl", max_cfl},
        {"maximum_clipped_face_stages", clipped_faces},
        {"depth_relative_error_max", max_depth_relative},
        {"depth_tolerance_max", kDepthRelativeTolerance},
        {"depth_accuracy_pass", depth_pass},
        {"velocity_relative_error_max", max_velocity_relative},
        {"velocity_tolerance_max", kVelocityRelativeTolerance},
        {"velocity_accuracy_pass", velocity_pass},
        {"actual_face_transfer_relative_error_max", max_face_relative},
        {"actual_face_transfer_tolerance_max", kFaceTransferRelativeTolerance},
        {"face_transfer_accuracy_pass", face_pass},
        {"hard_native_30m_gate", cell_size_m == 30.0},
        {"accuracy_pass", depth_pass && velocity_pass && face_pass},
        {"execution_ok", true},
        {"samples", std::move(samples)},
    };
}

constexpr double kRainDepthRateMPerS = 0.012 / 3600.0;

[[nodiscard]] double analytic_rain_momentum(double time_s, double slope, double initial_depth,
                                            double rain_rate, double damping) {
    const double one_minus_decay = -std::expm1(-damping * time_s);
    return kGravity * slope *
           (initial_depth * one_minus_decay / damping +
            rain_rate * (time_s / damping - one_minus_decay / (damping * damping)));
}

[[nodiscard]] double analytic_rain_integrated_momentum(double time_s, double slope,
                                                       double initial_depth, double rain_rate,
                                                       double damping) {
    const double one_minus_decay = -std::expm1(-damping * time_s);
    const double initial_term = time_s / damping - one_minus_decay / (damping * damping);
    const double rain_term = time_s * time_s / (2.0 * damping) - time_s / (damping * damping) +
                             one_minus_decay / (damping * damping * damping);
    return kGravity * slope * (initial_depth * initial_term + rain_rate * rain_term);
}

[[nodiscard]] Json run_uniform_incline_rain_reference() {
    TransportCase parameters = make_matrix_case(30.0, 0.05, 0.02);
    parameters.id = "continuous_rain_uniform_incline_reference";
    parameters.domain_height_m = 240.0;
    const auto [width, height] = grid_for(parameters);
    const auto config = make_config(width, height, parameters.cell_size_m, parameters.fixed_step_s,
                                    parameters.substeps, parameters.damping_per_s);
    auto scenario = make_incline_scenario(parameters, width, height);
    std::fill(scenario.source_depth_rate_m_per_s.begin(), scenario.source_depth_rate_m_per_s.end(),
              static_cast<float>(kRainDepthRateMPerS));
    Fluid25DTransportStudy study(config, scenario);
    std::map<std::pair<std::size_t, std::size_t>, double> cumulative_face_volume;
    Json samples = Json::array();
    double max_depth_relative = 0.0;
    double max_velocity_relative = 0.0;
    double max_face_relative = 0.0;
    double source_volume = 0.0;
    double max_ledger_residual = 0.0;
    double max_cfl = 0.0;
    std::uint64_t completed_steps = 0U;
    for (std::size_t sample_index = 0U; sample_index < kSampleTimes.size(); ++sample_index) {
        const std::uint32_t steps_per_sample =
            static_cast<std::uint32_t>(std::llround(2.0 / parameters.fixed_step_s));
        for (std::uint32_t step_index = 0U; step_index < steps_per_sample; ++step_index) {
            const auto result = study.step();
            const double residual = result.water.conservation_error_m3();
            if (!finite_ledger(result.water) ||
                std::abs(residual) > ledger_tolerance(result.water.volume_after_m3) ||
                study.last_cfl_number() > Fluid25DTransportStudy::kTargetCfl) {
                throw std::runtime_error("uniform incline-rain candidate failed health checks");
            }
            source_volume += result.water.source_volume_m3;
            max_ledger_residual = std::max(max_ledger_residual, std::abs(residual));
            max_cfl = std::max(max_cfl, study.last_cfl_number());
            for (const auto& transfer : study.last_face_transfers()) {
                if (transfer.normal_x < 0.5 || transfer.right_cell == kFluid25DNoCell) {
                    continue;
                }
                const auto left_x = static_cast<std::uint32_t>(transfer.left_cell % width);
                const auto left_y = static_cast<std::uint32_t>(transfer.left_cell / width);
                const double face_x = (static_cast<double>(left_x) + 1.0) * parameters.cell_size_m;
                const double face_y = (static_cast<double>(left_y) + 0.5) * parameters.cell_size_m;
                if (face_x >= kSampleXMinimumM && face_x <= kSampleXMaximumM &&
                    face_y >= kSampleYMinimumM && face_y <= kSampleYMaximumM) {
                    cumulative_face_volume[{transfer.left_cell, transfer.right_cell}] +=
                        transfer.water_volume_m3;
                }
            }
            ++completed_steps;
        }
        const double time_s = static_cast<double>(completed_steps) * parameters.fixed_step_s;
        const double expected_depth = parameters.depth_m + kRainDepthRateMPerS * time_s;
        const double expected_momentum =
            analytic_rain_momentum(time_s, parameters.slope, parameters.depth_m,
                                   kRainDepthRateMPerS, parameters.damping_per_s);
        const double expected_velocity = expected_momentum / expected_depth;
        double sample_depth_relative = 0.0;
        double sample_velocity_relative = 0.0;
        std::size_t cells_measured = 0U;
        for (std::uint32_t y = 0U; y < height; ++y) {
            const double py = (static_cast<double>(y) + 0.5) * parameters.cell_size_m;
            if (py < kSampleYMinimumM || py > kSampleYMaximumM) {
                continue;
            }
            for (std::uint32_t x = 0U; x < width; ++x) {
                const double px = (static_cast<double>(x) + 0.5) * parameters.cell_size_m;
                if (px < kSampleXMinimumM || px > kSampleXMaximumM) {
                    continue;
                }
                const auto& cell = study.cells()[fluid_25d_scenario_index(width, height, x, y)];
                const double velocity =
                    cell.depth_m > 0.0 ? cell.momentum_x_m2_per_s / cell.depth_m : 0.0;
                sample_depth_relative =
                    std::max(sample_depth_relative,
                             std::abs(cell.depth_m - expected_depth) / expected_depth);
                sample_velocity_relative =
                    std::max(sample_velocity_relative, std::abs(velocity - expected_velocity) /
                                                           std::max(expected_velocity, 1.0e-12));
                ++cells_measured;
            }
        }
        const double expected_face_volume =
            analytic_rain_integrated_momentum(time_s, parameters.slope, parameters.depth_m,
                                              kRainDepthRateMPerS, parameters.damping_per_s) *
            parameters.cell_size_m;
        double sample_face_relative = 0.0;
        std::size_t faces_measured = 0U;
        for (const auto& [face, volume] : cumulative_face_volume) {
            static_cast<void>(face);
            sample_face_relative =
                std::max(sample_face_relative,
                         std::abs(volume - expected_face_volume) / expected_face_volume);
            ++faces_measured;
        }
        max_depth_relative = std::max(max_depth_relative, sample_depth_relative);
        max_velocity_relative = std::max(max_velocity_relative, sample_velocity_relative);
        max_face_relative = std::max(max_face_relative, sample_face_relative);
        samples.push_back({{"time_s", time_s},
                           {"analytic_depth_m", expected_depth},
                           {"analytic_velocity_m_per_s", expected_velocity},
                           {"analytic_integrated_face_transfer_m3", expected_face_volume},
                           {"maximum_cell_depth_relative_error", sample_depth_relative},
                           {"maximum_cell_velocity_relative_error", sample_velocity_relative},
                           {"maximum_actual_face_transfer_relative_error", sample_face_relative},
                           {"measured_cells", cells_measured},
                           {"measured_actual_interior_faces", faces_measured}});
    }
    const bool depth_pass = max_depth_relative <= kDepthRelativeTolerance;
    const bool velocity_pass = max_velocity_relative <= kVelocityRelativeTolerance;
    const bool face_pass = max_face_relative <= kFaceTransferRelativeTolerance;
    return {{"fixture", "continuous_uniform_rain_on_incline"},
            {"equations",
             {{"h(t)", "h0+R*t"},
              {"q(t)", "g*S*[h0*(1-exp(-gamma*t))/gamma + R*(t/gamma-(1-exp(-gamma*t))/gamma^2)]"},
              {"integrated_q", "g*S*[h0*(t/gamma-(1-exp(-gamma*t))/gamma^2) + "
                               "R*(t^2/(2*gamma)-t/gamma^2+(1-exp(-gamma*t))/gamma^3)]"},
              {"u(t)", "q(t)/h(t)"}}},
            {"initial_depth_m", parameters.depth_m},
            {"rain_rate_mm_per_hour", 12.0},
            {"rain_depth_rate_m_per_s", kRainDepthRateMPerS},
            {"slope", parameters.slope},
            {"damping_per_s", parameters.damping_per_s},
            {"gravity_m_per_s2", kGravity},
            {"fixed_step_seconds", parameters.fixed_step_s},
            {"substeps", parameters.substeps},
            {"sample_times_s", kSampleTimes},
            {"face_metric", "actual SSPRK-weighted face volumes; never substituted by h*u"},
            {"maximum_cell_depth_relative_error", max_depth_relative},
            {"maximum_cell_velocity_relative_error", max_velocity_relative},
            {"maximum_actual_face_transfer_relative_error", max_face_relative},
            {"depth_accuracy_pass", depth_pass},
            {"velocity_accuracy_pass", velocity_pass},
            {"face_transfer_accuracy_pass", face_pass},
            {"accuracy_pass", depth_pass && velocity_pass && face_pass},
            {"conservation_health_pass", true},
            {"source_volume_m3", source_volume},
            {"maximum_ledger_residual_m3", max_ledger_residual},
            {"max_cfl", max_cfl},
            {"samples", std::move(samples)}};
}

using ControlRun = Json;

[[nodiscard]] ControlRun run_lake_rest_control(bool partially_wet,
                                               Fluid25DTransportStudyMethod method,
                                               bool curved_bed = false) {
    auto config = make_config(9U, 9U, 10.0, 0.01, 1U, 0.0);
    auto scenario = make_scenario(config);
    const double surface =
        curved_bed ? (partially_wet ? 0.25 : 0.75) : (partially_wet ? 0.35 : 1.0);
    std::vector<double> initial_depth(scenario.initial_water_depth_m.size());
    std::size_t initially_wet = 0U;
    std::size_t initially_dry = 0U;
    for (std::uint32_t y = 0U; y < scenario.height; ++y) {
        for (std::uint32_t x = 0U; x < scenario.width; ++x) {
            const double offset_x = static_cast<double>(static_cast<int>(x) - 4);
            const double offset_y = static_cast<double>(static_cast<int>(y) - 4);
            const double bed = curved_bed ? 0.015625 * (offset_x * offset_x + offset_y * offset_y)
                                          : 0.1 * (std::abs(offset_x) + std::abs(offset_y));
            const std::size_t index =
                fluid_25d_scenario_index(scenario.width, scenario.height, x, y);
            scenario.terrain_height_m[index] = static_cast<float>(bed);
            const double depth = std::max(0.0, surface - bed);
            scenario.initial_water_depth_m[index] = static_cast<float>(depth);
            initial_depth[index] = static_cast<double>(scenario.initial_water_depth_m[index]);
            if (depth > 0.0) {
                ++initially_wet;
            } else {
                ++initially_dry;
            }
        }
    }
    Fluid25DTransportStudy study(config, scenario, Fluid25DTransportStudyBoundary::ScenarioFaces,
                                 {}, {}, method);
    double max_depth_delta = 0.0;
    double max_speed = 0.0;
    double max_step_ledger_residual = 0.0;
    std::size_t rewetted_initially_dry_cells = 0U;
    bool ledger_pass = true;
    for (int step = 0; step < 100; ++step) {
        const auto result = study.step();
        const double residual = result.water.conservation_error_m3();
        ledger_pass = ledger_pass && finite_ledger(result.water) &&
                      std::abs(residual) <= ledger_tolerance(result.water.volume_after_m3);
        max_step_ledger_residual = std::max(max_step_ledger_residual, std::abs(residual));
        for (std::size_t i = 0U; i < study.cells().size(); ++i) {
            const auto& cell = study.cells()[i];
            max_depth_delta = std::max(max_depth_delta, std::abs(cell.depth_m - initial_depth[i]));
            const double u = cell.depth_m > config.minimum_wet_depth_m
                                 ? cell.momentum_x_m2_per_s / cell.depth_m
                                 : 0.0;
            const double v = cell.depth_m > config.minimum_wet_depth_m
                                 ? cell.momentum_y_m2_per_s / cell.depth_m
                                 : 0.0;
            max_speed = std::max(max_speed, std::hypot(u, v));
            if (initial_depth[i] == 0.0 && cell.depth_m > config.minimum_wet_depth_m) {
                ++rewetted_initially_dry_cells;
            }
        }
    }
    constexpr double depth_tolerance_m = 1.0e-5;
    constexpr double velocity_tolerance_m_per_s = 1.0e-5;
    const bool topology_pass = partially_wet
                                   ? initially_wet > 0U && initially_dry > 0U
                                   : initially_wet == initial_depth.size() && initially_dry == 0U;
    const bool pass = topology_pass && ledger_pass && max_depth_delta <= depth_tolerance_m &&
                      max_speed <= velocity_tolerance_m_per_s && rewetted_initially_dry_cells == 0U;
    return {{"fixture",
             curved_bed ? "partially_wet_smooth_curved_still_lake"
                        : (partially_wet ? "partially_wet_still_lake" : "fully_wet_still_lake")},
            {"method", method == Fluid25DTransportStudyMethod::FreeSurfaceUpwind
                           ? "berthon_foucher_free_surface_upwind_first_order_study_prototype"
                           : "candidate_muscl_hydrostatic_matched_source"},
            {"datum_m", 0.0},
            {"surface_elevation_m", surface},
            {"bed_equation",
             curved_bed ? "z=0.015625*((x-4)^2+(y-4)^2) m" : "z=0.1*(abs(x-4)+abs(y-4)) m"},
            {"grid", {{"width_cells", 9}, {"height_cells", 9}, {"cell_size_m", 10.0}}},
            {"fixed_step_seconds", 0.01},
            {"steps", 100},
            {"initially_wet_cells", initially_wet},
            {"initially_dry_cells", initially_dry},
            {"rewetted_initially_dry_cells", rewetted_initially_dry_cells},
            {"maximum_cell_depth_delta_m", max_depth_delta},
            {"maximum_cell_speed_m_per_s", max_speed},
            {"maximum_step_ledger_residual_m3", max_step_ledger_residual},
            {"depth_tolerance_m", depth_tolerance_m},
            {"velocity_tolerance_m_per_s", velocity_tolerance_m_per_s},
            {"conservation_health_pass", ledger_pass},
            {"pass", pass}};
}

[[nodiscard]] ControlRun run_high_datum_film_control(Fluid25DTransportStudyMethod method) {
    auto config = make_config(8U, 2U, 30.0, 0.01, 1U, 0.0);
    auto low_scenario = make_scenario(config);
    auto high_scenario = make_scenario(config);
    constexpr float kFilmDepthM = 0.001F;
    constexpr float kDatumM = 100000.0F;
    for (std::uint32_t y = 0U; y < config.grid_height; ++y) {
        for (std::uint32_t x = 0U; x < config.grid_width; ++x) {
            const std::size_t index =
                fluid_25d_scenario_index(config.grid_width, config.grid_height, x, y);
            const float relative_bed = -7.5F * static_cast<float>(x);
            low_scenario.terrain_height_m[index] = relative_bed;
            high_scenario.terrain_height_m[index] = kDatumM + relative_bed;
            low_scenario.initial_water_depth_m[index] = kFilmDepthM;
            high_scenario.initial_water_depth_m[index] = kFilmDepthM;
        }
    }
    Fluid25DTransportStudy low(config, low_scenario, Fluid25DTransportStudyBoundary::ScenarioFaces,
                               {}, {}, method);
    Fluid25DTransportStudy high(config, high_scenario,
                                Fluid25DTransportStudyBoundary::ScenarioFaces, {}, {}, method);
    double max_depth_delta = 0.0;
    double max_momentum_delta = 0.0;
    bool ledger_pass = true;
    for (int step = 0; step < 100; ++step) {
        const auto low_result = low.step();
        const auto high_result = high.step();
        ledger_pass = ledger_pass && finite_ledger(low_result.water) &&
                      finite_ledger(high_result.water) &&
                      std::abs(low_result.water.conservation_error_m3()) <=
                          ledger_tolerance(low_result.water.volume_after_m3) &&
                      std::abs(high_result.water.conservation_error_m3()) <=
                          ledger_tolerance(high_result.water.volume_after_m3);
        for (std::size_t i = 0U; i < low.cells().size(); ++i) {
            max_depth_delta = std::max(max_depth_delta,
                                       std::abs(low.cells()[i].depth_m - high.cells()[i].depth_m));
            max_momentum_delta = std::max(
                {max_momentum_delta,
                 std::abs(low.cells()[i].momentum_x_m2_per_s - high.cells()[i].momentum_x_m2_per_s),
                 std::abs(low.cells()[i].momentum_y_m2_per_s -
                          high.cells()[i].momentum_y_m2_per_s)});
        }
    }
    constexpr double state_tolerance = 1.0e-10;
    const bool pass =
        ledger_pass && max_depth_delta <= state_tolerance && max_momentum_delta <= state_tolerance;
    return {
        {"fixture", "high_absolute_datum_1mm_film_and_equivalent_relative_datum"},
        {"method", method == Fluid25DTransportStudyMethod::FreeSurfaceUpwind
                       ? "berthon_foucher_free_surface_upwind_first_order_study_prototype"
                       : "candidate_muscl_hydrostatic_matched_source"},
        {"high_bed_datum_m", kDatumM},
        {"minimum_high_bed_elevation_m", *std::min_element(high_scenario.terrain_height_m.begin(),
                                                           high_scenario.terrain_height_m.end())},
        {"maximum_high_bed_elevation_m", *std::max_element(high_scenario.terrain_height_m.begin(),
                                                           high_scenario.terrain_height_m.end())},
        {"film_depth_m", kFilmDepthM},
        {"bed_difference_per_cell_m", 7.5},
        {"fixed_step_seconds", 0.01},
        {"steps", 100},
        {"maximum_low_vs_high_datum_depth_delta_m", max_depth_delta},
        {"maximum_low_vs_high_datum_momentum_delta_m2_per_s", max_momentum_delta},
        {"state_tolerance", state_tolerance},
        {"conservation_health_pass", ledger_pass},
        {"pass", pass}};
}

[[nodiscard]] ControlRun run_dry_uniform_rain_control() {
    constexpr std::uint32_t width = 4U;
    constexpr std::uint32_t height = 4U;
    constexpr double dx = 30.0;
    constexpr double duration_s = 600.0;
    auto config = make_config(width, height, dx, 2.0, 1U, 0.0);
    auto scenario = make_scenario(config);
    std::fill(scenario.source_depth_rate_m_per_s.begin(), scenario.source_depth_rate_m_per_s.end(),
              static_cast<float>(kRainDepthRateMPerS));
    Fluid25DTransportStudy study(config, scenario);
    double source_volume = 0.0;
    double maximum_step_ledger_residual = 0.0;
    bool ledger_pass = true;
    for (std::uint32_t step = 0U; step < 300U; ++step) {
        const auto result = study.step();
        const double residual = result.water.conservation_error_m3();
        ledger_pass = ledger_pass && finite_ledger(result.water) &&
                      std::abs(residual) <= ledger_tolerance(result.water.volume_after_m3);
        source_volume += result.water.source_volume_m3;
        maximum_step_ledger_residual = std::max(maximum_step_ledger_residual, std::abs(residual));
    }
    const double expected_depth = kRainDepthRateMPerS * duration_s;
    double maximum_depth_error = 0.0;
    for (const auto& cell : study.cells()) {
        maximum_depth_error =
            std::max(maximum_depth_error, std::abs(cell.depth_m - expected_depth));
    }
    const double expected_source = expected_depth * width * height * dx * dx;
    const double source_error = std::abs(source_volume - expected_source);
    constexpr double depth_tolerance_m = 2.0e-8;
    const bool pass = ledger_pass && maximum_depth_error <= depth_tolerance_m &&
                      source_error <= ledger_tolerance(expected_source) &&
                      study.total_water_volume_m3() > 0.0;
    return {{"fixture", "flat_continuous_rain_on_initially_dry_bed"},
            {"rain_rate_mm_per_hour", 12.0},
            {"duration_s", duration_s},
            {"expected_uniform_depth_m", expected_depth},
            {"measured_source_volume_m3", source_volume},
            {"analytic_source_volume_m3", expected_source},
            {"source_volume_error_m3", source_error},
            {"maximum_cell_depth_error_m", maximum_depth_error},
            {"depth_tolerance_m", depth_tolerance_m},
            {"maximum_step_ledger_residual_m3", maximum_step_ledger_residual},
            {"conservation_health_pass", ledger_pass},
            {"pass", pass}};
}

[[nodiscard]] ControlRun run_dam_break_dye_control() {
    constexpr std::uint32_t width = 32U;
    constexpr std::uint32_t height = 3U;
    auto config = make_config(width, height, 1.0, 0.01, 1U, 0.0);
    auto scenario = make_scenario(config);
    std::vector<double> tracer_q(fluid_25d_cell_count(config), 0.0);
    for (std::uint32_t y = 0U; y < height; ++y) {
        for (std::uint32_t x = 0U; x < 8U; ++x) {
            const auto i = fluid_25d_scenario_index(width, height, x, y);
            scenario.initial_water_depth_m[i] = 1.0F;
            tracer_q[i] = 1.0;
        }
    }
    Fluid25DTransportStudy study(config, scenario, Fluid25DTransportStudyBoundary::ScenarioFaces,
                                 {}, std::move(tracer_q));
    const double initial_water = study.total_water_volume_m3();
    const double initial_tracer = study.total_tracer_amount_m3();
    double max_water_ledger_residual = 0.0;
    double max_tracer_ledger_residual = 0.0;
    std::size_t downstream_wet_cells = 0U;
    double downstream_tracer_amount = 0.0;
    double actual_transferred_water = 0.0;
    double actual_transferred_tracer = 0.0;
    bool ledger_pass = true;
    bool tracer_flux_tracks_water = true;
    bool c_one_is_preserved = true;
    double max_cell_change_vs_face_incidence = 0.0;
    double max_face_tracer_water_delta = 0.0;
    for (int step = 0; step < 100; ++step) {
        std::vector<double> before_depth;
        before_depth.reserve(study.cells().size());
        for (const auto& cell : study.cells()) {
            before_depth.push_back(cell.depth_m);
        }
        const auto result = study.step();
        const double water_residual = result.water.conservation_error_m3();
        const double tracer_residual = result.tracer.conservation_error_m3();
        max_water_ledger_residual = std::max(max_water_ledger_residual, std::abs(water_residual));
        max_tracer_ledger_residual =
            std::max(max_tracer_ledger_residual, std::abs(tracer_residual));
        ledger_pass = ledger_pass && finite_ledger(result.water) &&
                      std::abs(water_residual) <= ledger_tolerance(result.water.volume_after_m3) &&
                      std::isfinite(tracer_residual) &&
                      std::abs(tracer_residual) <= ledger_tolerance(result.tracer.amount_after_m3);
        for (const auto& transfer : study.last_face_transfers()) {
            actual_transferred_water += std::abs(transfer.water_volume_m3);
            actual_transferred_tracer += std::abs(transfer.tracer_amount_m3);
            max_face_tracer_water_delta =
                std::max(max_face_tracer_water_delta,
                         std::abs(transfer.water_volume_m3 - transfer.tracer_amount_m3));
            tracer_flux_tracks_water =
                tracer_flux_tracks_water &&
                std::abs(transfer.water_volume_m3 - transfer.tracer_amount_m3) <= 1.0e-12;
        }
        std::vector<double> face_incidence_depth_delta(study.cells().size(), 0.0);
        for (const auto& transfer : study.last_face_transfers()) {
            if (transfer.left_cell != kFluid25DNoCell) {
                face_incidence_depth_delta[transfer.left_cell] -= transfer.water_volume_m3;
            }
            if (transfer.right_cell != kFluid25DNoCell) {
                face_incidence_depth_delta[transfer.right_cell] += transfer.water_volume_m3;
            }
        }
        for (std::size_t i = 0U; i < study.cells().size(); ++i) {
            const double actual_delta = study.cells()[i].depth_m - before_depth[i];
            max_cell_change_vs_face_incidence =
                std::max(max_cell_change_vs_face_incidence,
                         std::abs(actual_delta - face_incidence_depth_delta[i]));
            c_one_is_preserved =
                c_one_is_preserved &&
                std::abs(study.cells()[i].tracer_q_m - study.cells()[i].depth_m) <= 1.0e-12;
        }
    }
    for (std::uint32_t y = 0U; y < height; ++y) {
        for (std::uint32_t x = 8U; x < width; ++x) {
            const auto& cell = study.cells()[fluid_25d_scenario_index(width, height, x, y)];
            if (cell.depth_m > config.minimum_wet_depth_m) {
                ++downstream_wet_cells;
            }
            downstream_tracer_amount += cell.tracer_q_m;
        }
    }
    const bool pass = ledger_pass && tracer_flux_tracks_water && downstream_wet_cells > 0U &&
                      downstream_tracer_amount > 0.0 && actual_transferred_water > 0.0 &&
                      actual_transferred_tracer > 0.0 && c_one_is_preserved &&
                      max_face_tracer_water_delta <= 1.0e-12 &&
                      max_cell_change_vs_face_incidence <= 1.0e-10 &&
                      std::abs(study.total_water_volume_m3() - initial_water) <=
                          ledger_tolerance(initial_water) &&
                      std::abs(study.total_tracer_amount_m3() - initial_tracer) <=
                          ledger_tolerance(initial_tracer);
    return {
        {"fixture", "wet_dam_release_and_actual_dye_migration"},
        {"grid", {{"width_cells", width}, {"height_cells", height}, {"cell_size_m", 1.0}}},
        {"reservoir_cells_x", 8},
        {"duration_s", 1.0},
        {"initial_water_volume_m3", initial_water},
        {"final_water_volume_m3", study.total_water_volume_m3()},
        {"initial_tracer_amount_m3", initial_tracer},
        {"final_tracer_amount_m3", study.total_tracer_amount_m3()},
        {"downstream_wet_cells", downstream_wet_cells},
        {"downstream_tracer_amount_m3", downstream_tracer_amount},
        {"sum_abs_actual_face_water_transfer_m3", actual_transferred_water},
        {"sum_abs_actual_face_tracer_transfer_m3", actual_transferred_tracer},
        {"tracer_flux_tracks_water_flux", tracer_flux_tracks_water},
        {"initial_water_concentration", 1.0},
        {"concentration_one_preserved_in_all_wet_cells", c_one_is_preserved},
        {"maximum_actual_face_tracer_minus_water_transfer_m3", max_face_tracer_water_delta},
        {"maximum_cell_depth_change_vs_actual_face_incidence_m", max_cell_change_vs_face_incidence},
        {"maximum_water_ledger_residual_m3", max_water_ledger_residual},
        {"maximum_tracer_ledger_residual_m3", max_tracer_ledger_residual},
        {"conservation_health_pass", ledger_pass},
        {"pass", pass}};
}

[[nodiscard]] ControlRun run_source_sink_outflow_control() {
    constexpr std::uint32_t width = 4U;
    constexpr std::uint32_t height = 2U;
    constexpr double dx = 5.0;
    auto config = make_config(width, height, dx, 0.05, 4U, 0.0);
    auto scenario = make_scenario(config);
    std::fill(scenario.initial_water_depth_m.begin(), scenario.initial_water_depth_m.end(), 0.5F);
    const std::size_t source_cell = fluid_25d_scenario_index(width, height, 0U, 0U);
    const std::size_t sink_cell = fluid_25d_scenario_index(width, height, 1U, 0U);
    scenario.source_depth_rate_m_per_s[source_cell] = 0.01F;
    scenario.sink_depth_rate_m_per_s[sink_cell] = 0.002F;
    std::vector<Fluid25DMomentum> initial_momentum(fluid_25d_cell_count(config));
    std::vector<double> initial_tracer(fluid_25d_cell_count(config));
    for (std::uint32_t y = 0U; y < height; ++y) {
        for (std::uint32_t x = 0U; x < width; ++x) {
            const auto i = fluid_25d_scenario_index(width, height, x, y);
            initial_momentum[i].x_m2_per_s = 0.2F;
            initial_tracer[i] = 0.125;
        }
        const std::size_t edge = fluid_25d_scenario_index(width, height, width - 1U, y);
        scenario.boundary_outflow_face_mask[edge] = kFluid25DBoundaryOutflowRight;
    }
    Fluid25DTransportStudy study(config, scenario, Fluid25DTransportStudyBoundary::ScenarioFaces,
                                 std::move(initial_momentum), std::move(initial_tracer));
    double water_source = 0.0;
    double water_sink = 0.0;
    double water_outflow = 0.0;
    double tracer_source = 0.0;
    double tracer_sink = 0.0;
    double tracer_outflow = 0.0;
    double max_water_residual = 0.0;
    double max_tracer_residual = 0.0;
    bool pass = true;
    for (int step = 0; step < 40; ++step) {
        const auto result = study.step(1.0, 0.75);
        water_source += result.water.source_volume_m3;
        water_sink += result.water.sink_volume_m3;
        water_outflow += result.water.boundary_outflow_volume_m3;
        tracer_source += result.tracer.source_amount_m3;
        tracer_sink += result.tracer.sink_amount_m3;
        tracer_outflow += result.tracer.boundary_outflow_amount_m3;
        max_water_residual =
            std::max(max_water_residual, std::abs(result.water.conservation_error_m3()));
        max_tracer_residual =
            std::max(max_tracer_residual, std::abs(result.tracer.conservation_error_m3()));
        pass = pass && finite_ledger(result.water) &&
               std::abs(result.water.conservation_error_m3()) <=
                   ledger_tolerance(result.water.volume_after_m3) &&
               std::isfinite(result.tracer.conservation_error_m3()) &&
               std::abs(result.tracer.conservation_error_m3()) <=
                   ledger_tolerance(result.tracer.amount_after_m3);
    }
    pass = pass && water_source > 0.0 && water_sink > 0.0 && water_outflow > 0.0 &&
           tracer_source > 0.0 && tracer_sink > 0.0 && tracer_outflow > 0.0;
    return {{"fixture", "source_sink_and_marked_boundary_outflow"},
            {"steps", 40},
            {"fixed_step_seconds", 0.05},
            {"water_source_volume_m3", water_source},
            {"water_sink_volume_m3", water_sink},
            {"water_boundary_outflow_m3", water_outflow},
            {"tracer_source_amount_m3", tracer_source},
            {"tracer_sink_amount_m3", tracer_sink},
            {"tracer_boundary_outflow_amount_m3", tracer_outflow},
            {"maximum_water_ledger_residual_m3", max_water_residual},
            {"maximum_tracer_ledger_residual_m3", max_tracer_residual},
            {"pass", pass}};
}

[[nodiscard]] ControlRun run_threshold_crossing_film_control() {
    auto config = make_config(2U, 2U, 10.0, 0.1, 1U, 0.0);
    auto scenario = make_scenario(config);
    scenario.initial_water_depth_m[0] = 0.00011F;
    scenario.sink_depth_rate_m_per_s[0] = 0.0002F;
    std::vector<Fluid25DMomentum> momentum(fluid_25d_cell_count(config));
    momentum[0].x_m2_per_s = 0.00011F;
    Fluid25DTransportStudy study(config, scenario, Fluid25DTransportStudyBoundary::ScenarioFaces,
                                 std::move(momentum));
    const auto result = study.step();
    const auto& cell = study.cells().front();
    const bool pass = finite_ledger(result.water) &&
                      std::abs(result.water.conservation_error_m3()) <=
                          ledger_tolerance(result.water.volume_after_m3) &&
                      cell.depth_m < config.minimum_wet_depth_m &&
                      cell.momentum_x_m2_per_s == 0.0 && cell.momentum_y_m2_per_s == 0.0 &&
                      result.water.sink_volume_m3 > 0.0;
    return {{"fixture", "sink_crosses_minimum_wet_film_threshold"},
            {"initial_depth_m", 0.00011},
            {"sink_rate_m_per_s", 0.0002},
            {"fixed_step_seconds", 0.1},
            {"minimum_wet_depth_m", config.minimum_wet_depth_m},
            {"final_depth_m", cell.depth_m},
            {"final_momentum_x_m2_per_s", cell.momentum_x_m2_per_s},
            {"sink_volume_m3", result.water.sink_volume_m3},
            {"water_ledger_residual_m3", result.water.conservation_error_m3()},
            {"pass", pass}};
}

[[nodiscard]] ControlRun run_step_rejection_rollback_control() {
    auto config = make_config(2U, 2U, 1.0, 0.02, 2U, 0.0);
    auto scenario = make_scenario(config);
    std::fill(scenario.initial_water_depth_m.begin(), scenario.initial_water_depth_m.end(), 0.2F);
    std::fill(scenario.source_depth_rate_m_per_s.begin(), scenario.source_depth_rate_m_per_s.end(),
              1000.0F);
    std::vector<double> initial_tracer(fluid_25d_cell_count(config), 0.05);
    Fluid25DTransportStudy study(config, scenario, Fluid25DTransportStudyBoundary::ScenarioFaces,
                                 {}, std::move(initial_tracer));
    const auto first = study.step(0.0);
    const auto before_cells = study.cells();
    const auto before_transfers = study.last_face_transfers();
    const double before_water = study.total_water_volume_m3();
    const double before_cfl = study.last_cfl_number();
    const std::uint64_t before_clipped = study.last_clipped_faces();
    const std::uint64_t before_steps = study.completed_steps();
    std::string rejection;
    try {
        static_cast<void>(study.step(1.0, 1.0));
    } catch (const std::exception& error) {
        rejection = error.what();
    }
    bool state_unchanged = study.cells().size() == before_cells.size();
    for (std::size_t i = 0U; i < std::min(study.cells().size(), before_cells.size()); ++i) {
        const auto& after = study.cells()[i];
        const auto& before = before_cells[i];
        state_unchanged = state_unchanged && after.depth_m == before.depth_m &&
                          after.momentum_x_m2_per_s == before.momentum_x_m2_per_s &&
                          after.momentum_y_m2_per_s == before.momentum_y_m2_per_s &&
                          after.tracer_q_m == before.tracer_q_m;
    }
    bool transfers_unchanged = study.last_face_transfers().size() == before_transfers.size();
    for (std::size_t i = 0U;
         i < std::min(study.last_face_transfers().size(), before_transfers.size()); ++i) {
        const auto& after = study.last_face_transfers()[i];
        const auto& before = before_transfers[i];
        transfers_unchanged = transfers_unchanged &&
                              after.water_volume_m3 == before.water_volume_m3 &&
                              after.tracer_amount_m3 == before.tracer_amount_m3;
    }
    const bool later_substep_rejected = rejection.find("substep 1 RK stage 1") != std::string::npos;
    const bool pass =
        finite_ledger(first.water) && later_substep_rejected && state_unchanged &&
        transfers_unchanged && study.completed_steps() == before_steps &&
        study.total_water_volume_m3() == before_water && study.last_cfl_number() == before_cfl &&
        study.last_clipped_faces() == before_clipped && before_cells.front().tracer_q_m > 0.0;
    return {
        {"fixture", "later_substep_rejection_rolls_back_entire_public_step"},
        {"successful_steps_before_rejection", before_steps},
        {"rejection_message", rejection},
        {"rejection_occurs_after_prior_substep_work", later_substep_rejected},
        {"state_unchanged", state_unchanged},
        {"last_actual_face_transfers_unchanged", transfers_unchanged},
        {"completed_step_clock_unchanged", study.completed_steps() == before_steps},
        {"last_cfl_diagnostic_unchanged", study.last_cfl_number() == before_cfl},
        {"last_clipped_face_diagnostic_unchanged", study.last_clipped_faces() == before_clipped},
        {"nonzero_persistent_tracer_before_rejection", before_cells.front().tracer_q_m > 0.0},
        {"pass", pass}};
}

[[nodiscard]] ControlRun run_stage2_rejection_rollback_control() {
    constexpr std::uint32_t width = 3U;
    constexpr std::uint32_t height = 3U;
    auto config = make_config(width, height, 1.0, 0.01, 1U, 0.0);
    auto scenario = make_scenario(config);
    std::fill(scenario.initial_water_depth_m.begin(), scenario.initial_water_depth_m.end(), 0.2F);
    for (std::uint32_t y = 0U; y < height; ++y) {
        for (std::uint32_t x = 0U; x < width; ++x) {
            scenario.terrain_height_m[fluid_25d_scenario_index(width, height, x, y)] =
                -201.0F * static_cast<float>(x);
        }
    }
    std::vector<double> tracer(fluid_25d_cell_count(config), 0.05);
    Fluid25DTransportStudy study(config, scenario, Fluid25DTransportStudyBoundary::ScenarioFaces,
                                 {}, std::move(tracer));
    const auto before_cells = study.cells();
    const double before_water = study.total_water_volume_m3();
    std::string rejection;
    try {
        static_cast<void>(study.step(0.0, 1.0));
    } catch (const std::exception& error) {
        rejection = error.what();
    }
    bool state_unchanged = study.cells().size() == before_cells.size();
    for (std::size_t i = 0U; i < std::min(study.cells().size(), before_cells.size()); ++i) {
        const auto& after = study.cells()[i];
        const auto& before = before_cells[i];
        state_unchanged = state_unchanged && after.depth_m == before.depth_m &&
                          after.momentum_x_m2_per_s == before.momentum_x_m2_per_s &&
                          after.momentum_y_m2_per_s == before.momentum_y_m2_per_s &&
                          after.tracer_q_m == before.tracer_q_m;
    }
    const bool stage2_rejected = rejection.find("substep 0 RK stage 2") != std::string::npos;
    const bool pass = stage2_rejected && state_unchanged &&
                      study.total_water_volume_m3() == before_water &&
                      study.completed_steps() == 0U && study.last_face_transfers().empty() &&
                      study.last_cfl_number() == 0.0 && study.last_clipped_faces() == 0U;
    return {{"fixture", "second_ssprk_stage_rejection_is_atomic"},
            {"terrain_slope", 201.0},
            {"initial_depth_m", 0.2},
            {"grid", {{"width_cells", width}, {"height_cells", height}, {"cell_size_m", 1.0}}},
            {"fixed_step_seconds", 0.01},
            {"rejection_message", rejection},
            {"stage2_rejected", stage2_rejected},
            {"state_unchanged", state_unchanged},
            {"completed_step_clock_unchanged", study.completed_steps() == 0U},
            {"last_face_transfers_remain_empty", study.last_face_transfers().empty()},
            {"last_cfl_remains_initial", study.last_cfl_number() == 0.0},
            {"pass", pass}};
}

[[nodiscard]] ControlRun run_dye_schedule_reset_control() {
    auto config = make_config(4U, 2U, 5.0, 0.25, 1U, 0.0);
    config.scenario = Fluid25DScenario::SourceOutletDemo;
    config.dye_pulse_start_seconds = 0.25F;
    config.dye_pulse_duration_seconds = 0.5F;
    validate_fluid_25d_config(config);
    auto scenario = make_scenario(config);
    std::fill(scenario.initial_water_depth_m.begin(), scenario.initial_water_depth_m.end(), 0.2F);
    std::fill(scenario.source_depth_rate_m_per_s.begin(), scenario.source_depth_rate_m_per_s.end(),
              0.01F);
    Fluid25DTransportStudy study(config, scenario);
    const auto first = study.step_with_dye();
    const auto second = study.step_with_dye();
    const auto third = study.step_with_dye();
    const double first_source_tracer = first.tracer.source_amount_m3;
    const double second_source_tracer = second.tracer.source_amount_m3;
    const double third_source_tracer = third.tracer.source_amount_m3;
    const std::uint64_t before_reset = study.completed_steps();
    study.reset();
    const bool pass = first_source_tracer == 0.0 && second_source_tracer > 0.0 &&
                      third_source_tracer > 0.0 && before_reset == 3U &&
                      study.completed_steps() == 0U && study.total_tracer_amount_m3() == 0.0;
    return {{"fixture", "dye_source_schedule_and_reset_clock"},
            {"pulse_start_seconds", 0.25},
            {"pulse_duration_seconds", 0.5},
            {"completed_steps_before_reset", before_reset},
            {"first_step_source_tracer_amount_m3", first_source_tracer},
            {"second_step_source_tracer_amount_m3", second_source_tracer},
            {"third_step_source_tracer_amount_m3", third_source_tracer},
            {"completed_steps_after_reset", study.completed_steps()},
            {"tracer_amount_after_reset_m3", study.total_tracer_amount_m3()},
            {"pass", pass}};
}

[[nodiscard]] Json run_control_suite(Fluid25DTransportStudyMethod method,
                                     bool include_dye_and_rain) {
    const auto full_lake = run_lake_rest_control(false, method);
    const auto partial_lake = run_lake_rest_control(true, method);
    const auto curved_partial_lake = run_lake_rest_control(true, method, true);
    const auto datum = run_high_datum_film_control(method);
    Json controls = Json::array({full_lake, partial_lake, curved_partial_lake, datum});
    bool pass = full_lake.at("pass").get<bool>() && partial_lake.at("pass").get<bool>() &&
                curved_partial_lake.at("pass").get<bool>() && datum.at("pass").get<bool>();
    if (include_dye_and_rain) {
        const auto dry_rain = run_dry_uniform_rain_control();
        const auto dam_dye = run_dam_break_dye_control();
        const auto source_sink = run_source_sink_outflow_control();
        const auto threshold = run_threshold_crossing_film_control();
        const auto rollback = run_step_rejection_rollback_control();
        const auto stage2_rollback = run_stage2_rejection_rollback_control();
        const auto schedule = run_dye_schedule_reset_control();
        pass = pass && dry_rain.at("pass").get<bool>() && dam_dye.at("pass").get<bool>() &&
               source_sink.at("pass").get<bool>() && threshold.at("pass").get<bool>() &&
               rollback.at("pass").get<bool>() && stage2_rollback.at("pass").get<bool>() &&
               schedule.at("pass").get<bool>();
        controls.push_back(dry_rain);
        controls.push_back(dam_dye);
        controls.push_back(source_sink);
        controls.push_back(threshold);
        controls.push_back(rollback);
        controls.push_back(stage2_rollback);
        controls.push_back(schedule);
    }
    return {{"all_controls_pass", pass},
            {"control_count", controls.size()},
            {"controls", std::move(controls)}};
}

[[nodiscard]] Json run_fallback_probe() {
    const auto method = Fluid25DTransportStudyMethod::FreeSurfaceUpwind;
    Json curved_runs = Json::array();
    bool all_curved_gates_pass = true;
    bool all_executed = true;
    for (const double dx : kCellSizes) {
        try {
            Json curved = run_bernoulli_case(dx, 10.0, 0.01, method);
            all_curved_gates_pass = all_curved_gates_pass && curved.at("accuracy_pass").get<bool>();
            curved_runs.push_back(std::move(curved));
        } catch (const std::exception& error) {
            all_executed = false;
            all_curved_gates_pass = false;
            curved_runs.push_back({{"cell_size_m", dx},
                                   {"method", "FreeSurfaceUpwind"},
                                   {"execution_ok", false},
                                   {"accuracy_pass", false},
                                   {"error", error.what()}});
        }
    }
    const Json controls = run_control_suite(method, false);
    const bool controls_pass = controls.at("all_controls_pass").get<bool>();
    const bool gate_pass = all_executed && all_curved_gates_pass && controls_pass;
    return {{"schema", "cubey.fluid25d.transport_study.v3"},
            {"mode", "bounded_fallback_probe"},
            {"method_scope", "first-order Berthon/Foucher free-surface upwind prototype only; no "
                             "claim about the full paper family"},
            {"equation_reference", "Berthon and Foucher free-surface upwind 2D formulation, "
                                   "equations 5.11-5.13; first-order cell states"},
            {"method", "FreeSurfaceUpwind"},
            {"dt_seconds", 0.01},
            {"fallback_datum_padding_m", Fluid25DTransportStudy::kFallbackDatumPaddingM},
            {"fallback_axis_cfl", Fluid25DTransportStudy::kFallbackAxisCfl},
            {"fallback_sum_cfl", Fluid25DTransportStudy::kTargetCfl},
            {"curved_bernoulli_gate",
             {{"run_count", curved_runs.size()},
              {"all_resolution_gates_pass", all_curved_gates_pass},
              {"runs", std::move(curved_runs)}}},
            {"lake_and_datum_controls", controls},
            {"diagnostic_success", all_executed},
            {"fallback_probe_acceptance_pass", gate_pass}};
}

[[nodiscard]] Json run_probe() {
    const auto stress = make_matrix_case(30.0, 0.25, 0.02);
    auto candidate = run_candidate_incline(stress);
    auto legacy = run_legacy_incline(stress);
    const Json curved = run_bernoulli_case(30.0, 10.0);
    const bool health = candidate.report.at("conservation_health_pass").get<bool>() &&
                        legacy.at("conservation_health_pass").get<bool>() &&
                        curved.at("execution_ok").get<bool>();
    const bool accuracy =
        candidate.report.at("accuracy_pass").get<bool>() && curved.at("accuracy_pass").get<bool>();
    return {
        {"schema", "cubey.fluid25d.transport_study.v3"},
        {"mode", "probe"},
        {"diagnostic_success", health},
        {"candidate_acceptance_pass", accuracy},
        {"incline_candidate", std::move(candidate.report)},
        {"incline_legacy_paired", std::move(legacy)},
        {"curved_bernoulli_native_30m", curved},
    };
}

[[nodiscard]] Json run_self_test() {
    const double expected_head = 5.1168399592252802;
    const double head = 0.02 + 0.2 * 0.2 / (2.0 * kGravity * 0.02 * 0.02);
    BernoulliParameters p;
    p.energy_head_m = head;
    const double crest_depth = supercritical_depth(p, 120.0);
    const double trough_depth = supercritical_depth(p, 360.0);
    if (std::abs(head - expected_head) > 1.0e-12 ||
        std::abs(crest_depth - 0.02568146874096744) > 1.0e-12 ||
        std::abs(trough_depth - 0.01694550624521117) > 1.0e-12 ||
        !(crest_depth < critical_depth(p.discharge_m2_per_s)) ||
        !(trough_depth < critical_depth(p.discharge_m2_per_s))) {
        throw std::runtime_error("independent Bernoulli reference self-test failed");
    }

    auto config = make_config(4U, 3U, 30.0, 0.01, 1U, 0.0);
    auto scenario = make_scenario(config);
    std::fill(scenario.initial_water_depth_m.begin(), scenario.initial_water_depth_m.end(), 0.2F);
    Fluid25DTransportStudy study(config, scenario);
    const double initial_volume = study.total_water_volume_m3();
    for (int i = 0; i < 10; ++i) {
        const auto result = study.step();
        if (std::abs(result.water.conservation_error_m3()) > 1.0e-8 ||
            study.last_cfl_number() > Fluid25DTransportStudy::kTargetCfl) {
            throw std::runtime_error("flat transport candidate control failed");
        }
    }
    if (std::abs(study.total_water_volume_m3() - initial_volume) > 1.0e-8 ||
        std::any_of(study.cells().begin(), study.cells().end(), [](const auto& cell) {
            return std::abs(cell.depth_m - static_cast<double>(0.2F)) > 1.0e-12 ||
                   std::abs(cell.momentum_x_m2_per_s) > 1.0e-12 ||
                   std::abs(cell.momentum_y_m2_per_s) > 1.0e-12;
        })) {
        throw std::runtime_error("flat transport candidate state changed unexpectedly");
    }
    const auto method = Fluid25DTransportStudyMethod::ReconstructedHydrostatic;
    const Json controls = run_control_suite(method, true);
    const Json rain_reference = run_uniform_incline_rain_reference();
    const bool checks_pass = controls.at("all_controls_pass").get<bool>() &&
                             rain_reference.at("accuracy_pass").get<bool>();
    return {
        {"schema", "cubey.fluid25d.transport_study.v3"},
        {"mode", "self_test"},
        {"diagnostic_success", true},
        {"candidate_acceptance_pass", checks_pass},
        {"reference_checks",
         {{"bernoulli_head_m", head},
          {"crest_depth_m", crest_depth},
          {"trough_depth_m", trough_depth},
          {"flat_control_pass", true},
          {"uniform_incline_rain_pass", rain_reference.at("accuracy_pass")},
          {"controls_pass", controls.at("all_controls_pass")}}},
        {"controls", controls},
        {"uniform_incline_rain_reference", rain_reference},
        {"full_numerical_matrix_run", false},
    };
}

[[nodiscard]] Json run_full_report() {
    Json candidate_runs = Json::array();
    Json legacy_runs = Json::array();
    bool all_execution_ok = true;
    bool all_conservation_pass = true;
    bool all_candidate_accuracy_pass = true;
    bool all_boundary_controls_pass = true;
    std::size_t candidate_success_count = 0U;
    Json boundary_controls = Json::array();

    for (const double dx : kCellSizes) {
        for (const double slope : kSlopes) {
            for (const double depth : kDepths) {
                const TransportCase parameters = make_matrix_case(dx, slope, depth);
                CandidateRun candidate;
                Json legacy;
                try {
                    candidate = run_candidate_incline(parameters);
                    legacy = run_legacy_incline(parameters);
                } catch (const std::exception& error) {
                    all_execution_ok = false;
                    all_conservation_pass = false;
                    all_candidate_accuracy_pass = false;
                    candidate_runs.push_back({{"case_id", parameters.id},
                                              {"execution_ok", false},
                                              {"error", error.what()}});
                    legacy_runs.push_back({{"case_id", parameters.id},
                                           {"execution_ok", false},
                                           {"error", error.what()}});
                    continue;
                }
                ++candidate_success_count;
                const bool candidate_health =
                    candidate.report.at("conservation_health_pass").get<bool>();
                const bool legacy_health = legacy.at("conservation_health_pass").get<bool>();
                const bool accuracy = candidate.report.at("accuracy_pass").get<bool>();
                all_conservation_pass = all_conservation_pass && candidate_health && legacy_health;
                all_candidate_accuracy_pass = all_candidate_accuracy_pass && accuracy;
                all_execution_ok = all_execution_ok && candidate_health && legacy_health;

                if (needs_expanded_domain_control(parameters)) {
                    try {
                        const auto expanded = run_candidate_incline(expanded_x_case(parameters));
                        const Json comparison =
                            compare_expanded_domain(candidate.snapshots, expanded.snapshots);
                        all_boundary_controls_pass =
                            all_boundary_controls_pass && comparison.at("pass").get<bool>();
                        candidate.report["expanded_domain_control"] = comparison;
                        boundary_controls.push_back(
                            {{"case_id", parameters.id},
                             {"pass", comparison.at("pass")},
                             {"sampled_cell_time_comparisons",
                              comparison.at("sampled_cell_time_comparisons")},
                             {"maximum_sample_cell_depth_delta_m",
                              comparison.at("maximum_sample_cell_depth_delta_m")},
                             {"maximum_sample_cell_velocity_delta_m_per_s",
                              comparison.at("maximum_sample_cell_velocity_delta_m_per_s")}});
                    } catch (const std::exception& error) {
                        all_boundary_controls_pass = false;
                        all_execution_ok = false;
                        candidate.report["expanded_domain_control"] = {{"pass", false},
                                                                       {"error", error.what()}};
                    }
                } else {
                    candidate.report["expanded_domain_control"] = nullptr;
                }
                candidate.report["paired_legacy_case_id"] = legacy.at("case_id");
                candidate_runs.push_back(std::move(candidate.report));
                legacy_runs.push_back(std::move(legacy));
            }
        }
    }

    Json curved_runs = Json::array();
    bool curved_pass = true;
    for (const double dx : kCellSizes) {
        try {
            Json curved = run_bernoulli_case(dx, 10.0);
            curved_pass = curved_pass && curved.at("accuracy_pass").get<bool>();
            curved_runs.push_back(std::move(curved));
        } catch (const std::exception& error) {
            all_execution_ok = false;
            curved_pass = false;
            curved_runs.push_back({{"cell_size_m", dx},
                                   {"execution_ok", false},
                                   {"accuracy_pass", false},
                                   {"error", error.what()}});
        }
    }
    Json rain_reference;
    Json controls;
    bool rain_reference_pass = false;
    bool controls_pass = false;
    try {
        rain_reference = run_uniform_incline_rain_reference();
        controls = run_control_suite(Fluid25DTransportStudyMethod::ReconstructedHydrostatic, true);
        rain_reference_pass = rain_reference.at("accuracy_pass").get<bool>();
        controls_pass = controls.at("all_controls_pass").get<bool>();
        all_conservation_pass =
            all_conservation_pass && rain_reference.at("conservation_health_pass").get<bool>();
        all_execution_ok = all_execution_ok && controls_pass;
    } catch (const std::exception& error) {
        all_execution_ok = false;
        all_conservation_pass = false;
        rain_reference = {
            {"execution_ok", false}, {"accuracy_pass", false}, {"error", error.what()}};
        controls = {{"all_controls_pass", false}, {"error", error.what()}};
    }
    const bool candidate_acceptance_pass =
        all_execution_ok && all_conservation_pass && all_candidate_accuracy_pass && curved_pass &&
        all_boundary_controls_pass && rain_reference_pass && controls_pass;
    return {
        {"schema", "cubey.fluid25d.transport_study.v3"},
        {"mode", "full_cpu_report"},
        {"method_scope", "isolated double-state CPU proof only; production oracle, GPU, defaults, "
                         "and product behavior unchanged"},
        {"diagnostic_success", all_execution_ok && all_conservation_pass},
        {"candidate_acceptance_pass", candidate_acceptance_pass},
        {"hard_gates",
         {{"depth_relative_error_max", kDepthRelativeTolerance},
          {"velocity_relative_error_max", kVelocityRelativeTolerance},
          {"integrated_face_transfer_relative_error_max", kFaceTransferRelativeTolerance},
          {"bernoulli_native_30m_is_hard_gate", true},
          {"expanded_domain_per_cell_depth_tolerance_m", kBoundaryDepthToleranceM},
          {"expanded_domain_per_cell_velocity_tolerance_m_per_s",
           kBoundaryVelocityToleranceMPerS}}},
        {"incline_contract",
         {{"domain_m", {{"width", kDomainWidthM}, {"height", kDomainHeightM}}},
          {"cell_sizes_m", kCellSizes},
          {"slopes", kSlopes},
          {"depths_m", kDepths},
          {"gravity_m_per_s2", kGravity},
          {"damping_per_s", kDamping},
          {"fixed_step_seconds", 2.0},
          {"substeps", kSubsteps},
          {"substep_seconds", 2.0 / kSubsteps},
          {"candidate_stage_cfl_target", Fluid25DTransportStudy::kTargetCfl},
          {"sample_times_s", kSampleTimes},
          {"sample_x_m", {kSampleXMinimumM, kSampleXMaximumM}},
          {"sample_y_m", {kSampleYMinimumM, kSampleYMaximumM}},
          {"boundary_mode", "closed_reflective_outflow_disabled"},
          {"candidate_face_metric",
           "sum actual SSPRK-weighted applied face transfer by public step; compare cumulative "
           "volume against depth*dx*integral(u dt)"}}},
        {"candidate_matrix",
         {{"run_count", candidate_runs.size()},
          {"successful_run_count", candidate_success_count},
          {"all_accuracy_gates_pass", all_candidate_accuracy_pass},
          {"runs", std::move(candidate_runs)}}},
        {"legacy_paired_matrix",
         {{"run_count", legacy_runs.size()},
          {"paired_fixed_step_seconds", 2.0},
          {"paired_substeps", kSubsteps},
          {"face_flux_not_observed", true},
          {"runs", std::move(legacy_runs)}}},
        {"expanded_domain_controls",
         {{"run_count", boundary_controls.size()},
          {"all_per_cell_comparisons_pass", all_boundary_controls_pass},
          {"controls", std::move(boundary_controls)}}},
        {"curved_bernoulli_gate",
         {{"run_count", curved_runs.size()},
          {"all_resolution_gates_pass", curved_pass},
          {"runs", std::move(curved_runs)}}},
        {"uniform_incline_rain_reference", std::move(rain_reference)},
        {"additional_controls", controls},
        {"all_additional_controls_pass", controls_pass},
        {"conservation_health_pass", all_conservation_pass},
    };
}

} // namespace
} // namespace cubey::projects::fluid::fluid_25d

int main(int argc, char** argv) {
    using namespace cubey::projects::fluid::fluid_25d;
    try {
        Json report;
        if (argc == 2 && std::string(argv[1]) == "--self-test") {
            report = run_self_test();
            std::cout << report.dump() << '\n';
            return report.at("candidate_acceptance_pass").get<bool>() ? 0 : 2;
        }
        if (argc == 2 && std::string(argv[1]) == "--probe") {
            report = run_probe();
            std::cout << report.dump() << '\n';
            if (!report.at("diagnostic_success").get<bool>()) {
                return 1;
            }
            return report.at("candidate_acceptance_pass").get<bool>() ? 0 : 2;
        }
        if (argc == 2 && std::string(argv[1]) == "--fallback-probe") {
            report = run_fallback_probe();
            std::cout << report.dump() << '\n';
            if (!report.at("diagnostic_success").get<bool>()) {
                return 1;
            }
            return report.at("fallback_probe_acceptance_pass").get<bool>() ? 0 : 2;
        }
        if (argc == 1) {
            report = run_full_report();
            std::cout << report.dump() << '\n';
            if (!report.at("diagnostic_success").get<bool>()) {
                return 1;
            }
            return report.at("candidate_acceptance_pass").get<bool>() ? 0 : 2;
        }
        throw std::invalid_argument(
            "usage: fluid_25d_transport_study_benchmark [--self-test|--probe|--fallback-probe]");
    } catch (const std::exception& error) {
        std::cout << Json{{"schema", "cubey.fluid25d.transport_study.v3"},
                          {"diagnostic_success", false},
                          {"error", error.what()}}
                         .dump()
                  << '\n';
        return 1;
    }
}
