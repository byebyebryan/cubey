#pragma once

#include "pyro_3d_config.h"

#include "../common/fluid_config_schema.h"

#include <cubey/engine/atmosphere_environment_config.h>
#include <cubey/engine/atmosphere_environment_schema.h>
#include <cubey/engine/cloud_environment_config.h>
#include <cubey/engine/cloud_environment_schema.h>
#include <cubey/engine/pbr_environment_schema.h>
#include <cubey/host/configured_app.h>

#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace cubey::projects::fluid::pyro_3d {

struct Pyro3DPbrOptions : cubey::PbrStaticIblOptions {
    std::optional<std::string> environment_source{};
};

struct Pyro3DTerrainOptions {
    std::optional<std::filesystem::path> heightfield_path{};
    std::optional<std::uint32_t> render_stride{};
    std::optional<float> foreground_height_m{};
};

struct Pyro3DProjectConfig {
    host::CommonRunConfig common{};
    common::FluidGridOptions grid{};
    std::string debug_view{};
    Pyro3DStartupOptions pyro{};
    cubey::AtmosphereEnvironmentOptions atmosphere{};
    cubey::CloudEnvironmentOptions clouds{};
    Pyro3DPbrOptions pbr{};
    Pyro3DTerrainOptions terrain{};
    Pyro3DConfig simulation{};
};

namespace pyro_3d_project_config_detail {

using config::OptionSpec;
using config::ValueType;

inline OptionSpec option(std::string path, std::string cli, std::string label,
                         std::string group, std::string help, ValueType type,
                         config::Range range = {}, std::vector<std::string> choices = {}) {
    return {.path = std::move(path),
            .cli_name = std::move(cli),
            .negative_cli_name = {},
            .label = std::move(label),
            .group_path = std::move(group),
            .help = std::move(help),
            .type = type,
            .range = range,
            .enum_values = std::move(choices)};
}

} // namespace pyro_3d_project_config_detail

