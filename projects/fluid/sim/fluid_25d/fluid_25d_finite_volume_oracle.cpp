#include "fluid_25d_finite_volume_oracle.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace cubey::projects::fluid::fluid_25d {
namespace {

// This is deliberately much smaller than the configured wet/dry threshold:
// it only absorbs a final float rounding residue after a CFL-valid update.
constexpr double kDepthRoundingResidueM = 1.0e-7;
constexpr double kTracerRoundingResidueM = 1.0e-7;

struct ConservedState {
    double depth_m = 0.0;
    double momentum_x_m2_per_s = 0.0;
    double momentum_y_m2_per_s = 0.0;
};

struct HydrostaticFaceFlux {
    ConservedState homogeneous{};
    ConservedState left_pressure_correction{};
    ConservedState right_pressure_correction{};
};

[[nodiscard]] ConservedState add(ConservedState left, const ConservedState& right) {
    left.depth_m += right.depth_m;
    left.momentum_x_m2_per_s += right.momentum_x_m2_per_s;
    left.momentum_y_m2_per_s += right.momentum_y_m2_per_s;
    return left;
}

[[nodiscard]] ConservedState read_state(const std::vector<float>& depth_m,
                                        const std::vector<Fluid25DMomentum>& momentum_m2_per_s,
                                        std::size_t index) {
    const double depth = static_cast<double>(depth_m[index]);
    const double momentum_x = static_cast<double>(momentum_m2_per_s[index].x_m2_per_s);
    const double momentum_y = static_cast<double>(momentum_m2_per_s[index].y_m2_per_s);
    if (!std::isfinite(depth) || depth < 0.0 || !std::isfinite(momentum_x) ||
        !std::isfinite(momentum_y)) {
        throw std::runtime_error("fluid 2.5D finite-volume state is invalid");
    }
    return {
        .depth_m = depth,
        .momentum_x_m2_per_s = momentum_x,
        .momentum_y_m2_per_s = momentum_y,
    };
}

[[nodiscard]] double read_tracer_concentration(const std::vector<float>& tracer_q_m,
                                               const std::vector<float>& depth_m,
                                               std::size_t index) {
    const double q_m = static_cast<double>(tracer_q_m[index]);
    const double depth = static_cast<double>(depth_m[index]);
    if (!std::isfinite(q_m) || q_m < -kTracerRoundingResidueM || !std::isfinite(depth) ||
        depth < 0.0 || q_m > depth + kTracerRoundingResidueM) {
        throw std::runtime_error("fluid 2.5D finite-volume tracer state is invalid");
    }
    if (depth <= 0.0) {
        if (std::abs(q_m) > kTracerRoundingResidueM) {
            throw std::runtime_error("fluid 2.5D finite-volume tracer state is dry but nonzero");
        }
        return 0.0;
    }
    return std::clamp(q_m, 0.0, depth) / depth;
}

[[nodiscard]] double normal_velocity(const ConservedState& state, double normal_x,
                                     double normal_y) {
    if (state.depth_m == 0.0) {
        return 0.0;
    }
    return ((state.momentum_x_m2_per_s * normal_x) + (state.momentum_y_m2_per_s * normal_y)) /
           state.depth_m;
}

[[nodiscard]] ConservedState physical_flux(const ConservedState& state, double normal_x,
                                           double normal_y, double gravity_m_per_s2) {
    const double normal_speed = normal_velocity(state, normal_x, normal_y);
    const double pressure = 0.5 * gravity_m_per_s2 * state.depth_m * state.depth_m;
    return {
        .depth_m = state.depth_m * normal_speed,
        .momentum_x_m2_per_s = (state.momentum_x_m2_per_s * normal_speed) + (pressure * normal_x),
        .momentum_y_m2_per_s = (state.momentum_y_m2_per_s * normal_speed) + (pressure * normal_y),
    };
}

[[nodiscard]] HydrostaticFaceFlux
hydrostatic_rusanov_flux(const ConservedState& left, const ConservedState& right,
                         double left_bed_height_m, double right_bed_height_m, double normal_x,
                         double normal_y, double gravity_m_per_s2) {
    // Difference-first form is algebraically identical to h + z - max(z),
    // but does not lose a shallow film when absolute terrain elevation is
    // large relative to depth. It is the canonical CPU/GPU reconstruction.
    const double left_depth_star_m =
        std::max(0.0, left.depth_m - std::max(0.0, right_bed_height_m - left_bed_height_m));
    const double right_depth_star_m =
        std::max(0.0, right.depth_m - std::max(0.0, left_bed_height_m - right_bed_height_m));

    const double left_normal_velocity = normal_velocity(left, normal_x, normal_y);
    const double left_tangent_velocity = normal_velocity(left, -normal_y, normal_x);
    const double right_normal_velocity = normal_velocity(right, normal_x, normal_y);
    const double right_tangent_velocity = normal_velocity(right, -normal_y, normal_x);
    const ConservedState left_star{
        .depth_m = left_depth_star_m,
        .momentum_x_m2_per_s = left_depth_star_m * ((left_normal_velocity * normal_x) -
                                                    (left_tangent_velocity * normal_y)),
        .momentum_y_m2_per_s = left_depth_star_m * ((left_normal_velocity * normal_y) +
                                                    (left_tangent_velocity * normal_x)),
    };
    const ConservedState right_star{
        .depth_m = right_depth_star_m,
        .momentum_x_m2_per_s = right_depth_star_m * ((right_normal_velocity * normal_x) -
                                                     (right_tangent_velocity * normal_y)),
        .momentum_y_m2_per_s = right_depth_star_m * ((right_normal_velocity * normal_y) +
                                                     (right_tangent_velocity * normal_x)),
    };

    const ConservedState left_flux = physical_flux(left_star, normal_x, normal_y, gravity_m_per_s2);
    const ConservedState right_flux =
        physical_flux(right_star, normal_x, normal_y, gravity_m_per_s2);
    const double left_wave_speed = std::abs(normal_velocity(left_star, normal_x, normal_y)) +
                                   std::sqrt(gravity_m_per_s2 * left_star.depth_m);
    const double right_wave_speed = std::abs(normal_velocity(right_star, normal_x, normal_y)) +
                                    std::sqrt(gravity_m_per_s2 * right_star.depth_m);
    const double wave_speed = std::max(left_wave_speed, right_wave_speed);
    if (!std::isfinite(wave_speed)) {
        throw std::runtime_error("fluid 2.5D finite-volume face wave speed is nonfinite");
    }

    HydrostaticFaceFlux face;
    face.homogeneous = {
        .depth_m = 0.5 * (left_flux.depth_m + right_flux.depth_m) -
                   0.5 * wave_speed * (right_star.depth_m - left_star.depth_m),
        .momentum_x_m2_per_s =
            0.5 * (left_flux.momentum_x_m2_per_s + right_flux.momentum_x_m2_per_s) -
            0.5 * wave_speed * (right_star.momentum_x_m2_per_s - left_star.momentum_x_m2_per_s),
        .momentum_y_m2_per_s =
            0.5 * (left_flux.momentum_y_m2_per_s + right_flux.momentum_y_m2_per_s) -
            0.5 * wave_speed * (right_star.momentum_y_m2_per_s - left_star.momentum_y_m2_per_s),
    };

    const double left_pressure_correction =
        0.5 * gravity_m_per_s2 *
        ((left.depth_m * left.depth_m) - (left_depth_star_m * left_depth_star_m));
    const double right_pressure_correction =
        0.5 * gravity_m_per_s2 *
        ((right.depth_m * right.depth_m) - (right_depth_star_m * right_depth_star_m));
    face.left_pressure_correction = {
        .depth_m = 0.0,
        .momentum_x_m2_per_s = left_pressure_correction * normal_x,
        .momentum_y_m2_per_s = left_pressure_correction * normal_y,
    };
    face.right_pressure_correction = {
        .depth_m = 0.0,
        .momentum_x_m2_per_s = right_pressure_correction * normal_x,
        .momentum_y_m2_per_s = right_pressure_correction * normal_y,
    };
    if (!std::isfinite(face.homogeneous.depth_m) ||
        !std::isfinite(face.homogeneous.momentum_x_m2_per_s) ||
        !std::isfinite(face.homogeneous.momentum_y_m2_per_s) ||
        !std::isfinite(face.left_pressure_correction.momentum_x_m2_per_s) ||
        !std::isfinite(face.left_pressure_correction.momentum_y_m2_per_s) ||
        !std::isfinite(face.right_pressure_correction.momentum_x_m2_per_s) ||
        !std::isfinite(face.right_pressure_correction.momentum_y_m2_per_s)) {
        throw std::runtime_error("fluid 2.5D finite-volume face flux is nonfinite");
    }
    return face;
}

void validate_finite_volume_scenario(const Fluid25DConfig& config,
                                     const Fluid25DScenarioData& scenario) {
    validate_fluid_25d_config(config);
    if (config.solver != Fluid25DSolver::FiniteVolume) {
        throw std::runtime_error("fluid 2.5D finite-volume oracle requires solver finite-volume");
    }
    if (scenario.width != config.grid_width || scenario.height != config.grid_height ||
        scenario.cell_size_m != config.cell_size_m) {
        throw std::runtime_error("fluid 2.5D finite-volume scenario does not match config");
    }
    const std::size_t cell_count = fluid_25d_cell_count(config);
    if (scenario.terrain_height_m.size() != cell_count ||
        scenario.initial_water_depth_m.size() != cell_count ||
        scenario.source_depth_rate_m_per_s.size() != cell_count ||
        scenario.sink_depth_rate_m_per_s.size() != cell_count ||
        scenario.boundary_outflow_face_mask.size() != cell_count) {
        throw std::runtime_error("fluid 2.5D finite-volume scenario field sizes do not match");
    }
    validate_fluid_25d_boundary_outflow_face_mask(config.grid_width, config.grid_height,
                                                  scenario.boundary_outflow_face_mask);
    for (std::size_t index = 0; index < cell_count; ++index) {
        if (!std::isfinite(scenario.terrain_height_m[index]) ||
            !std::isfinite(scenario.initial_water_depth_m[index]) ||
            scenario.initial_water_depth_m[index] < 0.0F ||
            !std::isfinite(scenario.source_depth_rate_m_per_s[index]) ||
            scenario.source_depth_rate_m_per_s[index] < 0.0F ||
            !std::isfinite(scenario.sink_depth_rate_m_per_s[index]) ||
            scenario.sink_depth_rate_m_per_s[index] < 0.0F) {
            throw std::runtime_error("fluid 2.5D finite-volume scenario field is invalid");
        }
    }
}

[[nodiscard]] float cfl_number(const Fluid25DConfig& config, const std::vector<float>& depth_m,
                               const std::vector<Fluid25DMomentum>& momentum_m2_per_s,
                               float delta_seconds) {
    double max_x_speed_m_per_s = 0.0;
    double max_y_speed_m_per_s = 0.0;
    for (std::size_t index = 0; index < depth_m.size(); ++index) {
        const ConservedState state = read_state(depth_m, momentum_m2_per_s, index);
        const double wave_speed_m_per_s =
            std::sqrt(static_cast<double>(config.gravity_m_per_s2) * state.depth_m);
        const double velocity_x_m_per_s =
            state.depth_m > static_cast<double>(config.minimum_wet_depth_m)
                ? state.momentum_x_m2_per_s / state.depth_m
                : 0.0;
        const double velocity_y_m_per_s =
            state.depth_m > static_cast<double>(config.minimum_wet_depth_m)
                ? state.momentum_y_m2_per_s / state.depth_m
                : 0.0;
        max_x_speed_m_per_s =
            std::max(max_x_speed_m_per_s, std::abs(velocity_x_m_per_s) + wave_speed_m_per_s);
        max_y_speed_m_per_s =
            std::max(max_y_speed_m_per_s, std::abs(velocity_y_m_per_s) + wave_speed_m_per_s);
    }
    const double cfl =
        (static_cast<double>(delta_seconds) / static_cast<double>(config.cell_size_m)) *
        (max_x_speed_m_per_s + max_y_speed_m_per_s);
    if (!std::isfinite(cfl)) {
        throw std::runtime_error("fluid 2.5D finite-volume CFL number is nonfinite");
    }
    return static_cast<float>(cfl);
}

} // namespace

