#pragma once

#include "../sim/fluid_25d/fluid_25d_config.h"

#include <cubey/host/common_config.h>

#include <string>
#include <utility>
#include <vector>

namespace cubey::projects::fluid::fluid_25d {

struct Fluid25DProjectConfig {
    host::CommonRunConfig common{};
    common::FluidGridOptions grid{};
    std::string view{};
    std::string debug_view{};
    bool gpu_oracle_validation = false;
    Fluid25DStartupOptions fluid{};
    Fluid25DConfig simulation{};
};

namespace fluid_25d_project_config_detail {

inline config::OptionSpec option(std::string path, std::string cli, std::string label,
                                 std::string help, config::ValueType type, config::Range range = {},
                                 std::vector<std::string> choices = {}) {
    return {.path = std::move(path),
            .cli_name = std::move(cli),
            .negative_cli_name = {},
            .label = std::move(label),
            .group_path = "Fluid 2.5D",
            .help = std::move(help),
            .type = type,
            .range = range,
            .enum_values = std::move(choices)};
}

} // namespace fluid_25d_project_config_detail

[[nodiscard]] inline config::Schema fluid_25d_project_config_schema(Fluid25DProjectConfig& config) {
    using config::ValueType;
    using fluid_25d_project_config_detail::option;

    auto builder = config::Schema::builder().compose(host::common_run_config_schema(config.common));
    builder.compose(common::fluid_grid_schema(config.grid, common::FluidGridSchemaMode::TwoD));
    builder
        .bind(option("fluid25d.view", "--fluid25d-view", "View",
                     "River V0 presentation: catchment or diagnostics.", ValueType::Enum, {},
                     {"catchment", "diagnostics"}),
              config.view)
        .bind(option(
                  "fluid25d.debug_view", "--debug-view", "Debug View",
                  "Top-down diagnostic view: terrain, depth, surface, flow, direction, or wet-dry.",
                  ValueType::Enum, {},
                  {"terrain", "depth", "surface", "flow", "direction", "wet-dry"}),
              config.debug_view)
        .bind(
            option("fluid25d.gpu_oracle_validation", "--fluid25d-gpu-oracle-validation",
                   "GPU Oracle Validation",
                   "Headless-only opt-in CPU/GPU solver comparison with simulation-state readback.",
                   ValueType::Bool),
            config.gpu_oracle_validation)
        .bind(option("fluid25d.scenario", "--fluid25d-scenario", "Scenario",
                     "Deterministic River V0 scenario.", ValueType::Enum, {},
                     {"dry-bed", "lake-at-rest", "river-catchment"}),
              config.fluid.scenario)
        .bind(option("fluid25d.cell_size_m", "--fluid25d-cell-size-m", "Cell Size",
                     "Simulation cell width in metres.", ValueType::Float,
                     {.has_min = true, .min = 0.000001}),
              config.fluid.cell_size_m)
        .bind(option("fluid25d.fixed_delta_seconds", "--fluid25d-fixed-delta-seconds",
                     "Fixed Delta", "Fixed simulation frame duration in seconds.", ValueType::Float,
                     {.has_min = true, .min = 0.000001}),
              config.fluid.fixed_delta_seconds)
        .bind(option("fluid25d.simulation_substeps", "--fluid25d-substeps", "Substeps",
                     "Fixed solver substeps per simulation frame.", ValueType::UInt32,
                     {.has_min = true,
                      .has_max = true,
                      .min = 1.0,
                      .max = static_cast<double>(kMaxFluid25DSubsteps)}),
              config.fluid.simulation_substeps)
        .bind(option("fluid25d.gravity_m_per_s2", "--fluid25d-gravity-m-per-s2", "Gravity",
                     "Gravitational acceleration in metres per second squared.", ValueType::Float,
                     {.has_min = true, .min = 0.000001}),
              config.fluid.gravity_m_per_s2)
        .bind(option("fluid25d.flow_damping_per_second", "--fluid25d-flow-damping-per-second",
                     "Flow Damping", "Flow damping rate in inverse seconds.", ValueType::Float,
                     {.has_min = true, .min = 0.0}),
              config.fluid.flow_damping_per_second)
        .bind(option("fluid25d.minimum_wet_depth_m", "--fluid25d-minimum-wet-depth-m",
                     "Minimum Wet Depth",
                     "Numerical wet/dry depth threshold used by flux evolution, in metres.",
                     ValueType::Float, {.has_min = true, .min = 0.0}),
              config.fluid.minimum_wet_depth_m);
    return std::move(builder).build();
}

[[nodiscard]] inline Fluid25DProjectConfig
parse_fluid_25d_project_config(int argc, char** argv, config::ParseResult* result = nullptr) {
    Fluid25DProjectConfig project_config;
    config::Schema schema = fluid_25d_project_config_schema(project_config);
    config::ParseResult parsed = schema.parse_cli(argc, argv);
    host::normalize_common_run_config(project_config.common,
                                      parsed.path_was_assigned("output") ||
                                          project_config.common.output_path != "cubey-output.png");
    project_config.simulation =
        fluid_25d_config_from_options(project_config.grid, project_config.fluid);
    static_cast<void>(fluid_25d_presentation_view_from_name(project_config.view));
    static_cast<void>(fluid_25d_debug_view_from_name(project_config.debug_view));
    if (project_config.gpu_oracle_validation && !project_config.common.headless) {
        throw std::runtime_error(
            "fluid 2.5D GPU oracle validation is headless-only; pass --headless");
    }
    if (parsed.write_config_template_path.has_value()) {
        schema.write_template(parsed.write_config_template_path.value());
    }
    if (result != nullptr) {
        *result = std::move(parsed);
    }
    return project_config;
}

} // namespace cubey::projects::fluid::fluid_25d
