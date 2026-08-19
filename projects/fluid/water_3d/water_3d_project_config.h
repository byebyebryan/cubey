#pragma once

#include "../sim/common/fluid_config_schema.h"
#include "../sim/water_3d/water_3d_config.h"

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

namespace cubey::projects::fluid::water_3d {

struct Water3DPbrOptions : cubey::PbrStaticIblOptions {
    std::optional<std::string> environment_source{};
};

struct Water3DTerrainOptions {
    std::optional<std::filesystem::path> heightfield_path{};
    std::optional<std::uint32_t> render_stride{};
    std::optional<float> foreground_height_m{};
};

struct Water3DProjectConfig {
    host::CommonRunConfig common{};
    common::FluidGridOptions grid{};
    std::string debug_view{};
    Water3DStartupOptions water{};
    cubey::AtmosphereEnvironmentOptions atmosphere{};
    cubey::CloudEnvironmentOptions clouds{};
    Water3DPbrOptions pbr{};
    Water3DTerrainOptions terrain{};
    Water3DConfig simulation{};
};

namespace water_3d_project_config_detail {

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

} // namespace water_3d_project_config_detail

[[nodiscard]] inline config::Schema water_3d_project_config_schema(
    Water3DProjectConfig& config) {
    using config::ValueType;
    using water_3d_project_config_detail::OptionSpec;
    using water_3d_project_config_detail::option;

    auto builder = config::Schema::builder().compose(host::common_run_config_schema(config.common));
    builder.compose(common::fluid_grid_schema(config.grid, common::FluidGridSchemaMode::ThreeD));

    builder.bind(option("debug_view", "--debug-view", "Debug View", "Debug",
                        "Water 3D debug view.", ValueType::String),
                 config.debug_view);
    builder.bind(option("water3d.transfer", "--water3d-transfer", "Transfer", "Water 3D",
                        "Particle-grid transfer mode.", ValueType::Enum, {},
                        {"apic", "pic-flip", "picflip", "pic/flip"}),
                 config.water.transfer_mode);
    builder.bind(option("water3d.transfer_limit", "--water3d-transfer-limit", "Transfer Limit",
                        "Water 3D", "Particle samples consumed per grid cell.",
                        ValueType::UInt32, {.has_min = true, .min = 1.0}),
                 config.water.transfer_limit);
    builder.bind(option("water3d.p2g_mode", "--water3d-p2g-mode", "P2G Mode", "Water 3D",
                        "Particle-to-grid implementation mode.", ValueType::Enum, {},
                        {"active", "active-faces", "tiled", "tiled-faces"}),
                 config.water.p2g_mode);

    OptionSpec hose = option("water3d.hose", "--water3d-hose", "Hose", "Water 3D",
                             "Enable hose injection.", ValueType::Bool);
    hose.negative_cli_name = "--no-water3d-hose";
    builder.bind(std::move(hose), config.water.hose);
    OptionSpec drain = option("water3d.drain", "--water3d-drain", "Drain", "Water 3D",
                              "Enable draining.", ValueType::Bool);
    drain.negative_cli_name = "--no-water3d-drain";
    builder.bind(std::move(drain), config.water.drain);
    OptionSpec rain = option("water3d.rain", "--water3d-rain", "Rain", "Water 3D",
                             "Enable rain injection.", ValueType::Bool);
    rain.negative_cli_name = "--no-water3d-rain";
    builder.bind(std::move(rain), config.water.rain);
    OptionSpec wave = option("water3d.wave", "--water3d-wave", "Wave", "Water 3D",
                             "Enable wave forcing.", ValueType::Bool);
    wave.negative_cli_name = "--no-water3d-wave";
    builder.bind(std::move(wave), config.water.wave);
    OptionSpec whitewater = option("water3d.whitewater", "--water3d-whitewater", "Whitewater",
                                   "Water 3D", "Enable whitewater particles.", ValueType::Bool);
    whitewater.negative_cli_name = "--no-water3d-whitewater";
    builder.bind(std::move(whitewater), config.water.whitewater);

    builder.compose(cubey::pbr_static_ibl_schema(config.pbr));
    builder.bind(option("pbr.environment_source", "--pbr-environment-source", "Environment Source",
                        "PBR", "Choose static IBL or the procedural atmosphere environment.",
                        ValueType::Enum, {}, {"static", "atmosphere"}),
                 config.pbr.environment_source);
    builder.compose(cubey::atmosphere_environment_schema(config.atmosphere));
    builder.compose(cubey::cloud_environment_schema(config.clouds));

    builder.bind(option("terrain.heightfield", "--terrain-heightfield", "Heightfield", "Terrain",
                        "Terrain backdrop heightfield.", ValueType::Path),
                 config.terrain.heightfield_path);
    builder.bind(option("terrain.render_stride", "--terrain-render-stride", "Render Stride",
                        "Terrain", "Cached topology stride used for terrain backdrop geometry.",
                        ValueType::UInt32, {.has_min = true, .has_max = true, .min = 1.0,
                                            .max = 3.0}),
                 config.terrain.render_stride);
    builder.bind(option("terrain.foreground_height_m", "--terrain-foreground-height",
                        "Foreground Height", "Terrain", "Terrain foreground height.",
                        ValueType::Float,
                        {.has_min = true, .has_max = true, .min = 0.0, .max = 1000.0}),
                 config.terrain.foreground_height_m);
    return std::move(builder).build();
}

[[nodiscard]] inline Water3DProjectConfig parse_water_3d_project_config(
    int argc, char** argv, config::ParseResult* result = nullptr) {
    Water3DProjectConfig config = host::parse_configured_app<Water3DProjectConfig>(
        argc, argv, water_3d_project_config_schema, result);
    validate_atmosphere_environment_options(config.atmosphere);
    validate_cloud_environment_options(config.clouds);
    config.simulation = water_3d_config_from_options(config.grid, config.water, config.pbr,
                                                     config.common);
    return config;
}

} // namespace cubey::projects::fluid::water_3d