Fluid25DFiniteVolumeOracle::Fluid25DFiniteVolumeOracle(Fluid25DConfig config,
                                                       Fluid25DScenarioData scenario)
    : config_(std::move(config)), scenario_(std::move(scenario)) {
    validate_finite_volume_scenario(config_, scenario_);
    const std::size_t cell_count = fluid_25d_cell_count(config_);
    water_depth_m_.resize(cell_count);
    source_sink_depth_m_.resize(cell_count);
    next_water_depth_m_.resize(cell_count);
    momentum_m2_per_s_.resize(cell_count);
    source_sink_momentum_m2_per_s_.resize(cell_count);
    next_momentum_m2_per_s_.resize(cell_count);
    transport_delta_.resize(cell_count);
    tracer_q_m_.resize(cell_count);
    source_sink_tracer_q_m_.resize(cell_count);
    next_tracer_q_m_.resize(cell_count);
    tracer_transport_delta_.resize(cell_count);
    tracer_concentration_.resize(cell_count);
    velocity_m_per_s_.resize(cell_count);
    wet_mask_.resize(cell_count);
    reset();
}

double Fluid25DFiniteVolumeOracle::total_water_volume_m3() const {
    const double cell_area_m2 =
        static_cast<double>(config_.cell_size_m) * static_cast<double>(config_.cell_size_m);
    double volume_m3 = 0.0;
    for (const float depth_m : water_depth_m_) {
        if (!std::isfinite(depth_m) || depth_m < 0.0F) {
            throw std::runtime_error("fluid 2.5D finite-volume water depth is invalid");
        }
        volume_m3 += static_cast<double>(depth_m) * cell_area_m2;
    }
    if (!std::isfinite(volume_m3)) {
        throw std::runtime_error("fluid 2.5D finite-volume water volume is nonfinite");
    }
    return volume_m3;
}

