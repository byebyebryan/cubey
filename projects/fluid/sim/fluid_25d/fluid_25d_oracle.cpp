#include "fluid_25d_oracle.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace cubey::projects::fluid::fluid_25d {
namespace {

[[nodiscard]] std::size_t neighbor_index(std::uint32_t width, std::uint32_t height, std::uint32_t x,
                                         std::uint32_t y, Fluid25DFace face) {
    switch (face) {
    case Fluid25DFace::Left:
        return x == 0 ? kFluid25DNoCell : fluid_25d_scenario_index(width, height, x - 1U, y);
    case Fluid25DFace::Right:
        return x == width - 1U ? kFluid25DNoCell
                               : fluid_25d_scenario_index(width, height, x + 1U, y);
    case Fluid25DFace::Down:
        return y == 0 ? kFluid25DNoCell : fluid_25d_scenario_index(width, height, x, y - 1U);
    case Fluid25DFace::Up:
        return y == height - 1U ? kFluid25DNoCell
                                : fluid_25d_scenario_index(width, height, x, y + 1U);
    }
    return kFluid25DNoCell;
}

[[nodiscard]] Fluid25DFace opposite_face(Fluid25DFace face) {
    switch (face) {
    case Fluid25DFace::Left:
        return Fluid25DFace::Right;
    case Fluid25DFace::Right:
        return Fluid25DFace::Left;
    case Fluid25DFace::Down:
        return Fluid25DFace::Up;
    case Fluid25DFace::Up:
        return Fluid25DFace::Down;
    }
    return Fluid25DFace::Left;
}

void validate_scenario(const Fluid25DConfig& config, const Fluid25DScenarioData& scenario) {
    validate_fluid_25d_config(config);
    if (scenario.width != config.grid_width || scenario.height != config.grid_height) {
        throw std::runtime_error("fluid 2.5D scenario dimensions do not match config");
    }
    if (scenario.cell_size_m != config.cell_size_m) {
        throw std::runtime_error("fluid 2.5D scenario cell size does not match config");
    }
    const std::size_t cell_count = fluid_25d_cell_count(config);
    if (scenario.terrain_height_m.size() != cell_count ||
        scenario.initial_water_depth_m.size() != cell_count ||
        scenario.source_depth_rate_m_per_s.size() != cell_count ||
        scenario.sink_depth_rate_m_per_s.size() != cell_count) {
        throw std::runtime_error("fluid 2.5D scenario field sizes do not match config");
    }
    for (std::size_t index = 0; index < cell_count; ++index) {
        if (!std::isfinite(scenario.terrain_height_m[index])) {
            throw std::runtime_error("fluid 2.5D terrain contains a nonfinite value");
        }
        if (scenario.initial_water_depth_m[index] < 0.0F ||
            !std::isfinite(scenario.initial_water_depth_m[index])) {
            throw std::runtime_error(
                "fluid 2.5D initial water depth must be finite and nonnegative");
        }
        if (scenario.source_depth_rate_m_per_s[index] < 0.0F ||
            !std::isfinite(scenario.source_depth_rate_m_per_s[index])) {
            throw std::runtime_error("fluid 2.5D source depth rate must be finite and nonnegative");
        }
        if (scenario.sink_depth_rate_m_per_s[index] < 0.0F ||
            !std::isfinite(scenario.sink_depth_rate_m_per_s[index])) {
            throw std::runtime_error("fluid 2.5D sink depth rate must be finite and nonnegative");
        }
    }
    if (scenario.source_cell != kFluid25DNoCell && scenario.source_cell >= cell_count) {
        throw std::runtime_error("fluid 2.5D source cell is out of bounds");
    }
    if (scenario.sink_cell != kFluid25DNoCell && scenario.sink_cell >= cell_count) {
        throw std::runtime_error("fluid 2.5D sink cell is out of bounds");
    }
}

} // namespace

Fluid25DOracle::Fluid25DOracle(Fluid25DConfig config, Fluid25DScenarioData scenario)
    : config_(std::move(config)), scenario_(std::move(scenario)) {
    validate_scenario(config_, scenario_);
    const std::size_t cell_count = fluid_25d_cell_count(config_);
    water_depth_m_.resize(cell_count);
    next_water_depth_m_.resize(cell_count);
    outgoing_flux_m3_per_s_.resize(cell_count);
    velocity_m_per_s_.resize(cell_count);
    wet_mask_.resize(cell_count);
    reset();
}

