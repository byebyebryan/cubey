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

// Presentation-only diagnostic views. They never alter the River V0 solver
// contract or its persistent GPU state.
enum class Fluid25DDebugView : std::uint32_t {
    Terrain = 0,
    WaterDepth = 1,
    SurfaceHeight = 2,
    FlowMagnitude = 3,
    FlowDirection = 4,
    WetDry = 5,
};

// River V0 has one product-facing view and a deliberately separate top-down
// diagnostics surface. Keeping this as an enum makes headless captures and
// windowed interaction reproducible without a hidden presentation toggle.
enum class Fluid25DPresentationView : std::uint32_t {
    Catchment = 0,
    Diagnostics = 1,
};

// The River V0 product default is a bounded 2:1 catchment. Focused CPU/GPU
// oracle fixtures deliberately override this with much smaller grids.
inline constexpr std::uint32_t kDefaultFluid25DGridWidth = 256;
inline constexpr std::uint32_t kDefaultFluid25DGridHeight = 128;
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

[[nodiscard]] inline Fluid25DDebugView fluid_25d_debug_view_from_name(std::string_view name) {
    if (name.empty() || name == "terrain") {
        return Fluid25DDebugView::Terrain;
    }
    if (name == "depth" || name == "water-depth") {
        return Fluid25DDebugView::WaterDepth;
    }
    if (name == "surface" || name == "surface-height") {
        return Fluid25DDebugView::SurfaceHeight;
    }
    if (name == "flow" || name == "flow-magnitude") {
        return Fluid25DDebugView::FlowMagnitude;
    }
    if (name == "direction" || name == "flow-direction") {
        return Fluid25DDebugView::FlowDirection;
    }
    if (name == "wet-dry" || name == "wetdry") {
        return Fluid25DDebugView::WetDry;
    }
    throw std::runtime_error("fluid 2.5D debug view must be terrain, depth, surface, flow, "
                             "direction, or wet-dry");
}

[[nodiscard]] inline Fluid25DPresentationView
fluid_25d_presentation_view_from_name(std::string_view name) {
    if (name.empty() || name == "catchment") {
        return Fluid25DPresentationView::Catchment;
    }
    if (name == "diagnostics" || name == "diagnostic") {
        return Fluid25DPresentationView::Diagnostics;
    }
    throw std::runtime_error("fluid 2.5D view must be catchment or diagnostics");
}

[[nodiscard]] inline const char* fluid_25d_presentation_view_name(Fluid25DPresentationView view) {
    switch (view) {
    case Fluid25DPresentationView::Catchment:
        return "Catchment";
    case Fluid25DPresentationView::Diagnostics:
        return "Diagnostics";
    }
    return "Catchment";
}

[[nodiscard]] inline const char* fluid_25d_debug_view_name(Fluid25DDebugView view) {
    switch (view) {
    case Fluid25DDebugView::Terrain:
        return "Terrain";
    case Fluid25DDebugView::WaterDepth:
        return "Water Depth";
    case Fluid25DDebugView::SurfaceHeight:
        return "Surface Height";
    case Fluid25DDebugView::FlowMagnitude:
        return "Flow Magnitude";
    case Fluid25DDebugView::FlowDirection:
        return "Flow Direction";
    case Fluid25DDebugView::WetDry:
        return "Wet / Dry";
    }
    return "Terrain";
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

[[nodiscard]] inline std::size_t fluid_25d_mesh_vertex_count(const Fluid25DConfig& config) {
    if (config.grid_width < 2U || config.grid_height < 2U) {
        throw std::runtime_error("fluid 2.5D product mesh requires grid dimensions of at least 2");
    }
    const std::size_t cells_x = static_cast<std::size_t>(config.grid_width - 1U);
    const std::size_t cells_y = static_cast<std::size_t>(config.grid_height - 1U);
    if (cells_x > std::numeric_limits<std::size_t>::max() / cells_y) {
        throw std::runtime_error("fluid 2.5D product mesh cell count is too large");
    }
    const std::size_t quad_count = cells_x * cells_y;
    if (quad_count > std::numeric_limits<std::size_t>::max() / 6U) {
        throw std::runtime_error("fluid 2.5D product mesh vertex count is too large");
    }
    return quad_count * 6U;
}

inline void validate_fluid_25d_config(const Fluid25DConfig& config) {
    static_cast<void>(fluid_25d_cell_count(config));
    static_cast<void>(fluid_25d_mesh_vertex_count(config));
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