double Fluid25DFiniteVolumeOracle::total_tracer_amount_m3() const {
    const double cell_area_m2 =
        static_cast<double>(config_.cell_size_m) * static_cast<double>(config_.cell_size_m);
    double amount_m3 = 0.0;
    for (std::size_t index = 0U; index < tracer_q_m_.size(); ++index) {
        const double q_m = static_cast<double>(tracer_q_m_[index]);
        const double depth_m = static_cast<double>(water_depth_m_[index]);
        if (!std::isfinite(q_m) || q_m < -kTracerRoundingResidueM || !std::isfinite(depth_m) ||
            depth_m < 0.0 || q_m > depth_m + kTracerRoundingResidueM) {
            throw std::runtime_error("fluid 2.5D finite-volume tracer amount is invalid");
        }
        amount_m3 += std::max(0.0, q_m) * cell_area_m2;
    }
    if (!std::isfinite(amount_m3)) {
        throw std::runtime_error("fluid 2.5D finite-volume tracer amount is nonfinite");
    }
    return amount_m3;
}

Fluid25DStepLedger Fluid25DFiniteVolumeOracle::step(float source_rate_scale) {
    return step_impl(source_rate_scale, 0.0F).water;
}

Fluid25DTracerStepResult Fluid25DFiniteVolumeOracle::step_with_dye(float source_rate_scale) {
    const float dye_source_concentration = dye_source_schedule_.source_concentration(config_);
    return step_impl(source_rate_scale, dye_source_concentration);
}

