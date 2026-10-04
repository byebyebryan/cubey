#include "fluid_25d_finite_volume_oracle.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace cubey::projects::fluid::fluid_25d {
namespace {

using Json = nlohmann::json;

constexpr double kDomainWidthM = 1920.0;
constexpr double kDomainHeightM = 240.0;
constexpr double kSampleXMinimumM = 480.0;
constexpr double kSampleXMaximumM = 1440.0;
constexpr double kSampleYMinimumM = 60.0;
constexpr double kSampleYMaximumM = 180.0;
constexpr double kBaseElevationM = 0.0;
constexpr double kGravityMPerS2 = 9.81;
constexpr double kDampingPerS = 0.15;
constexpr double kDepthRelativeTolerance = 0.01;
constexpr double kVelocityRelativeTolerance = 0.05;
constexpr double kZeroVelocityAbsoluteToleranceMPerS = 1.0e-6;
constexpr double kLedgerRelativeTolerance = 2.0e-6;
constexpr double kLedgerMinimumToleranceM3 = 1.0e-5;
constexpr double kFixedStepSeconds = 2.0;
constexpr std::uint32_t kSubsteps = 16U;
constexpr std::array<double, 5> kSampleTimesSeconds{2.0, 4.0, 6.0, 8.0, 10.0};
constexpr std::array<double, 3> kCellSizesM{30.0, 15.0, 7.5};
constexpr std::array<double, 4> kSlopes{0.0, 0.005, 0.05, 0.25};
constexpr std::array<double, 3> kInitialDepthsM{0.02, 0.2, 2.0};

struct TransportCase {
    std::string variant;
    double cell_size_m = 30.0;
    double slope = 0.0;
    double initial_depth_m = 0.2;
    double damping_per_s = kDampingPerS;
    double fixed_step_s = kFixedStepSeconds;
    std::uint32_t substeps = kSubsteps;
    double domain_width_m = kDomainWidthM;
    double domain_height_m = kDomainHeightM;
    double domain_origin_x_m = 0.0;
    double domain_origin_y_m = 0.0;
};

[[nodiscard]] double analytic_velocity_x(double slope, double damping_per_s, double time_s) {
    if (!std::isfinite(slope) || slope < 0.0 || !std::isfinite(damping_per_s) ||
        damping_per_s < 0.0 || !std::isfinite(time_s) || time_s < 0.0) {
        throw std::invalid_argument("analytic transport inputs must be finite and nonnegative");
    }
    if (damping_per_s == 0.0) {
        return kGravityMPerS2 * slope * time_s;
    }
    return (kGravityMPerS2 * slope / damping_per_s) * -std::expm1(-damping_per_s * time_s);
}

[[nodiscard]] double analytic_advective_travel_x(double slope, double damping_per_s,
                                                 double time_s) {
    if (!std::isfinite(slope) || slope < 0.0 || !std::isfinite(damping_per_s) ||
        damping_per_s < 0.0 || !std::isfinite(time_s) || time_s < 0.0) {
        throw std::invalid_argument("analytic transport inputs must be finite and nonnegative");
    }
    if (damping_per_s == 0.0) {
        return 0.5 * kGravityMPerS2 * slope * time_s * time_s;
    }
    const double acceleration_m_per_s2 = kGravityMPerS2 * slope;
    return (acceleration_m_per_s2 / damping_per_s) *
           (time_s + std::expm1(-damping_per_s * time_s) / damping_per_s);
}

[[nodiscard]] Json analytic_fixture(const TransportCase& parameters, double horizon_s) {
    if (!std::isfinite(parameters.cell_size_m) || parameters.cell_size_m <= 0.0 ||
        !std::isfinite(parameters.slope) || parameters.slope < 0.0 ||
        !std::isfinite(parameters.initial_depth_m) || parameters.initial_depth_m <= 0.0 ||
        !std::isfinite(parameters.damping_per_s) || parameters.damping_per_s < 0.0 ||
        !std::isfinite(parameters.domain_width_m) || parameters.domain_width_m <= 0.0 ||
        !std::isfinite(parameters.domain_height_m) || parameters.domain_height_m <= 0.0 ||
        !std::isfinite(parameters.domain_origin_x_m) ||
        !std::isfinite(parameters.domain_origin_y_m) || !std::isfinite(horizon_s) ||
        horizon_s <= 0.0) {
        throw std::invalid_argument("analytic transport fixture parameters are invalid");
    }

    const double wave_speed_m_per_s = std::sqrt(kGravityMPerS2 * parameters.initial_depth_m);
    const double max_signal_travel_x_m =
        analytic_advective_travel_x(parameters.slope, parameters.damping_per_s, horizon_s) +
        (wave_speed_m_per_s * horizon_s);
    const double max_signal_travel_y_m = wave_speed_m_per_s * horizon_s;
    const double x_clearance_m =
        std::min(kSampleXMinimumM - parameters.domain_origin_x_m,
                 parameters.domain_origin_x_m + parameters.domain_width_m - kSampleXMaximumM);
    const double y_clearance_m =
        std::min(kSampleYMinimumM - parameters.domain_origin_y_m,
                 parameters.domain_origin_y_m + parameters.domain_height_m - kSampleYMaximumM);
    const bool analytic_pde_isolation_pass =
        max_signal_travel_x_m < x_clearance_m && max_signal_travel_y_m < y_clearance_m;
    if (x_clearance_m <= 0.0 || y_clearance_m <= 0.0 || !analytic_pde_isolation_pass) {
        throw std::runtime_error("analytic travel bound reaches the benchmark sample region");
    }

    return {
        {"gravity_m_per_s2", kGravityMPerS2},
        {"horizon_s", horizon_s},
        {"analytic_wave_speed_m_per_s", wave_speed_m_per_s},
        {"maximum_signal_travel_x_m", max_signal_travel_x_m},
        {"maximum_signal_travel_y_m", max_signal_travel_y_m},
        {"minimum_sample_clearance_x_m", x_clearance_m},
        {"minimum_sample_clearance_y_m", y_clearance_m},
        {"analytic_pde_boundary_isolation_pass", analytic_pde_isolation_pass},
        {"scope", "analytic_pde_travel_bound_only; discrete stencil and numerical boundary "
                  "contamination are assessed by the doubled-domain comparison"},
    };
}

[[nodiscard]] Fluid25DConfig make_config(const TransportCase& parameters, std::uint32_t width,
                                         std::uint32_t height) {
    if (!std::isfinite(parameters.fixed_step_s) || parameters.fixed_step_s <= 0.0 ||
        parameters.substeps == 0U || parameters.substeps > 64U) {
        throw std::invalid_argument("transport timestep parameters are invalid");
    }
    Fluid25DConfig config;
    config.grid_width = width;
    config.grid_height = height;
    config.cell_size_m = static_cast<float>(parameters.cell_size_m);
    config.fixed_delta_seconds = static_cast<float>(parameters.fixed_step_s);
    config.simulation_substeps = parameters.substeps;
    config.gravity_m_per_s2 = static_cast<float>(kGravityMPerS2);
    config.flow_damping_per_second = static_cast<float>(parameters.damping_per_s);
    config.scenario = Fluid25DScenario::DryBed;
    config.solver = Fluid25DSolver::FiniteVolume;
    validate_fluid_25d_config(config);
    return config;
}

[[nodiscard]] Fluid25DScenarioData make_scenario(const TransportCase& parameters,
                                                 std::uint32_t width, std::uint32_t height) {
    Fluid25DScenarioData scenario;
    scenario.width = width;
    scenario.height = height;
    scenario.cell_size_m = static_cast<float>(parameters.cell_size_m);
    const std::size_t cell_count = static_cast<std::size_t>(width) * height;
    scenario.terrain_height_m.resize(cell_count);
    scenario.initial_water_depth_m.assign(cell_count,
                                          static_cast<float>(parameters.initial_depth_m));
    scenario.source_depth_rate_m_per_s.assign(cell_count, 0.0F);
    scenario.sink_depth_rate_m_per_s.assign(cell_count, 0.0F);
    scenario.boundary_outflow_face_mask.assign(cell_count, 0U);
    for (std::uint32_t y = 0U; y < height; ++y) {
        for (std::uint32_t x = 0U; x < width; ++x) {
            const double physical_x_m = parameters.domain_origin_x_m +
                                        (static_cast<double>(x) + 0.5) * parameters.cell_size_m;
            const std::size_t index = fluid_25d_scenario_index(width, height, x, y);
            scenario.terrain_height_m[index] =
                static_cast<float>(kBaseElevationM - (parameters.slope * physical_x_m));
        }
    }
    return scenario;
}

struct SampleMeasurement {
    std::uint64_t sample_cell_count = 0U;
    double mean_depth_m = 0.0;
    double mean_velocity_x_m_per_s = 0.0;
    double mean_speed_m_per_s = 0.0;
    double max_cell_depth_relative_error = 0.0;
    double max_cell_velocity_absolute_error_m_per_s = 0.0;
    double max_cell_velocity_relative_error = 0.0;
};

[[nodiscard]] SampleMeasurement measure_sample(const Fluid25DFiniteVolumeOracle& oracle,
                                               const TransportCase& parameters, double time_s) {
    const auto& config = oracle.config();
    const double analytic_velocity =
        analytic_velocity_x(parameters.slope, parameters.damping_per_s, time_s);
    SampleMeasurement measurement;
    std::size_t sample_count = 0U;
    for (std::uint32_t y = 0U; y < config.grid_height; ++y) {
        const double physical_y_m =
            parameters.domain_origin_y_m + (static_cast<double>(y) + 0.5) * config.cell_size_m;
        if (physical_y_m < kSampleYMinimumM || physical_y_m > kSampleYMaximumM) {
            continue;
        }
        for (std::uint32_t x = 0U; x < config.grid_width; ++x) {
            const double physical_x_m = (static_cast<double>(x) + 0.5) * config.cell_size_m;
            if (physical_x_m < kSampleXMinimumM || physical_x_m > kSampleXMaximumM) {
                continue;
            }
            const std::size_t index =
                fluid_25d_scenario_index(config.grid_width, config.grid_height, x, y);
            const double depth_m = oracle.water_depth_m()[index];
            const double velocity_x_m_per_s = oracle.velocity_m_per_s()[index].x_m_per_s;
            const double velocity_y_m_per_s = oracle.velocity_m_per_s()[index].y_m_per_s;
            const double speed_m_per_s = std::hypot(velocity_x_m_per_s, velocity_y_m_per_s);
            if (!std::isfinite(depth_m) || !std::isfinite(velocity_x_m_per_s) ||
                !std::isfinite(velocity_y_m_per_s) || !std::isfinite(speed_m_per_s)) {
                throw std::runtime_error("transport benchmark measured a nonfinite sample");
            }
            measurement.mean_depth_m += depth_m;
            measurement.mean_velocity_x_m_per_s += velocity_x_m_per_s;
            measurement.mean_speed_m_per_s += speed_m_per_s;
            measurement.max_cell_depth_relative_error = std::max(
                measurement.max_cell_depth_relative_error,
                std::abs(depth_m - parameters.initial_depth_m) / parameters.initial_depth_m);
            const double velocity_error_m_per_s = std::abs(velocity_x_m_per_s - analytic_velocity);
            measurement.max_cell_velocity_absolute_error_m_per_s = std::max(
                measurement.max_cell_velocity_absolute_error_m_per_s, velocity_error_m_per_s);
            if (analytic_velocity > 0.0) {
                measurement.max_cell_velocity_relative_error =
                    std::max(measurement.max_cell_velocity_relative_error,
                             velocity_error_m_per_s / analytic_velocity);
            }
            ++sample_count;
        }
    }
    if (sample_count == 0U) {
        throw std::runtime_error("transport benchmark sample region has no cells");
    }
    measurement.sample_cell_count = static_cast<std::uint64_t>(sample_count);
    const double inverse_count = 1.0 / static_cast<double>(sample_count);
    measurement.mean_depth_m *= inverse_count;
    measurement.mean_velocity_x_m_per_s *= inverse_count;
    measurement.mean_speed_m_per_s *= inverse_count;
    return measurement;
}

[[nodiscard]] Json run_case(const TransportCase& parameters) {
    if (!std::isfinite(parameters.cell_size_m) || parameters.cell_size_m <= 0.0 ||
        !std::isfinite(parameters.slope) || parameters.slope < 0.0 ||
        !std::isfinite(parameters.initial_depth_m) || parameters.initial_depth_m <= 0.0 ||
        !std::isfinite(parameters.damping_per_s) || parameters.damping_per_s < 0.0 ||
        !std::isfinite(parameters.domain_width_m) || parameters.domain_width_m <= 0.0 ||
        !std::isfinite(parameters.domain_height_m) || parameters.domain_height_m <= 0.0 ||
        !std::isfinite(parameters.domain_origin_x_m) ||
        !std::isfinite(parameters.domain_origin_y_m)) {
        throw std::invalid_argument("transport benchmark case contains invalid values");
    }
    const auto cells_for_length = [cell_size_m = parameters.cell_size_m](double length_m) {
        const double cells = length_m / cell_size_m;
        const auto rounded = static_cast<std::uint32_t>(std::llround(cells));
        if (rounded < 2U ||
            std::abs(static_cast<double>(rounded) * cell_size_m - length_m) > 1.0e-9) {
            throw std::invalid_argument("cell size must divide the physical benchmark domain");
        }
        return rounded;
    };
    const std::uint32_t width = cells_for_length(parameters.domain_width_m);
    const std::uint32_t height = cells_for_length(parameters.domain_height_m);
    const Fluid25DConfig config = make_config(parameters, width, height);
    Fluid25DFiniteVolumeOracle oracle(config, make_scenario(parameters, width, height));
    const double initial_volume_m3 = oracle.total_water_volume_m3();
    const double cell_area_m2 = parameters.cell_size_m * parameters.cell_size_m;
    const std::size_t cell_count = static_cast<std::size_t>(width) * height;
    const double expected_initial_volume_m3 =
        parameters.initial_depth_m * cell_area_m2 * static_cast<double>(cell_count);
    if (!std::isfinite(initial_volume_m3) ||
        std::abs(initial_volume_m3 - expected_initial_volume_m3) >
            std::max(1.0e-7, expected_initial_volume_m3 * 1.0e-7)) {
        throw std::runtime_error("transport benchmark initial volume fixture is invalid");
    }

    const Json analytic = analytic_fixture(parameters, kSampleTimesSeconds.back());
    const std::uint32_t fixed_steps_per_sample =
        static_cast<std::uint32_t>(std::llround(2.0 / parameters.fixed_step_s));
    if (fixed_steps_per_sample == 0U ||
        std::abs(parameters.fixed_step_s * fixed_steps_per_sample - 2.0) > 1.0e-9) {
        throw std::invalid_argument("fixed step must divide the 2-second sample interval");
    }

    Json samples = Json::array();
    double source_volume_m3 = 0.0;
    double sink_volume_m3 = 0.0;
    double boundary_outflow_volume_m3 = 0.0;
    double max_abs_step_ledger_residual_m3 = 0.0;
    bool step_ledger_pass = true;
    double max_cfl = 0.0;
    double max_depth_relative_error = 0.0;
    double max_velocity_relative_error = 0.0;
    double max_velocity_absolute_error_m_per_s = 0.0;
    bool depth_accuracy_pass = true;
    bool velocity_accuracy_pass = true;

    for (std::size_t sample_index = 0; sample_index < kSampleTimesSeconds.size(); ++sample_index) {
        for (std::uint32_t step_index = 0U; step_index < fixed_steps_per_sample; ++step_index) {
            const Fluid25DStepLedger ledger = oracle.step();
            const double step_residual_m3 = ledger.conservation_error_m3();
            const float step_cfl = oracle.last_cfl_number();
            if (!std::isfinite(step_residual_m3) || !std::isfinite(ledger.volume_after_m3) ||
                !std::isfinite(step_cfl) || step_cfl < 0.0F ||
                step_cfl > Fluid25DFiniteVolumeOracle::kTargetCfl) {
                throw std::runtime_error("transport benchmark produced invalid ledger or CFL data");
            }
            source_volume_m3 += ledger.source_volume_m3;
            sink_volume_m3 += ledger.sink_volume_m3;
            boundary_outflow_volume_m3 += ledger.boundary_outflow_volume_m3;
            max_abs_step_ledger_residual_m3 =
                std::max(max_abs_step_ledger_residual_m3, std::abs(step_residual_m3));
            const double step_ledger_tolerance_m3 = std::max(
                kLedgerMinimumToleranceM3, ledger.volume_after_m3 * kLedgerRelativeTolerance);
            step_ledger_pass =
                step_ledger_pass && std::abs(step_residual_m3) <= step_ledger_tolerance_m3;
            max_cfl = std::max(max_cfl, static_cast<double>(step_cfl));
        }

        const double time_s = static_cast<double>(sample_index + 1U) * 2.0;
        const SampleMeasurement measured = measure_sample(oracle, parameters, time_s);
        const double expected_velocity_m_per_s =
            analytic_velocity_x(parameters.slope, parameters.damping_per_s, time_s);
        const double depth_absolute_error_m =
            std::abs(measured.mean_depth_m - parameters.initial_depth_m);
        const double depth_relative_error = depth_absolute_error_m / parameters.initial_depth_m;
        const double velocity_absolute_error_m_per_s =
            std::abs(measured.mean_velocity_x_m_per_s - expected_velocity_m_per_s);
        const bool sample_depth_accuracy_pass =
            measured.max_cell_depth_relative_error <= kDepthRelativeTolerance;
        const bool sample_velocity_accuracy_pass =
            expected_velocity_m_per_s > 0.0
                ? measured.max_cell_velocity_relative_error <= kVelocityRelativeTolerance
                : measured.max_cell_velocity_absolute_error_m_per_s <=
                      kZeroVelocityAbsoluteToleranceMPerS;
        depth_accuracy_pass = depth_accuracy_pass && sample_depth_accuracy_pass;
        velocity_accuracy_pass = velocity_accuracy_pass && sample_velocity_accuracy_pass;
        max_depth_relative_error =
            std::max(max_depth_relative_error, measured.max_cell_depth_relative_error);
        max_velocity_relative_error =
            std::max(max_velocity_relative_error, measured.max_cell_velocity_relative_error);
        max_velocity_absolute_error_m_per_s = std::max(
            max_velocity_absolute_error_m_per_s, measured.max_cell_velocity_absolute_error_m_per_s);

        Json sample{
            {"time_s", time_s},
            {"sample_cell_count", measured.sample_cell_count},
            {"measured_mean_depth_m", measured.mean_depth_m},
            {"analytic_depth_m", parameters.initial_depth_m},
            {"depth_absolute_error_m", depth_absolute_error_m},
            {"depth_relative_error", depth_relative_error},
            {"maximum_cell_depth_relative_error", measured.max_cell_depth_relative_error},
            {"measured_mean_velocity_x_m_per_s", measured.mean_velocity_x_m_per_s},
            {"measured_mean_speed_m_per_s", measured.mean_speed_m_per_s},
            {"analytic_velocity_x_m_per_s", expected_velocity_m_per_s},
            {"velocity_absolute_error_m_per_s", velocity_absolute_error_m_per_s},
            {"velocity_relative_error",
             expected_velocity_m_per_s > 0.0
                 ? Json(velocity_absolute_error_m_per_s / expected_velocity_m_per_s)
                 : Json(nullptr)},
            {"maximum_cell_velocity_absolute_error_m_per_s",
             measured.max_cell_velocity_absolute_error_m_per_s},
            {"maximum_cell_velocity_relative_error",
             expected_velocity_m_per_s > 0.0 ? Json(measured.max_cell_velocity_relative_error)
                                             : Json(nullptr)},
            {"depth_accuracy_pass", sample_depth_accuracy_pass},
            {"velocity_accuracy_pass", sample_velocity_accuracy_pass},
        };
        samples.push_back(std::move(sample));
    }

    const double final_volume_m3 = oracle.total_water_volume_m3();
    const double net_mass_balance_residual_m3 = final_volume_m3 - initial_volume_m3 -
                                                source_volume_m3 + sink_volume_m3 +
                                                boundary_outflow_volume_m3;
    const double ledger_tolerance_m3 =
        std::max(kLedgerMinimumToleranceM3, final_volume_m3 * kLedgerRelativeTolerance);
    if (!std::isfinite(final_volume_m3) || !std::isfinite(net_mass_balance_residual_m3) ||
        !std::isfinite(source_volume_m3) || !std::isfinite(sink_volume_m3) ||
        !std::isfinite(boundary_outflow_volume_m3) ||
        !std::isfinite(max_abs_step_ledger_residual_m3)) {
        throw std::runtime_error("transport benchmark produced a nonfinite mass ledger");
    }
    const bool mass_ledger_pass = step_ledger_pass &&
                                  std::abs(net_mass_balance_residual_m3) <= ledger_tolerance_m3 &&
                                  std::abs(source_volume_m3) <= ledger_tolerance_m3 &&
                                  std::abs(sink_volume_m3) <= ledger_tolerance_m3 &&
                                  std::abs(boundary_outflow_volume_m3) <= ledger_tolerance_m3 &&
                                  max_abs_step_ledger_residual_m3 <= ledger_tolerance_m3;

    const double ratio_slope_dx_over_depth =
        parameters.slope * parameters.cell_size_m / parameters.initial_depth_m;
    return {
        {"case_id", parameters.variant + "-dx" + std::to_string(parameters.cell_size_m) + "-s" +
                        std::to_string(parameters.slope) + "-h" +
                        std::to_string(parameters.initial_depth_m) + "-gamma" +
                        std::to_string(parameters.damping_per_s)},
        {"variant", parameters.variant},
        {"domain_m",
         {{"width", parameters.domain_width_m},
          {"height", parameters.domain_height_m},
          {"origin_x", parameters.domain_origin_x_m},
          {"origin_y", parameters.domain_origin_y_m}}},
        {"grid", {{"width_cells", width}, {"height_cells", height}}},
        {"cell_size_m", parameters.cell_size_m},
        {"terrain_slope", parameters.slope},
        {"initial_depth_m", parameters.initial_depth_m},
        {"damping_per_s", parameters.damping_per_s},
        {"gravity_m_per_s2", kGravityMPerS2},
        {"fixed_step_seconds", parameters.fixed_step_s},
        {"substeps", parameters.substeps},
        {"substep_seconds", parameters.fixed_step_s / parameters.substeps},
        {"slope_dx_over_depth", ratio_slope_dx_over_depth},
        {"analytic_pde_boundary_isolation", analytic},
        {"mass_ledger",
         {{"initial_volume_m3", initial_volume_m3},
          {"final_volume_m3", final_volume_m3},
          {"source_volume_m3", source_volume_m3},
          {"sink_volume_m3", sink_volume_m3},
          {"boundary_outflow_volume_m3", boundary_outflow_volume_m3},
          {"net_mass_balance_residual_m3", net_mass_balance_residual_m3},
          {"maximum_absolute_step_ledger_residual_m3", max_abs_step_ledger_residual_m3},
          {"ledger_tolerance_m3", ledger_tolerance_m3},
          {"closed_domain_check_pass", mass_ledger_pass}}},
        {"max_cfl", max_cfl},
        {"maximum_cell_depth_relative_error", max_depth_relative_error},
        {"maximum_cell_velocity_relative_error",
         parameters.slope > 0.0 ? Json(max_velocity_relative_error) : Json(nullptr)},
        {"maximum_cell_velocity_absolute_error_m_per_s", max_velocity_absolute_error_m_per_s},
        {"depth_accuracy_pass", depth_accuracy_pass},
        {"velocity_accuracy_pass", velocity_accuracy_pass},
        {"analytic_accuracy_pass", depth_accuracy_pass && velocity_accuracy_pass},
        {"execution_ok", true},
        {"conservation_health_pass", mass_ledger_pass},
        {"conservation_error", mass_ledger_pass
                                   ? Json(nullptr)
                                   : Json("closed-domain mass ledger residual exceeded the "
                                          "reported source-regression tolerance; inspect all "
                                          "ledger fields in this row")},
        {"error", nullptr},
        {"samples", std::move(samples)},
    };
}

[[nodiscard]] Json make_matrix() {
    Json runs = Json::array();
    Json selected_domain_baseline;
    Json selected_timestep_baseline;
    for (const double cell_size_m : kCellSizesM) {
        for (const double slope : kSlopes) {
            for (const double initial_depth_m : kInitialDepthsM) {
                const TransportCase parameters{
                    .variant = "baseline",
                    .cell_size_m = cell_size_m,
                    .slope = slope,
                    .initial_depth_m = initial_depth_m,
                    .damping_per_s = kDampingPerS,
                    .fixed_step_s = kFixedStepSeconds,
                    .substeps = kSubsteps,
                };
                Json row;
                try {
                    row = run_case(parameters);
                } catch (const std::exception& error) {
                    row = {
                        {"case_id", "baseline-dx" + std::to_string(cell_size_m) + "-s" +
                                        std::to_string(slope) + "-h" +
                                        std::to_string(initial_depth_m)},
                        {"variant", parameters.variant},
                        {"cell_size_m", cell_size_m},
                        {"terrain_slope", slope},
                        {"initial_depth_m", initial_depth_m},
                        {"damping_per_s", kDampingPerS},
                        {"fixed_step_seconds", kFixedStepSeconds},
                        {"substeps", kSubsteps},
                        {"execution_ok", false},
                        {"error", error.what()},
                    };
                }
                if (cell_size_m == 30.0 && slope == 0.25 && initial_depth_m == 0.2) {
                    selected_domain_baseline = row;
                }
                if (cell_size_m == 15.0 && slope == 0.05 && initial_depth_m == 0.2) {
                    selected_timestep_baseline = row;
                }
                runs.push_back(std::move(row));
            }
        }
    }

    const TransportCase zero_damping_parameters{
        .variant = "zero-damping-crosscheck",
        .cell_size_m = 15.0,
        .slope = 0.005,
        .initial_depth_m = 0.2,
        .damping_per_s = 0.0,
        .fixed_step_s = kFixedStepSeconds,
        .substeps = kSubsteps,
    };
    Json zero_damping_row;
    try {
        zero_damping_row = run_case(zero_damping_parameters);
    } catch (const std::exception& error) {
        zero_damping_row = {
            {"variant", zero_damping_parameters.variant},
            {"cell_size_m", zero_damping_parameters.cell_size_m},
            {"terrain_slope", zero_damping_parameters.slope},
            {"initial_depth_m", zero_damping_parameters.initial_depth_m},
            {"damping_per_s", zero_damping_parameters.damping_per_s},
            {"fixed_step_seconds", zero_damping_parameters.fixed_step_s},
            {"substeps", zero_damping_parameters.substeps},
            {"execution_ok", false},
            {"error", error.what()},
        };
    }
    runs.push_back(std::move(zero_damping_row));

    const TransportCase half_timestep_parameters{
        .variant = "half-timestep-crosscheck",
        .cell_size_m = 15.0,
        .slope = 0.05,
        .initial_depth_m = 0.2,
        .damping_per_s = kDampingPerS,
        .fixed_step_s = 1.0,
        .substeps = 16U,
    };
    Json half_timestep_row;
    try {
        half_timestep_row = run_case(half_timestep_parameters);
    } catch (const std::exception& error) {
        half_timestep_row = {
            {"variant", half_timestep_parameters.variant},
            {"cell_size_m", half_timestep_parameters.cell_size_m},
            {"terrain_slope", half_timestep_parameters.slope},
            {"initial_depth_m", half_timestep_parameters.initial_depth_m},
            {"damping_per_s", half_timestep_parameters.damping_per_s},
            {"fixed_step_seconds", half_timestep_parameters.fixed_step_s},
            {"substeps", half_timestep_parameters.substeps},
            {"execution_ok", false},
            {"error", error.what()},
        };
    }
    if (half_timestep_row.value("execution_ok", false) &&
        selected_timestep_baseline.value("execution_ok", false)) {
        const Json& baseline_samples = selected_timestep_baseline.at("samples");
        const Json& candidate_samples = half_timestep_row.at("samples");
        Json differences = Json::array();
        for (std::size_t index = 0U; index < baseline_samples.size(); ++index) {
            const double baseline_depth =
                baseline_samples.at(index).at("measured_mean_depth_m").get<double>();
            const double candidate_depth =
                candidate_samples.at(index).at("measured_mean_depth_m").get<double>();
            const double baseline_velocity =
                baseline_samples.at(index).at("measured_mean_velocity_x_m_per_s").get<double>();
            const double candidate_velocity =
                candidate_samples.at(index).at("measured_mean_velocity_x_m_per_s").get<double>();
            differences.push_back({
                {"time_s", baseline_samples.at(index).at("time_s")},
                {"mean_depth_delta_m", candidate_depth - baseline_depth},
                {"mean_velocity_x_delta_m_per_s", candidate_velocity - baseline_velocity},
            });
        }
        half_timestep_row["same_case_baseline_comparison"] = {
            {"baseline_case_id", selected_timestep_baseline.at("case_id")},
            {"baseline_substep_seconds", kFixedStepSeconds / kSubsteps},
            {"candidate_substep_seconds",
             half_timestep_parameters.fixed_step_s / half_timestep_parameters.substeps},
            {"candidate_substep_is_half_baseline", true},
            {"sample_mean_field_differences", std::move(differences)},
        };
    } else {
        half_timestep_row["same_case_baseline_comparison"] = {
            {"available", false},
            {"reason", "baseline or timestep-halving execution failed; inspect both run rows"},
        };
    }
    runs.push_back(std::move(half_timestep_row));

    const TransportCase doubled_domain_parameters{
        .variant = "doubled-domain-control",
        .cell_size_m = 30.0,
        .slope = 0.25,
        .initial_depth_m = 0.2,
        .damping_per_s = kDampingPerS,
        .fixed_step_s = kFixedStepSeconds,
        .substeps = kSubsteps,
        .domain_width_m = 2.0 * kDomainWidthM,
        .domain_height_m = 2.0 * kDomainHeightM,
        .domain_origin_x_m = -0.5 * kDomainWidthM,
        .domain_origin_y_m = -0.5 * kDomainHeightM,
    };
    Json doubled_domain_row;
    try {
        doubled_domain_row = run_case(doubled_domain_parameters);
    } catch (const std::exception& error) {
        doubled_domain_row = {
            {"variant", doubled_domain_parameters.variant},
            {"cell_size_m", doubled_domain_parameters.cell_size_m},
            {"terrain_slope", doubled_domain_parameters.slope},
            {"initial_depth_m", doubled_domain_parameters.initial_depth_m},
            {"damping_per_s", doubled_domain_parameters.damping_per_s},
            {"fixed_step_seconds", doubled_domain_parameters.fixed_step_s},
            {"substeps", doubled_domain_parameters.substeps},
            {"domain_width_m", doubled_domain_parameters.domain_width_m},
            {"domain_height_m", doubled_domain_parameters.domain_height_m},
            {"execution_ok", false},
            {"error", error.what()},
        };
    }
    if (doubled_domain_row.value("execution_ok", false) &&
        selected_domain_baseline.value("execution_ok", false)) {
        const Json& baseline_samples = selected_domain_baseline.at("samples");
        const Json& candidate_samples = doubled_domain_row.at("samples");
        Json differences = Json::array();
        for (std::size_t index = 0U; index < baseline_samples.size(); ++index) {
            const double baseline_depth =
                baseline_samples.at(index).at("measured_mean_depth_m").get<double>();
            const double candidate_depth =
                candidate_samples.at(index).at("measured_mean_depth_m").get<double>();
            const double baseline_velocity =
                baseline_samples.at(index).at("measured_mean_velocity_x_m_per_s").get<double>();
            const double candidate_velocity =
                candidate_samples.at(index).at("measured_mean_velocity_x_m_per_s").get<double>();
            differences.push_back({
                {"time_s", baseline_samples.at(index).at("time_s")},
                {"baseline_mean_depth_m", baseline_depth},
                {"doubled_domain_mean_depth_m", candidate_depth},
                {"mean_depth_delta_m", candidate_depth - baseline_depth},
                {"baseline_mean_velocity_x_m_per_s", baseline_velocity},
                {"doubled_domain_mean_velocity_x_m_per_s", candidate_velocity},
                {"mean_velocity_x_delta_m_per_s", candidate_velocity - baseline_velocity},
            });
        }
        doubled_domain_row["same_physical_central_sample_comparison"] = {
            {"baseline_case_id", selected_domain_baseline.at("case_id")},
            {"domain_width_doubled", true},
            {"domain_height_doubled", true},
            {"sample_x_m", {kSampleXMinimumM, kSampleXMaximumM}},
            {"sample_y_m", {kSampleYMinimumM, kSampleYMaximumM}},
            {"sample_mean_field_differences", std::move(differences)},
        };
    } else {
        doubled_domain_row["same_physical_central_sample_comparison"] = {
            {"available", false},
            {"reason", "baseline or doubled-domain execution failed; inspect both run rows"},
        };
    }
    runs.push_back(std::move(doubled_domain_row));

    bool diagnostic_success = true;
    bool conservation_health_pass = true;
    bool depth_accuracy_pass = true;
    bool velocity_accuracy_pass = true;
    std::size_t successful_run_count = 0U;
    for (const Json& run : runs) {
        if (!run.value("execution_ok", false)) {
            diagnostic_success = false;
            conservation_health_pass = false;
            continue;
        }
        conservation_health_pass =
            conservation_health_pass && run.at("conservation_health_pass").get<bool>();
        ++successful_run_count;
        depth_accuracy_pass = depth_accuracy_pass && run.at("depth_accuracy_pass").get<bool>();
        velocity_accuracy_pass =
            velocity_accuracy_pass && run.at("velocity_accuracy_pass").get<bool>();
    }
    return {
        {"schema", "cubey.fluid25d.analytic_transport_benchmark.v1"},
        {"mode", "baseline_matrix"},
        {"comparison", "CPU finite-volume oracle against independent closed-form gravity and "
                       "linear-damping transport"},
        {"diagnostic_success", diagnostic_success},
        {"conservation_health_pass_all_successful_runs", conservation_health_pass},
        {"analytic_accuracy_thresholds",
         {{"depth_relative_error_max", kDepthRelativeTolerance},
          {"velocity_relative_error_max", kVelocityRelativeTolerance},
          {"zero_analytic_velocity_absolute_error_max_m_per_s",
           kZeroVelocityAbsoluteToleranceMPerS}}},
        {"analytic_accuracy",
         {{"evaluated_run_count", successful_run_count},
          {"depth_pass_all_successful_runs", depth_accuracy_pass},
          {"velocity_pass_all_successful_runs", velocity_accuracy_pass},
          {"pass_all_successful_runs", depth_accuracy_pass && velocity_accuracy_pass}}},
        {"contract",
         {{"physical_domain_m", {{"width", kDomainWidthM}, {"height", kDomainHeightM}}},
          {"cell_sizes_m", kCellSizesM},
          {"slopes", kSlopes},
          {"initial_depths_m", kInitialDepthsM},
          {"baseline_damping_per_s", kDampingPerS},
          {"baseline_fixed_step_seconds", kFixedStepSeconds},
          {"baseline_substeps", kSubsteps},
          {"halved_substep_crosscheck",
           {{"fixed_step_seconds", 1.0}, {"substeps", 16U}, {"substep_seconds", 0.0625}}},
          {"doubled_domain_control",
           {{"width_m", 2.0 * kDomainWidthM},
            {"height_m", 2.0 * kDomainHeightM},
            {"origin_x_m", -0.5 * kDomainWidthM},
            {"origin_y_m", -0.5 * kDomainHeightM}}},
          {"sample_times_s", kSampleTimesSeconds},
          {"sample_x_m", {kSampleXMinimumM, kSampleXMaximumM}},
          {"sample_y_m", {kSampleYMinimumM, kSampleYMaximumM}},
          {"rainfall_source_sink", "disabled"},
          {"boundary_mode", "closed_reflective_outflow_disabled"},
          {"transport_ratio", "slope*dx/initial_depth"},
          {"accuracy_is_diagnostic_only", true},
          {"matrix_execution_failures_are_recorded_per_row", true}}},
        {"run_count", runs.size()},
        {"runs", std::move(runs)},
    };
}

[[nodiscard]] Json run_self_test() {
    constexpr double expected_zero_damping_velocity_m_per_s = 0.981;
    constexpr double expected_damped_velocity_m_per_s = 0.847524;
    const double zero_damping_velocity = analytic_velocity_x(0.005, 0.0, 20.0);
    const double damped_velocity = analytic_velocity_x(0.05, 0.15, 2.0);
    if (std::abs(zero_damping_velocity - expected_zero_damping_velocity_m_per_s) > 1.0e-12 ||
        std::abs(damped_velocity - expected_damped_velocity_m_per_s) > 2.0e-6 ||
        analytic_velocity_x(0.0, 0.15, 10.0) != 0.0) {
        throw std::runtime_error("analytic transport reference sanity check failed");
    }
    const Json flat_run = run_case({
        .variant = "self-test-flat-control",
        .cell_size_m = 30.0,
        .slope = 0.0,
        .initial_depth_m = 0.2,
        .damping_per_s = kDampingPerS,
        .fixed_step_s = kFixedStepSeconds,
        .substeps = kSubsteps,
    });
    if (!flat_run.at("depth_accuracy_pass").get<bool>() ||
        !flat_run.at("velocity_accuracy_pass").get<bool>() ||
        !flat_run.at("conservation_health_pass").get<bool>() ||
        !flat_run.at("mass_ledger").at("net_mass_balance_residual_m3").is_number()) {
        throw std::runtime_error("flat analytic control did not preserve its exact reference");
    }
    return {
        {"schema", "cubey.fluid25d.analytic_transport_benchmark.v1"},
        {"mode", "self_test"},
        {"diagnostic_success", true},
        {"conservation_health_pass_all_runs", true},
        {"reference_sanity",
         {{"zero_damping_velocity_m_per_s", zero_damping_velocity},
          {"expected_zero_damping_velocity_m_per_s", expected_zero_damping_velocity_m_per_s},
          {"damped_velocity_m_per_s", damped_velocity},
          {"expected_damped_velocity_m_per_s", expected_damped_velocity_m_per_s},
          {"flat_control_pass", true}}},
        {"analytic_accuracy",
         {{"depth_pass_all_runs", true},
          {"velocity_pass_all_runs", true},
          {"pass_all_runs", true}}},
        {"run_count", 1},
        {"runs", Json::array({flat_run})},
    };
}

} // namespace
} // namespace cubey::projects::fluid::fluid_25d

int main(int argc, char** argv) {
    using namespace cubey::projects::fluid::fluid_25d;
    try {
        if (argc == 1) {
            const Json result = make_matrix();
            std::cout << result.dump() << '\n';
            return result.at("diagnostic_success").get<bool>() &&
                           result.at("conservation_health_pass_all_successful_runs").get<bool>()
                       ? 0
                       : 1;
        }
        if (argc == 2 && std::string(argv[1]) == "--self-test") {
            std::cout << run_self_test().dump() << '\n';
            return 0;
        }
        throw std::invalid_argument("usage: fluid_25d_transport_benchmark [--self-test]");
    } catch (const std::exception& error) {
        std::cout << Json{
                         {"schema", "cubey.fluid25d.analytic_transport_benchmark.v1"},
                         {"diagnostic_success", false},
                         {"error", error.what()},
                     }
                         .dump()
                  << '\n';
        return 1;
    }
}