double Fluid25DOracle::total_water_volume_m3() const {
    const double cell_area_m2 =
        static_cast<double>(config_.cell_size_m) * static_cast<double>(config_.cell_size_m);
    double volume_m3 = 0.0;
    for (const float depth_m : water_depth_m_) {
        if (depth_m < 0.0F || !std::isfinite(depth_m)) {
            throw std::runtime_error("fluid 2.5D water depth is invalid");
        }
        volume_m3 += static_cast<double>(depth_m) * cell_area_m2;
    }
    if (!std::isfinite(volume_m3)) {
        throw std::runtime_error("fluid 2.5D water volume is nonfinite");
    }
    return volume_m3;
}

Fluid25DStepLedger Fluid25DOracle::step() {
    Fluid25DStepLedger ledger;
    ledger.volume_before_m3 = total_water_volume_m3();
    const float substep_delta_seconds =
        config_.fixed_delta_seconds / static_cast<float>(config_.simulation_substeps);
    for (std::uint32_t substep = 0; substep < config_.simulation_substeps; ++substep) {
        const Fluid25DStepLedger substep_ledger = step_substep(substep_delta_seconds);
        ledger.source_volume_m3 += substep_ledger.source_volume_m3;
        ledger.sink_volume_m3 += substep_ledger.sink_volume_m3;
    }
    ledger.volume_after_m3 = total_water_volume_m3();
    return ledger;
}