Fluid25DTracerStepResult Fluid25DFiniteVolumeOracle::step_impl(float source_rate_scale,
                                                               float dye_source_concentration) {
    if (!std::isfinite(source_rate_scale) || source_rate_scale < 0.0F) {
        throw std::runtime_error("fluid 2.5D source rate scale must be finite and nonnegative");
    }
    if (!std::isfinite(dye_source_concentration) || dye_source_concentration < 0.0F ||
        dye_source_concentration > 1.0F) {
        throw std::runtime_error(
            "fluid 2.5D dye source concentration must be finite and within 0..1");
    }
    Fluid25DTracerStepResult result;
    result.water.volume_before_m3 = total_water_volume_m3();
    result.tracer.amount_before_m3 = total_tracer_amount_m3();
    const float substep_delta_seconds =
        config_.fixed_delta_seconds / static_cast<float>(config_.simulation_substeps);
    for (std::uint32_t substep = 0U; substep < config_.simulation_substeps; ++substep) {
        const StepResult substep_result =
            step_substep(substep_delta_seconds, source_rate_scale, dye_source_concentration);
        result.water.source_volume_m3 += substep_result.water.source_volume_m3;
        result.water.sink_volume_m3 += substep_result.water.sink_volume_m3;
        result.water.boundary_outflow_volume_m3 += substep_result.water.boundary_outflow_volume_m3;
        result.tracer.source_amount_m3 += substep_result.tracer.source_amount_m3;
        result.tracer.sink_amount_m3 += substep_result.tracer.sink_amount_m3;
        result.tracer.boundary_outflow_amount_m3 +=
            substep_result.tracer.boundary_outflow_amount_m3;
    }
    result.water.volume_after_m3 = total_water_volume_m3();
    result.tracer.amount_after_m3 = total_tracer_amount_m3();
    record_tracer_ledger(result.tracer);
    dye_source_schedule_.advance_fixed_step();
    return result;
}

