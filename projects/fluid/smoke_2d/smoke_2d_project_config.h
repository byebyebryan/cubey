#pragma once

#include "../sim/smoke_2d/smoke_2d_config.h"

#include <cubey/host/common_config.h>

#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace cubey::projects::fluid::smoke_2d {

struct Smoke2DProjectConfig {
    host::CommonRunConfig common{};
    common::FluidGridOptions grid{};
    std::string debug_view{};
    Smoke2DStartupOptions smoke{};
    Smoke2DConfig simulation{};
};

namespace smoke_2d_project_config_detail {

inline config::OptionSpec option(std::string path, std::string cli, std::string label,
                                 std::string help, config::ValueType type,
                                 config::Range range = {},
                                 std::vector<std::string> choices = {}) {
    return {.path = std::move(path),
            .cli_name = std::move(cli),
            .negative_cli_name = {},
            .label = std::move(label),
            .group_path = "Smoke 2D",
            .help = std::move(help),
            .type = type,
            .range = range,
            .enum_values = std::move(choices)};
}

} // namespace smoke_2d_project_config_detail

[[nodiscard]] inline config::Schema smoke_2d_project_config_schema(
    Smoke2DProjectConfig& config) {
    using config::ValueType;
    using smoke_2d_project_config_detail::option;

    auto builder = config::Schema::builder().compose(host::common_run_config_schema(config.common));
    builder.compose(common::fluid_grid_schema(config.grid, common::FluidGridSchemaMode::TwoD));
    builder
        .bind({.path = "debug_view",
               .cli_name = "--debug-view",
               .negative_cli_name = {},
               .label = "Debug View",
               .group_path = "Debug",
               .help = "Smoke 2D debug view: dye, velocity, divergence, pressure, speed, or vorticity.",
               .type = ValueType::String,
               .range = {},
               .enum_values = {}},
              config.debug_view)
        .bind(option("smoke.injectors", "--smoke-injectors", "Injectors",
                     "Number of built-in smoke injectors.", ValueType::UInt32,
                     {.has_min = true, .min = 1.0}),
              config.smoke.injectors)
        .bind(option("smoke.pressure_iterations", "--smoke-pressure-iterations",
                     "Pressure Iterations", "Pressure projection iteration count.",
                     ValueType::UInt32, {.has_min = true, .min = 1.0}),
              config.smoke.pressure_iterations)
        .bind(option("smoke.pressure_solver", "--smoke-pressure-solver", "Pressure Solver",
                     "Pressure solver implementation.", ValueType::Enum, {},
                     {"jacobi", "rbgs", "red-black-gauss-seidel"}),
              config.smoke.pressure_solver)
        .bind(option("smoke.dye_decay", "--smoke-dye-decay", "Dye Decay",
                     "Per-frame dye retention.", ValueType::Float,
                     {.has_min = true, .has_max = true, .min = 0.0, .max = 1.0}),
              config.smoke.dye_decay)
        .bind(option("smoke.velocity_decay", "--smoke-velocity-decay", "Velocity Decay",
                     "Per-frame velocity retention.", ValueType::Float,
                     {.has_min = true, .has_max = true, .min = 0.0, .max = 1.0}),
              config.smoke.velocity_decay)
        .bind(option("smoke.injector_radius", "--smoke-injector-radius", "Injector Radius",
                     "Smoke injector radius in normalized simulation space.", ValueType::Float,
                     {.has_min = true, .min = 0.000001}),
              config.smoke.injector_radius)
        .bind(option("smoke.injector_force", "--smoke-injector-force", "Injector Force",
                     "Force applied by smoke injectors.", ValueType::Float,
                     {.has_min = true, .min = 0.0}),
              config.smoke.injector_force)
        .bind(option("smoke.injector_propulsion", "--smoke-injector-propulsion",
                     "Injector Propulsion", "Propulsion applied opposite injector motion.",
                     ValueType::Float, {.has_min = true, .min = 0.0}),
              config.smoke.injector_propulsion)
        .bind(option("smoke.injector_orbit_radius", "--smoke-injector-orbit-radius",
                     "Orbit Radius", "Base injector orbit radius.", ValueType::Float,
                     {.has_min = true, .min = 0.000001}),
              config.smoke.injector_orbit_radius)
        .bind(option("smoke.injector_orbit_radius_spread", "--smoke-injector-orbit-radius-spread",
                     "Orbit Radius Spread", "Injector orbit radius variation.", ValueType::Float,
                     {.has_min = true, .min = 0.0}),
              config.smoke.injector_orbit_radius_spread)
        .bind(option("smoke.injector_orbit_angular_speed", "--smoke-injector-orbit-angular-speed",
                     "Orbit Speed", "Base injector angular speed.", ValueType::Float),
              config.smoke.injector_orbit_angular_speed)
        .bind(option("smoke.injector_orbit_angular_speed_spread",
                     "--smoke-injector-orbit-angular-speed-spread", "Orbit Speed Spread",
                     "Injector angular speed variation.", ValueType::Float,
                     {.has_min = true, .min = 0.0}),
              config.smoke.injector_orbit_angular_speed_spread)
        .bind(option("smoke.injector_orbit_phase_spread", "--smoke-injector-orbit-phase-spread",
                     "Orbit Phase Spread", "Injector orbit phase variation.", ValueType::Float,
                     {.has_min = true, .min = 0.0}),
              config.smoke.injector_orbit_phase_spread)
        .bind(option("smoke.vorticity", "--smoke-vorticity", "Vorticity",
                     "Vorticity confinement strength.", ValueType::Float,
                     {.has_min = true, .min = 0.0}),
              config.smoke.vorticity);
    return std::move(builder).build();
}

[[nodiscard]] inline Smoke2DProjectConfig parse_smoke_2d_project_config(
    int argc, char** argv, config::ParseResult* result = nullptr) {
    Smoke2DProjectConfig config;
    config::Schema schema = smoke_2d_project_config_schema(config);
    config::ParseResult parsed = schema.parse_cli(argc, argv);
    host::normalize_common_run_config(
        config.common,
        parsed.path_was_assigned("output") || config.common.output_path != "cubey-output.png");
    config.simulation = smoke_2d_config_from_options(config.grid, config.smoke, config.common);
    if (parsed.write_config_template_path.has_value()) {
        schema.write_template(parsed.write_config_template_path.value());
    }
    if (result != nullptr) {
        *result = std::move(parsed);
    }
    return config;
}

} // namespace cubey::projects::fluid::smoke_2d