Fluid25DStepLedger Fluid25DOracle::step_substep(float delta_seconds) {
    const double cell_area_m2 =
        static_cast<double>(config_.cell_size_m) * static_cast<double>(config_.cell_size_m);
    Fluid25DStepLedger ledger;
    ledger.volume_before_m3 = total_water_volume_m3();

    // Sources and sinks are depth rates.  Record the actual representable
    // float change so the ledger remains tied to state, including rounding.
    for (std::size_t index = 0; index < water_depth_m_.size(); ++index) {
        const float old_depth_m = water_depth_m_[index];
        const float source_depth_delta_m =
            scenario_.source_depth_rate_m_per_s[index] * delta_seconds;
        const float sourced_depth_m = old_depth_m + source_depth_delta_m;
        if (!std::isfinite(sourced_depth_m) || sourced_depth_m < 0.0F) {
            throw std::runtime_error("fluid 2.5D source update produced an invalid depth");
        }
        water_depth_m_[index] = sourced_depth_m;
        ledger.source_volume_m3 +=
            static_cast<double>(sourced_depth_m - old_depth_m) * cell_area_m2;

        const float before_sink_depth_m = water_depth_m_[index];
        const float requested_sink_depth_m =
            scenario_.sink_depth_rate_m_per_s[index] * delta_seconds;
        const float removed_depth_m = std::min(before_sink_depth_m, requested_sink_depth_m);
        water_depth_m_[index] = before_sink_depth_m - removed_depth_m;
        ledger.sink_volume_m3 +=
            static_cast<double>(before_sink_depth_m - water_depth_m_[index]) * cell_area_m2;
    }

    const float damping = std::exp(-config_.flow_damping_per_second * delta_seconds);
    const float cell_size_m = config_.cell_size_m;
    const float gravity_m_per_s2 = config_.gravity_m_per_s2;
    const float minimum_wet_depth_m = config_.minimum_wet_depth_m;
    // River V0 uses one cell face as the local pipe cross-section.  This keeps
    // the reference equation in physical units while avoiding a public
    // project option before a GPU implementation demonstrates a need for it.
    const float pipe_area_m2 = cell_size_m * cell_size_m;
    const std::array<Fluid25DFace, 4> faces{
        Fluid25DFace::Left,
        Fluid25DFace::Right,
        Fluid25DFace::Down,
        Fluid25DFace::Up,
    };

    for (std::uint32_t y = 0; y < config_.grid_height; ++y) {
        for (std::uint32_t x = 0; x < config_.grid_width; ++x) {
            const std::size_t index =
                fluid_25d_scenario_index(config_.grid_width, config_.grid_height, x, y);
            const float depth_m = water_depth_m_[index];
            if (depth_m < 0.0F || !std::isfinite(depth_m)) {
                throw std::runtime_error("fluid 2.5D water depth is invalid before flux solve");
            }
            const float surface_height_m = scenario_.terrain_height_m[index] + depth_m;
            if (!std::isfinite(surface_height_m)) {
                throw std::runtime_error("fluid 2.5D surface height is nonfinite");
            }
            Fluid25DFaceFlux& outgoing = outgoing_flux_m3_per_s_[index];
            if (depth_m <= minimum_wet_depth_m) {
                // A dry cell cannot retain or originate a directed pipe
                // discharge.  This is a numerical wet/dry threshold, not a
                // presentation-only diagnostic.
                outgoing.fill(0.0F);
                continue;
            }
            for (const Fluid25DFace face : faces) {
                const std::size_t neighbor =
                    neighbor_index(config_.grid_width, config_.grid_height, x, y, face);
                if (neighbor == kFluid25DNoCell) {
                    outgoing[static_cast<std::size_t>(face)] =
                        0.0F; // River V0 has closed outer boundaries.
                    continue;
                }
                const float neighbor_surface_height_m =
                    scenario_.terrain_height_m[neighbor] + water_depth_m_[neighbor];
                if (!std::isfinite(neighbor_surface_height_m)) {
                    throw std::runtime_error("fluid 2.5D neighbor surface height is nonfinite");
                }
                const float height_difference_m = surface_height_m - neighbor_surface_height_m;
                if (!std::isfinite(height_difference_m)) {
                    throw std::runtime_error("fluid 2.5D surface difference is nonfinite");
                }
                const std::size_t face_index = static_cast<std::size_t>(face);
                const float previous_flux_m3_per_s = outgoing[face_index];
                if (previous_flux_m3_per_s < 0.0F || !std::isfinite(previous_flux_m3_per_s)) {
                    throw std::runtime_error("fluid 2.5D previous face flux is invalid");
                }
                // Persistent directed pipe discharge.  A higher neighboring
                // surface therefore decelerates retained flow instead of
                // resetting it to an instantaneous slope-only value.
                const float updated_flux_m3_per_s =
                    (damping * previous_flux_m3_per_s) +
                    (delta_seconds * pipe_area_m2 * gravity_m_per_s2 * height_difference_m /
                     cell_size_m);
                if (!std::isfinite(updated_flux_m3_per_s)) {
                    throw std::runtime_error("fluid 2.5D flux update is nonfinite");
                }
                outgoing[face_index] = std::max(0.0F, updated_flux_m3_per_s);
            }

            // A cell cannot export more water than it owns during this
            // substep.  This is the positivity limiter for the virtual pipes.
            const double available_volume_m3 = static_cast<double>(depth_m) * cell_area_m2;
            const double total_outgoing_m3_per_s =
                static_cast<double>(outgoing[0]) + static_cast<double>(outgoing[1]) +
                static_cast<double>(outgoing[2]) + static_cast<double>(outgoing[3]);
            const double requested_volume_m3 =
                total_outgoing_m3_per_s * static_cast<double>(delta_seconds);
            if (requested_volume_m3 > available_volume_m3 && requested_volume_m3 > 0.0) {
                const float scale = static_cast<float>(available_volume_m3 / requested_volume_m3);
                for (float& flux_m3_per_s : outgoing) {
                    flux_m3_per_s *= scale;
                }
            }
        }
    }

    const double depth_scale = static_cast<double>(delta_seconds) / cell_area_m2;
    for (std::uint32_t y = 0; y < config_.grid_height; ++y) {
        for (std::uint32_t x = 0; x < config_.grid_width; ++x) {
            const std::size_t index =
                fluid_25d_scenario_index(config_.grid_width, config_.grid_height, x, y);
            const Fluid25DFaceFlux& outgoing = outgoing_flux_m3_per_s_[index];
            double incoming_flux_m3_per_s = 0.0;
            for (const Fluid25DFace face : faces) {
                const std::size_t neighbor =
                    neighbor_index(config_.grid_width, config_.grid_height, x, y, face);
                if (neighbor != kFluid25DNoCell) {
                    incoming_flux_m3_per_s += static_cast<double>(
                        outgoing_flux_m3_per_s_[neighbor]
                                               [static_cast<std::size_t>(opposite_face(face))]);
                }
            }
            const double total_outgoing_m3_per_s =
                static_cast<double>(outgoing[0]) + static_cast<double>(outgoing[1]) +
                static_cast<double>(outgoing[2]) + static_cast<double>(outgoing[3]);
            const double updated_depth_m =
                static_cast<double>(water_depth_m_[index]) +
                depth_scale * (incoming_flux_m3_per_s - total_outgoing_m3_per_s);
            if (!std::isfinite(updated_depth_m)) {
                throw std::runtime_error("fluid 2.5D flux update produced a nonfinite depth");
            }
            // The outflow limiter guarantees positivity.  Clamp only the tiny
            // negative residue that can result from float accumulation.
            next_water_depth_m_[index] = static_cast<float>(std::max(0.0, updated_depth_m));
        }
    }
    water_depth_m_.swap(next_water_depth_m_);
    derive_velocity_and_wet_state();
    ledger.volume_after_m3 = total_water_volume_m3();
    return ledger;
}