Fluid25DFiniteVolumeOracle::StepResult
Fluid25DFiniteVolumeOracle::step_substep(float delta_seconds, float source_rate_scale,
                                         float dye_source_concentration) {
    const double cell_area_m2 =
        static_cast<double>(config_.cell_size_m) * static_cast<double>(config_.cell_size_m);
    StepResult result;
    Fluid25DStepLedger& ledger = result.water;
    Fluid25DTracerStepLedger& tracer_ledger = result.tracer;
    ledger.volume_before_m3 = total_water_volume_m3();
    tracer_ledger.amount_before_m3 = total_tracer_amount_m3();

    // Source then sink is intentionally assembled into scratch state first.
    // A rejected CFL update therefore leaves the persistent state unchanged.
    for (std::size_t index = 0; index < water_depth_m_.size(); ++index) {
        static_cast<void>(read_state(water_depth_m_, momentum_m2_per_s_, index));
        const double existing_q_m = static_cast<double>(tracer_q_m_[index]);
        if (!std::isfinite(existing_q_m) || existing_q_m < -kTracerRoundingResidueM ||
            existing_q_m > static_cast<double>(water_depth_m_[index]) + kTracerRoundingResidueM) {
            throw std::runtime_error("fluid 2.5D finite-volume tracer source state is invalid");
        }
        const float source_delta_m =
            scenario_.source_depth_rate_m_per_s[index] * source_rate_scale * delta_seconds;
        const float sourced_depth_m = water_depth_m_[index] + source_delta_m;
        if (!std::isfinite(sourced_depth_m) || sourced_depth_m < 0.0F) {
            throw std::runtime_error("fluid 2.5D finite-volume source update is invalid");
        }
        source_sink_depth_m_[index] = sourced_depth_m;
        source_sink_momentum_m2_per_s_[index] = momentum_m2_per_s_[index];
        ledger.source_volume_m3 +=
            static_cast<double>(sourced_depth_m - water_depth_m_[index]) * cell_area_m2;
        const double source_q_m =
            static_cast<double>(source_delta_m) * static_cast<double>(dye_source_concentration);
        double sourced_q_m = existing_q_m + source_q_m;
        if (!std::isfinite(sourced_q_m) || sourced_q_m < -kTracerRoundingResidueM ||
            sourced_q_m > static_cast<double>(sourced_depth_m) + kTracerRoundingResidueM) {
            throw std::runtime_error("fluid 2.5D finite-volume tracer source update is invalid");
        }
        sourced_q_m = std::clamp(sourced_q_m, 0.0, static_cast<double>(sourced_depth_m));
        source_sink_tracer_q_m_[index] = static_cast<float>(sourced_q_m);
        tracer_ledger.source_amount_m3 += source_q_m * cell_area_m2;

        const float requested_sink_delta_m =
            scenario_.sink_depth_rate_m_per_s[index] * delta_seconds;
        const float removed_depth_m = std::min(sourced_depth_m, requested_sink_delta_m);
        const float sink_depth_m = sourced_depth_m - removed_depth_m;
        source_sink_depth_m_[index] = sink_depth_m;
        ledger.sink_volume_m3 += static_cast<double>(sourced_depth_m - sink_depth_m) * cell_area_m2;
        const double retained_fraction =
            sourced_depth_m > 0.0F
                ? static_cast<double>(sink_depth_m) / static_cast<double>(sourced_depth_m)
                : 0.0;
        const double sink_q_m = sourced_q_m * retained_fraction;
        if (!std::isfinite(sink_q_m) || sink_q_m < -kTracerRoundingResidueM ||
            sink_q_m > static_cast<double>(sink_depth_m) + kTracerRoundingResidueM) {
            throw std::runtime_error("fluid 2.5D finite-volume tracer sink update is invalid");
        }
        source_sink_tracer_q_m_[index] =
            static_cast<float>(std::clamp(sink_q_m, 0.0, static_cast<double>(sink_depth_m)));
        tracer_ledger.sink_amount_m3 += (sourced_q_m - sink_q_m) * cell_area_m2;

        // Sources add depth but no momentum. A sink carries away the local
        // momentum fraction with the removed depth, leaving velocity intact.
        if (sourced_depth_m > 0.0F) {
            const float retained_fraction_f = sink_depth_m / sourced_depth_m;
            source_sink_momentum_m2_per_s_[index].x_m2_per_s *= retained_fraction_f;
            source_sink_momentum_m2_per_s_[index].y_m2_per_s *= retained_fraction_f;
        } else {
            source_sink_momentum_m2_per_s_[index] = {};
        }
        if (sink_depth_m <= config_.minimum_wet_depth_m) {
            source_sink_momentum_m2_per_s_[index] = {};
        }
    }

    const float cfl =
        cfl_number(config_, source_sink_depth_m_, source_sink_momentum_m2_per_s_, delta_seconds);
    if (cfl > kTargetCfl) {
        throw std::runtime_error(
            "fluid 2.5D finite-volume CFL exceeds the 0.45 target (0.50 hard ceiling)");
    }

    std::fill(transport_delta_.begin(), transport_delta_.end(), TransportDelta{});
    std::fill(tracer_transport_delta_.begin(), tracer_transport_delta_.end(),
              TracerTransportDelta{});
    const auto add_face_contribution = [this](std::size_t index, double sign,
                                              const ConservedState& flux) {
        transport_delta_[index].depth_m_per_s += sign * flux.depth_m;
        transport_delta_[index].momentum_x_m2_per_s2 += sign * flux.momentum_x_m2_per_s;
        transport_delta_[index].momentum_y_m2_per_s2 += sign * flux.momentum_y_m2_per_s;
    };
    const auto process_internal_face =
        [this, &add_face_contribution](std::size_t left_index, std::size_t right_index,
                                       double normal_x, double normal_y) {
            const ConservedState left =
                read_state(source_sink_depth_m_, source_sink_momentum_m2_per_s_, left_index);
            const ConservedState right =
                read_state(source_sink_depth_m_, source_sink_momentum_m2_per_s_, right_index);
            const HydrostaticFaceFlux face = hydrostatic_rusanov_flux(
                left, right, static_cast<double>(scenario_.terrain_height_m[left_index]),
                static_cast<double>(scenario_.terrain_height_m[right_index]), normal_x, normal_y,
                static_cast<double>(config_.gravity_m_per_s2));
            add_face_contribution(left_index, -1.0,
                                  add(face.homogeneous, face.left_pressure_correction));
            add_face_contribution(right_index, 1.0,
                                  add(face.homogeneous, face.right_pressure_correction));
            const double left_concentration =
                read_tracer_concentration(source_sink_tracer_q_m_, source_sink_depth_m_,
                                          left_index);
            const double right_concentration =
                read_tracer_concentration(source_sink_tracer_q_m_, source_sink_depth_m_,
                                          right_index);
            const double tracer_flux_m2_per_s =
                face.homogeneous.depth_m >= 0.0 ? face.homogeneous.depth_m * left_concentration
                                                : face.homogeneous.depth_m * right_concentration;
            if (!std::isfinite(tracer_flux_m2_per_s)) {
                throw std::runtime_error("fluid 2.5D finite-volume tracer face flux is nonfinite");
            }
            tracer_transport_delta_[left_index].q_m2_per_s -= tracer_flux_m2_per_s;
            tracer_transport_delta_[right_index].q_m2_per_s += tracer_flux_m2_per_s;
        };

    for (std::uint32_t y = 0U; y < config_.grid_height; ++y) {
        for (std::uint32_t x = 0U; x + 1U < config_.grid_width; ++x) {
            const std::size_t left =
                fluid_25d_scenario_index(config_.grid_width, config_.grid_height, x, y);
            const std::size_t right =
                fluid_25d_scenario_index(config_.grid_width, config_.grid_height, x + 1U, y);
            process_internal_face(left, right, 1.0, 0.0);
        }
    }
    for (std::uint32_t y = 0U; y + 1U < config_.grid_height; ++y) {
        for (std::uint32_t x = 0U; x < config_.grid_width; ++x) {
            const std::size_t down =
                fluid_25d_scenario_index(config_.grid_width, config_.grid_height, x, y);
            const std::size_t up =
                fluid_25d_scenario_index(config_.grid_width, config_.grid_height, x, y + 1U);
            process_internal_face(down, up, 0.0, 1.0);
        }
    }

    const auto process_boundary_face = [this, &add_face_contribution, delta_seconds, &ledger,
                                        &tracer_ledger](std::size_t index, Fluid25DFace face,
                                                        double normal_x, double normal_y) {
        const ConservedState interior =
            read_state(source_sink_depth_m_, source_sink_momentum_m2_per_s_, index);
        const bool open_outflow = (scenario_.boundary_outflow_face_mask[index] &
                                   fluid_25d_boundary_outflow_bit(face)) != 0U;
        ConservedState exterior{};
        if (!open_outflow) {
            const double outward_velocity = normal_velocity(interior, normal_x, normal_y);
            exterior = {
                .depth_m = interior.depth_m,
                .momentum_x_m2_per_s = interior.momentum_x_m2_per_s -
                                       (2.0 * interior.depth_m * outward_velocity * normal_x),
                .momentum_y_m2_per_s = interior.momentum_y_m2_per_s -
                                       (2.0 * interior.depth_m * outward_velocity * normal_y),
            };
        }
        const double bed_height_m = static_cast<double>(scenario_.terrain_height_m[index]);
        HydrostaticFaceFlux face_flux =
            hydrostatic_rusanov_flux(interior, exterior, bed_height_m, bed_height_m, normal_x,
                                     normal_y, static_cast<double>(config_.gravity_m_per_s2));
        ConservedState outward_flux =
            add(face_flux.homogeneous, face_flux.left_pressure_correction);
        if (!open_outflow) {
            if (std::abs(outward_flux.depth_m) > kDepthRoundingResidueM) {
                throw std::runtime_error(
                    "fluid 2.5D finite-volume reflected boundary changed water mass");
            }
            outward_flux.depth_m = 0.0;
        } else {
            // With a Rusanov dry ghost, h-flux is nonnegative analytically.
            // Reject anything beyond a final rounding residue instead of
            // converting a boundary into an untracked external water source.
            if (outward_flux.depth_m < -kDepthRoundingResidueM) {
                throw std::runtime_error(
                    "fluid 2.5D finite-volume outflow boundary attempted external inflow");
            }
            outward_flux.depth_m = std::max(0.0, outward_flux.depth_m);
            ledger.boundary_outflow_volume_m3 += outward_flux.depth_m *
                                                 static_cast<double>(config_.cell_size_m) *
                                                 static_cast<double>(delta_seconds);
            const double interior_concentration =
                read_tracer_concentration(source_sink_tracer_q_m_, source_sink_depth_m_, index);
            const double tracer_flux_m2_per_s = outward_flux.depth_m * interior_concentration;
            if (!std::isfinite(tracer_flux_m2_per_s) || tracer_flux_m2_per_s < 0.0) {
                throw std::runtime_error("fluid 2.5D finite-volume tracer outflow flux is invalid");
            }
            tracer_ledger.boundary_outflow_amount_m3 += tracer_flux_m2_per_s *
                                                        static_cast<double>(config_.cell_size_m) *
                                                        static_cast<double>(delta_seconds);
            tracer_transport_delta_[index].q_m2_per_s -= tracer_flux_m2_per_s;
        }
        add_face_contribution(index, -1.0, outward_flux);
    };

    for (std::uint32_t y = 0U; y < config_.grid_height; ++y) {
        const std::size_t left =
            fluid_25d_scenario_index(config_.grid_width, config_.grid_height, 0U, y);
        const std::size_t right = fluid_25d_scenario_index(config_.grid_width, config_.grid_height,
                                                           config_.grid_width - 1U, y);
        process_boundary_face(left, Fluid25DFace::Left, -1.0, 0.0);
        process_boundary_face(right, Fluid25DFace::Right, 1.0, 0.0);
    }
    for (std::uint32_t x = 0U; x < config_.grid_width; ++x) {
        const std::size_t down =
            fluid_25d_scenario_index(config_.grid_width, config_.grid_height, x, 0U);
        const std::size_t up = fluid_25d_scenario_index(config_.grid_width, config_.grid_height, x,
                                                        config_.grid_height - 1U);
        process_boundary_face(down, Fluid25DFace::Down, 0.0, -1.0);
        process_boundary_face(up, Fluid25DFace::Up, 0.0, 1.0);
    }

    const double transport_scale =
        static_cast<double>(delta_seconds) / static_cast<double>(config_.cell_size_m);
    const double damping = std::exp(-static_cast<double>(config_.flow_damping_per_second) *
                                    static_cast<double>(delta_seconds));
    for (std::size_t index = 0; index < water_depth_m_.size(); ++index) {
        double updated_depth_m = static_cast<double>(source_sink_depth_m_[index]) +
                                 (transport_scale * transport_delta_[index].depth_m_per_s);
        if (!std::isfinite(updated_depth_m) || updated_depth_m < -kDepthRoundingResidueM) {
            throw std::runtime_error(
                "fluid 2.5D finite-volume transport produced an invalid water depth");
        }
        updated_depth_m = std::max(0.0, updated_depth_m);
        double updated_tracer_q_m = static_cast<double>(source_sink_tracer_q_m_[index]) +
                                    (transport_scale * tracer_transport_delta_[index].q_m2_per_s);
        if (!std::isfinite(updated_tracer_q_m) || updated_tracer_q_m < -kTracerRoundingResidueM ||
            updated_tracer_q_m > updated_depth_m + kTracerRoundingResidueM) {
            throw std::runtime_error("fluid 2.5D finite-volume tracer transport is invalid");
        }
        updated_tracer_q_m = std::clamp(updated_tracer_q_m, 0.0, updated_depth_m);
        double updated_momentum_x_m2_per_s =
            (static_cast<double>(source_sink_momentum_m2_per_s_[index].x_m2_per_s) +
             (transport_scale * transport_delta_[index].momentum_x_m2_per_s2)) *
            damping;
        double updated_momentum_y_m2_per_s =
            (static_cast<double>(source_sink_momentum_m2_per_s_[index].y_m2_per_s) +
             (transport_scale * transport_delta_[index].momentum_y_m2_per_s2)) *
            damping;
        if (!std::isfinite(updated_momentum_x_m2_per_s) ||
            !std::isfinite(updated_momentum_y_m2_per_s)) {
            throw std::runtime_error("fluid 2.5D finite-volume momentum is nonfinite");
        }
        if (updated_depth_m <= static_cast<double>(config_.minimum_wet_depth_m)) {
            updated_momentum_x_m2_per_s = 0.0;
            updated_momentum_y_m2_per_s = 0.0;
        }
        next_water_depth_m_[index] = static_cast<float>(updated_depth_m);
        next_tracer_q_m_[index] = static_cast<float>(updated_tracer_q_m);
        next_momentum_m2_per_s_[index] = {
            .x_m2_per_s = static_cast<float>(updated_momentum_x_m2_per_s),
            .y_m2_per_s = static_cast<float>(updated_momentum_y_m2_per_s),
        };
        static_cast<void>(read_state(next_water_depth_m_, next_momentum_m2_per_s_, index));
    }

    water_depth_m_.swap(next_water_depth_m_);
    tracer_q_m_.swap(next_tracer_q_m_);
    momentum_m2_per_s_.swap(next_momentum_m2_per_s_);
    last_cfl_number_ = cfl;
    derive_velocity_and_wet_state();
    derive_tracer_concentration();
    ledger.volume_after_m3 = total_water_volume_m3();
    tracer_ledger.amount_after_m3 = total_tracer_amount_m3();
    return result;
}