[[nodiscard]] inline config::Schema pyro_3d_project_config_schema(
    Pyro3DProjectConfig& config) {
    using config::ValueType;
    using pyro_3d_project_config_detail::option;

    auto builder = config::Schema::builder().compose(host::common_run_config_schema(config.common));
    builder.compose(common::fluid_grid_schema(config.grid, common::FluidGridSchemaMode::ThreeD));
    builder.bind(option("debug_view", "--debug-view", "Debug View", "Debug",
                        "Pyro 3D debug view: smoke, density-slice, or velocity.",
                        ValueType::String),
                 config.debug_view);

    constexpr config::Range positive{.has_min = true, .min = 1.0};
    constexpr config::Range nonnegative{.has_min = true, .min = 0.0};
    constexpr config::Range unit{.has_min = true, .has_max = true, .min = 0.0, .max = 1.0};
    constexpr config::Range radius{.has_min = true, .has_max = true, .min = 0.0, .max = 0.5};
    constexpr config::Range positive_float{.has_min = true, .min = 0.000001};

    builder
        .bind(option("pyro.shadow_grid.width", "--shadow-grid-width", "Shadow Width", "Pyro 3D",
                     "Shadow-volume grid width.", ValueType::UInt32, positive),
              config.pyro.shadow_grid_width)
        .bind(option("pyro.shadow_grid.height", "--shadow-grid-height", "Shadow Height",
                     "Pyro 3D", "Shadow-volume grid height.", ValueType::UInt32, positive),
              config.pyro.shadow_grid_height)
        .bind(option("pyro.shadow_grid.depth", "--shadow-grid-depth", "Shadow Depth", "Pyro 3D",
                     "Shadow-volume grid depth.", ValueType::UInt32, positive),
              config.pyro.shadow_grid_depth)
        .bind(option("pyro.shadow_steps", "--shadow-steps", "Shadow Steps", "Pyro 3D",
                     "Raymarch steps for volumetric shadowing.", ValueType::UInt32, positive),
              config.pyro.shadow_steps)
        .bind(option("pyro.shadow_update_interval", "--shadow-update-interval",
                     "Shadow Update Interval", "Pyro 3D",
                     "Frame interval for updating the shadow volume.", ValueType::UInt32, positive),
              config.pyro.shadow_update_interval)
        .bind(option("pyro.sources", "--pyro-sources", "Sources", "Pyro 3D",
                     "Number of pyro source emitters.", ValueType::UInt32, positive),
              config.pyro.sources)
        .bind(option("pyro.source_height", "--pyro-source-height", "Source Height", "Pyro 3D",
                     "Source height in normalized volume coordinates.", ValueType::Float, unit),
              config.pyro.source_height)
        .bind(option("pyro.source_radius", "--pyro-source-radius", "Source Radius", "Pyro 3D",
                     "Source radius in normalized volume coordinates.", ValueType::Float,
                     positive_float),
              config.pyro.source_radius)
        .bind(option("pyro.source_force", "--pyro-source-force", "Source Force", "Pyro 3D",
                     "Velocity force injected by pyro sources.", ValueType::Float, nonnegative),
              config.pyro.source_force)
        .bind(option("pyro.soot", "--pyro-soot", "Soot", "Pyro 3D",
                     "Soot or smoke amount injected by sources.", ValueType::Float, nonnegative),
              config.pyro.soot)
        .bind(option("pyro.temperature", "--pyro-temperature", "Temperature", "Pyro 3D",
                     "Temperature injected by sources.", ValueType::Float, nonnegative),
              config.pyro.temperature)
        .bind(option("pyro.fuel", "--pyro-fuel", "Fuel", "Pyro 3D",
                     "Fuel injected by sources.", ValueType::Float, nonnegative),
              config.pyro.fuel)
        .bind(option("pyro.buoyancy", "--pyro-buoyancy", "Buoyancy", "Pyro 3D",
                     "Thermal buoyancy strength.", ValueType::Float, nonnegative),
              config.pyro.buoyancy)
        .bind(option("pyro.ignition_temperature", "--pyro-ignition-temperature", "Ignition",
                     "Pyro 3D", "Temperature threshold for combustion.", ValueType::Float,
                     nonnegative),
              config.pyro.ignition_temperature)
        .bind(option("pyro.burn_rate", "--pyro-burn-rate", "Burn Rate", "Pyro 3D",
                     "Fuel burn rate.", ValueType::Float, nonnegative),
              config.pyro.burn_rate)
        .bind(option("pyro.heat_output", "--pyro-heat-output", "Heat Output", "Pyro 3D",
                     "Heat produced by combustion.", ValueType::Float, nonnegative),
              config.pyro.heat_output)
        .bind(option("pyro.soot_yield", "--pyro-soot-yield", "Soot Yield", "Pyro 3D",
                     "Soot produced by combustion.", ValueType::Float, nonnegative),
              config.pyro.soot_yield)
        .bind(option("pyro.expansion", "--pyro-expansion", "Expansion", "Pyro 3D",
                     "Combustion expansion force.", ValueType::Float, nonnegative),
              config.pyro.expansion)
        .bind(option("pyro.flame_cooling", "--pyro-flame-cooling", "Flame Cooling", "Pyro 3D",
                     "Cooling rate for visible flame.", ValueType::Float, nonnegative),
              config.pyro.flame_cooling)
        .bind(option("pyro.shredding", "--pyro-shredding", "Shredding", "Pyro 3D",
                     "Small-scale flame breakup strength.", ValueType::Float, nonnegative),
              config.pyro.shredding)
        .bind(option("pyro.turbulence", "--pyro-turbulence", "Turbulence", "Pyro 3D",
                     "Source turbulence amount.", ValueType::Float, nonnegative),
              config.pyro.turbulence)
        .bind(option("pyro.obstacle_height", "--pyro-obstacle-height", "Obstacle Height",
                     "Pyro 3D", "Ball obstacle center height.", ValueType::Float, unit),
              config.pyro.obstacle_height)
        .bind(option("pyro.obstacle_radius", "--pyro-obstacle-radius", "Obstacle Radius",
                     "Pyro 3D", "Ball obstacle radius.", ValueType::Float, radius),
              config.pyro.obstacle_radius)
        .bind(option("pyro.explosion_interval_seconds", "--explosion-interval",
                     "Explosion Interval", "Pyro 3D", "Seconds between explosion impulses.",
                     ValueType::Float, positive_float),
              config.pyro.explosion_interval_seconds)
        .bind(option("pyro.explosion_duration_seconds", "--explosion-duration",
                     "Explosion Duration", "Pyro 3D", "Seconds spent in the explosion impulse.",
                     ValueType::Float, positive_float),
              config.pyro.explosion_duration_seconds)
        .bind(option("pyro.explosion_boost", "--explosion-boost", "Explosion Boost", "Pyro 3D",
                     "Impulse multiplier for explosion mode.", ValueType::Float, nonnegative),
              config.pyro.explosion_boost);

    builder.compose(cubey::pbr_static_ibl_schema(config.pbr));
    builder.bind(option("pbr.environment_source", "--pbr-environment-source", "Environment Source",
                        "PBR", "Choose static IBL or the procedural atmosphere environment.",
                        ValueType::Enum, {}, {"static", "atmosphere"}),
                 config.pbr.environment_source);
    builder.compose(cubey::atmosphere_environment_schema(config.atmosphere));
    builder.compose(cubey::cloud_environment_schema(config.clouds));

    builder
        .bind(option("terrain.heightfield", "--terrain-heightfield", "Heightfield", "Terrain",
                     "Terrain backdrop heightfield.", ValueType::Path),
              config.terrain.heightfield_path)
        .bind(option("terrain.render_stride", "--terrain-render-stride", "Render Stride",
                     "Terrain", "Cached topology stride used for terrain backdrop geometry.",
                     ValueType::UInt32,
                     {.has_min = true, .has_max = true, .min = 1.0, .max = 3.0}),
              config.terrain.render_stride)
        .bind(option("terrain.foreground_height_m", "--terrain-foreground-height",
                     "Foreground Height", "Terrain", "Terrain foreground height.",
                     ValueType::Float,
                     {.has_min = true, .has_max = true, .min = 0.0, .max = 1000.0}),
              config.terrain.foreground_height_m);
    return std::move(builder).build();
}

[[nodiscard]] inline Pyro3DProjectConfig parse_pyro_3d_project_config(
    int argc, char** argv, Pyro3DMode mode, config::ParseResult* result = nullptr) {
    Pyro3DProjectConfig config = host::parse_configured_app<Pyro3DProjectConfig>(
        argc, argv, pyro_3d_project_config_schema, result);
    validate_atmosphere_environment_options(config.atmosphere);
    validate_cloud_environment_options(config.clouds);
    config.simulation = pyro_3d_config_from_options(config.grid, config.pyro, mode, config.common);
    return config;
}

[[nodiscard]] inline Pyro3DProjectConfig parse_pyro_3d_project_config(
    int argc, char** argv, config::ParseResult* result = nullptr) {
    return parse_pyro_3d_project_config(argc, argv, Pyro3DMode::Fire, result);
}

} // namespace cubey::projects::fluid::pyro_3d
