#pragma once

#include "atmosphere_config.h"

#include <cubey/engine/atmosphere_environment_schema.h>
#include <cubey/engine/cloud_environment_schema.h>
#include <cubey/host/configured_app.h>

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace cubey::projects::atmosphere {
namespace detail {

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

} // namespace detail

using config::OptionSpec;
using config::ValueType;

struct AtmosphereProjectConfig {
    host::CommonRunConfig common;
    AtmosphereStartupOptions options;
    AtmosphereConfig atmosphere;
};

inline config::Schema atmosphere_project_config_schema(AtmosphereProjectConfig& config) {
    auto builder = config::Schema::builder().compose(host::common_run_config_schema(config.common));
    builder.compose(cubey::atmosphere_environment_schema(config.options.atmosphere));
    builder.bind(detail::option("atmosphere.preset", "--atmosphere-preset", "Preset",
                                "Atmosphere", "Atmosphere preset name.", ValueType::String),
                 config.options.atmosphere.preset);
    builder.bind(detail::option("atmosphere.milky_way_layer", "--milky-way-layer",
                                "Milky Way Layer", "Atmosphere",
                                "Generated Milky Way atlas layer to inspect.", ValueType::Enum, {},
                                {"final", "stellar-emission", "dust-tau", "star-clouds",
                                 "hii-emission", "speckles"}),
                 config.options.atmosphere.milky_way_layer);
    builder.bind(detail::option("atmosphere.milky_way_variation", "--milky-way-variation",
                                "Milky Way Variation", "Atmosphere",
                                "Procedural variation phase for Milky Way generation.",
                                ValueType::Float,
                                {.has_min = true, .has_max = true, .min = 0.0, .max = 16.0}),
                 config.options.atmosphere.milky_way_variation);
    OptionSpec reference = detail::option(
        "atmosphere.reference_geometry", "--reference-geometry", "Reference Geometry",
        "Atmosphere", "Enable the standalone atmosphere ground reference grid.", ValueType::Bool);
    reference.negative_cli_name = "--no-reference-geometry";
    builder.bind(std::move(reference), config.options.atmosphere.reference_geometry);
    builder.compose(cubey::cloud_environment_schema(config.options.clouds));
    builder.compose(cubey::pbr_exposure_schema(config.options.pbr));
    builder.bind(detail::option("debug_view", "--debug-view", "Debug View", "Atmosphere",
                                "Atmosphere render view.", ValueType::String),
                 config.options.debug_view);
    return std::move(builder).build();
}

inline AtmosphereProjectConfig parse_atmosphere_project_config(
    int argc, char** argv, config::ParseResult* result = nullptr) {
    AtmosphereProjectConfig config = host::parse_configured_app<AtmosphereProjectConfig>(
        argc, argv, atmosphere_project_config_schema, result);
    validate_atmosphere_environment_options(config.options.atmosphere);
    validate_cloud_environment_options(config.options.clouds);
    config.atmosphere = atmosphere_config_from_options(config.options);
    return config;
}

} // namespace cubey::projects::atmosphere