void Fluid25DFiniteVolumeOracle::derive_velocity_and_wet_state() {
    for (std::size_t index = 0; index < water_depth_m_.size(); ++index) {
        const ConservedState state = read_state(water_depth_m_, momentum_m2_per_s_, index);
        wet_mask_[index] =
            state.depth_m > static_cast<double>(config_.minimum_wet_depth_m) ? 1U : 0U;
        Fluid25DVelocity& velocity = velocity_m_per_s_[index];
        velocity = {};
        if (wet_mask_[index] == 0U) {
            continue;
        }
        velocity.x_m_per_s = static_cast<float>(state.momentum_x_m2_per_s / state.depth_m);
        velocity.y_m_per_s = static_cast<float>(state.momentum_y_m2_per_s / state.depth_m);
        if (!std::isfinite(velocity.x_m_per_s) || !std::isfinite(velocity.y_m_per_s)) {
            throw std::runtime_error("fluid 2.5D finite-volume derived velocity is nonfinite");
        }
    }
}

void Fluid25DFiniteVolumeOracle::derive_tracer_concentration() {
    for (std::size_t index = 0U; index < tracer_q_m_.size(); ++index) {
        const double concentration = read_tracer_concentration(tracer_q_m_, water_depth_m_, index);
        if (!std::isfinite(concentration) || concentration < 0.0 || concentration > 1.0) {
            throw std::runtime_error("fluid 2.5D finite-volume tracer concentration is invalid");
        }
        tracer_concentration_[index] = static_cast<float>(concentration);
    }
}