void Fluid25DOracle::derive_velocity_and_wet_state() {
    const float cell_size_m = config_.cell_size_m;
    const float minimum_wet_depth_m = config_.minimum_wet_depth_m;
    for (std::uint32_t y = 0; y < config_.grid_height; ++y) {
        for (std::uint32_t x = 0; x < config_.grid_width; ++x) {
            const std::size_t index =
                fluid_25d_scenario_index(config_.grid_width, config_.grid_height, x, y);
            const float depth_m = water_depth_m_[index];
            wet_mask_[index] = depth_m > minimum_wet_depth_m ? 1U : 0U;
            Fluid25DVelocity& velocity = velocity_m_per_s_[index];
            velocity = {};
            if (depth_m <= minimum_wet_depth_m) {
                continue;
            }

            const std::size_t left =
                neighbor_index(config_.grid_width, config_.grid_height, x, y, Fluid25DFace::Left);
            const std::size_t right =
                neighbor_index(config_.grid_width, config_.grid_height, x, y, Fluid25DFace::Right);
            const std::size_t down =
                neighbor_index(config_.grid_width, config_.grid_height, x, y, Fluid25DFace::Down);
            const std::size_t up =
                neighbor_index(config_.grid_width, config_.grid_height, x, y, Fluid25DFace::Up);

            float right_face_flow_m3_per_s =
                outgoing_flux_m3_per_s_[index][static_cast<std::size_t>(Fluid25DFace::Right)];
            if (right != kFluid25DNoCell) {
                right_face_flow_m3_per_s -=
                    outgoing_flux_m3_per_s_[right][static_cast<std::size_t>(Fluid25DFace::Left)];
            }
            float left_face_flow_m3_per_s =
                -outgoing_flux_m3_per_s_[index][static_cast<std::size_t>(Fluid25DFace::Left)];
            if (left != kFluid25DNoCell) {
                left_face_flow_m3_per_s +=
                    outgoing_flux_m3_per_s_[left][static_cast<std::size_t>(Fluid25DFace::Right)];
            }
            float up_face_flow_m3_per_s =
                outgoing_flux_m3_per_s_[index][static_cast<std::size_t>(Fluid25DFace::Up)];
            if (up != kFluid25DNoCell) {
                up_face_flow_m3_per_s -=
                    outgoing_flux_m3_per_s_[up][static_cast<std::size_t>(Fluid25DFace::Down)];
            }
            float down_face_flow_m3_per_s =
                -outgoing_flux_m3_per_s_[index][static_cast<std::size_t>(Fluid25DFace::Down)];
            if (down != kFluid25DNoCell) {
                down_face_flow_m3_per_s +=
                    outgoing_flux_m3_per_s_[down][static_cast<std::size_t>(Fluid25DFace::Up)];
            }

            const float cross_section_m2 = depth_m * cell_size_m;
            velocity.x_m_per_s =
                0.5F * (right_face_flow_m3_per_s + left_face_flow_m3_per_s) / cross_section_m2;
            velocity.y_m_per_s =
                0.5F * (up_face_flow_m3_per_s + down_face_flow_m3_per_s) / cross_section_m2;
            if (!std::isfinite(velocity.x_m_per_s) || !std::isfinite(velocity.y_m_per_s)) {
                throw std::runtime_error("fluid 2.5D derived velocity is nonfinite");
            }
        }
    }
}

void Fluid25DOracle::reset() {
    water_depth_m_ = scenario_.initial_water_depth_m;
    std::fill(next_water_depth_m_.begin(), next_water_depth_m_.end(), 0.0F);
    for (Fluid25DFaceFlux& flux : outgoing_flux_m3_per_s_) {
        flux.fill(0.0F);
    }
    derive_velocity_and_wet_state();
}

} // namespace cubey::projects::fluid::fluid_25d
