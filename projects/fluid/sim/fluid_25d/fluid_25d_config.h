#pragma once

#include "../common/fluid_config_schema.h"

#include <cubey/host/common_config.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace cubey::projects::fluid::fluid_25d {

// River V0 deliberately names the physical quantities used by the numerical
// contract.  A source or sink rate is a change in water depth per second over
// one cell (m/s); the oracle converts it to a volume rate with cell area (m2)
// before it enters the ledger.
enum class Fluid25DScenario : std::uint32_t {
    DryBed = 0,
    LakeAtRest = 1,
    RiverCatchment = 2,
};

// This compact 2:1 grid is for the numerical/oracle phase only.  A future
// River V0 runtime should choose its bounded product grid independently (a
// roughly 256x128 catchment is the current direction).
inline constexpr std::uint32_t kDefaultFluid25DGridWidth = 32;
inline constexpr std::uint32_t kDefaultFluid25DGridHeight = 16;
inline constexpr std::uint32_t kMaxFluid25DSubsteps = 64;

struct Fluid25DConfig {
    std::uint32_t grid_width = kDefaultFluid25DGridWidth;
    std::uint32_t grid_height = kDefaultFluid25DGridHeight;

    // All spatial quantities are metres and all rates below are per-second.
    float cell_size_m = 1.0F;
    float fixed_delta_seconds = 1.0F / 60.0F;
    std::uint32_t simulation_substeps = 2;
    float gravity_m_per_s2 = 9.81F;
    float flow_damping_per_second = 0.15F;
    float minimum_wet_depth_m = 0.0001F;

    Fluid25DScenario scenario = Fluid25DScenario::RiverCatchment;
};

// Startup options retain "unset" separately from the concrete, validated
// runtime values.  This is the same Config V2 shape used by the active fluid
// projects, but remains project-local until a runtime target exists.
struct Fluid25DStartupOptions {
    std::optional<float> cell_size_m{};
    std::optional<float> fixed_delta_seconds{};
    std::optional<std::uint32_t> simulation_substeps{};
    std::optional<float> gravity_m_per_s2{};
    std::optional<float> flow_damping_per_second{};
    std::optional<float> minimum_wet_depth_m{};
    std::optional<std::string> scenario{};
};

[[nodiscard]] inline const char* fluid_25d_scenario_name(Fluid25DScenario scenario) {
    switch (scenario) {
    case Fluid25DScenario::DryBed:
        return "dry-bed";
    case Fluid25DScenario::LakeAtRest:
        return "lake-at-rest";
    case Fluid25DScenario::RiverCatchment:
        return "river-catchment";
    }
    return "river-catchment";
}

[[nodiscard]] inline Fluid25DScenario fluid_25d_scenario_from_name(std::string_view name) {
    if (name.empty() || name == "river" || name == "river-catchment") {
        return Fluid25DScenario::RiverCatchment;
    }
    if (name == "dry" || name == "dry-bed") {
        return Fluid25DScenario::DryBed;
    }
    if (name == "lake" || name == "lake-at-rest") {
        return Fluid25DScenario::LakeAtRest;
    }
    throw std::runtime_error(
        "fluid 2.5D scenario must be dry-bed, lake-at-rest, or river-catchment");
}

[[nodiscard]] inline std::size_t fluid_25d_cell_count(const Fluid25DConfig& config) {
    if (config.grid_width == 0 || config.grid_height == 0) {
        throw std::runtime_error("fluid 2.5D grid dimensions must be positive");
    }
    const std::size_t width = static_cast<std::size_t>(config.grid_width);
    const std::size_t height = static_cast<std::size_t>(config.grid_height);
    if (width > std::numeric_limits<std::size_t>::max() / height) {
        throw std::runtime_error("fluid 2.5D grid dimensions are too large");
    }
    return width * height;
}

inline void validate_fluid_25d_config(const Fluid25DConfig& config) {
    static_cast<void>(fluid_25d_cell_count(config));
    if (config.scenario != Fluid25DScenario::DryBed &&
        config.scenario != Fluid25DScenario::LakeAtRest &&
        config.scenario != Fluid25DScenario::RiverCatchment) {
        throw std::runtime_error("fluid 2.5D scenario value is invalid");
    }
    if (!(config.cell_size_m > 0.0F) || !std::isfinite(config.cell_size_m)) {
        throw std::runtime_error("fluid 2.5D cell size must be finite and positive");
    }
    if (!(config.fixed_delta_seconds > 0.0F) || !std::isfinite(config.fixed_delta_seconds)) {
        throw std::runtime_error("fluid 2.5D fixed delta must be finite and positive");
    }
    if (config.simulation_substeps == 0 || config.simulation_substeps > kMaxFluid25DSubsteps) {
        throw std::runtime_error("fluid 2.5D substeps must be in 1..64");
    }
    if (!(config.gravity_m_per_s2 > 0.0F) || !std::isfinite(config.gravity_m_per_s2)) {
        throw std::runtime_error("fluid 2.5D gravity must be finite and positive");
    }
    if (config.flow_damping_per_second < 0.0F || !std::isfinite(config.flow_damping_per_second)) {
        throw std::runtime_error("fluid 2.5D flow damping must be finite and nonnegative");
    }
    if (config.minimum_wet_depth_m < 0.0F || !std::isfinite(config.minimum_wet_depth_m)) {
        throw std::runtime_error("fluid 2.5D minimum wet depth must be finite and nonnegative");
    }
}

[[nodiscard]] inline Fluid25DConfig
fluid_25d_config_from_options(const common::FluidGridOptions& grid,
                              const Fluid25DStartupOptions& options) {
    Fluid25DConfig config;
    if (grid.width) {
        config.grid_width = *grid.width;
    }
    if (grid.height) {
        config.grid_height = *grid.height;
    }
    if (options.cell_size_m) {
        config.cell_size_m = *options.cell_size_m;
    }
    if (options.fixed_delta_seconds) {
        config.fixed_delta_seconds = *options.fixed_delta_seconds;
    }
    if (options.simulation_substeps) {
        config.simulation_substeps = *options.simulation_substeps;
    }
    if (options.gravity_m_per_s2) {
        config.gravity_m_per_s2 = *options.gravity_m_per_s2;
    }
    if (options.flow_damping_per_second) {
        config.flow_damping_per_second = *options.flow_damping_per_second;
    }
    if (options.minimum_wet_depth_m) {
        config.minimum_wet_depth_m = *options.minimum_wet_depth_m;
    }
    config.scenario = fluid_25d_scenario_from_name(options.scenario.value_or(""));
    validate_fluid_25d_config(config);
    return config;
}

} // namespace cubey::projects::fluid::fluid_25d