void Fluid25DFiniteVolumeOracle::record_tracer_ledger(const Fluid25DTracerStepLedger& ledger) {
    last_tracer_step_ledger_ = ledger;
    if (dye_source_schedule_.completed_steps() == 0U) {
        cumulative_tracer_ledger_ = ledger;
        return;
    }
    cumulative_tracer_ledger_.amount_after_m3 = ledger.amount_after_m3;
    cumulative_tracer_ledger_.source_amount_m3 += ledger.source_amount_m3;
    cumulative_tracer_ledger_.sink_amount_m3 += ledger.sink_amount_m3;
    cumulative_tracer_ledger_.boundary_outflow_amount_m3 += ledger.boundary_outflow_amount_m3;
}

void Fluid25DFiniteVolumeOracle::reset() {
    water_depth_m_ = scenario_.initial_water_depth_m;
    std::fill(source_sink_depth_m_.begin(), source_sink_depth_m_.end(), 0.0F);
    std::fill(next_water_depth_m_.begin(), next_water_depth_m_.end(), 0.0F);
    std::fill(momentum_m2_per_s_.begin(), momentum_m2_per_s_.end(), Fluid25DMomentum{});
    std::fill(source_sink_momentum_m2_per_s_.begin(), source_sink_momentum_m2_per_s_.end(),
              Fluid25DMomentum{});
    std::fill(next_momentum_m2_per_s_.begin(), next_momentum_m2_per_s_.end(), Fluid25DMomentum{});
    std::fill(transport_delta_.begin(), transport_delta_.end(), TransportDelta{});
    std::fill(tracer_q_m_.begin(), tracer_q_m_.end(), 0.0F);
    std::fill(source_sink_tracer_q_m_.begin(), source_sink_tracer_q_m_.end(), 0.0F);
    std::fill(next_tracer_q_m_.begin(), next_tracer_q_m_.end(), 0.0F);
    std::fill(tracer_transport_delta_.begin(), tracer_transport_delta_.end(),
              TracerTransportDelta{});
    std::fill(tracer_concentration_.begin(), tracer_concentration_.end(), 0.0F);
    last_cfl_number_ = 0.0F;
    dye_source_schedule_.reset();
    last_tracer_step_ledger_ = {};
    cumulative_tracer_ledger_ = {};
    derive_velocity_and_wet_state();
    derive_tracer_concentration();
}

} // namespace cubey::projects::fluid::fluid_25d
