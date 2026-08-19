#pragma once

#include "cloud_ref_config.h"

#include <cubey/engine/atmosphere_environment_schema.h>
#include <cubey/engine/cloud_environment_schema.h>
#include <cubey/host/configured_app.h>

#include <string>
#include <utility>
#include <vector>

namespace cubey::projects::cloud_ref {
namespace detail {

using config::OptionSpec;
using config::ValueType;

inline OptionSpec option(std::string path, std::string cli, std::string label,
                         std::string help, ValueType type, config::Range range = {},
                         std::vector<std::string> choices = {}) {
    return {.path = std::move(path),
            .cli_name = std::move(cli),
            .negative_cli_name = {},
            .label = std::move(label),
            .group_path = "Cloud Ref",
            .help = std::move(help),
            .type = type,
            .range = range,
            .enum_values = std::move(choices)};
}

inline void bind_cloud_ref_extras(config::Schema::Builder& builder,
                                  CloudsStartupOptions::Clouds& clouds,
                                  std::string& debug_view) {
    builder.bind(option("debug_view", "--debug-view", "Debug View", "Cloud debug view.",
                        ValueType::String),
                  debug_view);
    builder.bind(option("clouds.camera_mode", "--cloud-camera-mode", "Camera Mode",
                        "Initial camera mode.", ValueType::Enum, {},
                        {"surface", "surface-horizon", "surface-up", "surface-sun", "high",
                         "high-top", "high-oblique", "orbit", "orbit-day", "orbit-terminator"}),
                  clouds.camera_mode);
    builder.bind(option("clouds.resolve_radius_px", "--cloud-resolve-radius-px", "Resolve Radius",
                        "cloud_ref post-resolve blur radius in pixels.", ValueType::Float,
                        {.has_min = true, .has_max = true, .min = 0.0, .max = 8.0}),
                  clouds.resolve_radius_px);
}

} // namespace detail

struct CloudRefProjectConfig {
    host::CommonRunConfig common;
    CloudsStartupOptions options;
    CloudsConfig clouds;
};

inline config::Schema cloud_ref_project_config_schema(CloudRefProjectConfig& config) {
    auto builder = config::Schema::builder().compose(host::common_run_config_schema(config.common));
    builder.compose(cubey::cloud_environment_schema(
        config.options.clouds, cubey::CloudEnvironmentSchemaMode::CloudReference));
    detail::bind_cloud_ref_extras(builder, config.options.clouds, config.options.debug_view);
    builder.compose(cubey::atmosphere_environment_schema(
        config.options.atmosphere, cubey::AtmosphereEnvironmentSchemaMode::CloudReference));
    return std::move(builder).build();
}

inline CloudRefProjectConfig parse_cloud_ref_project_config(int argc, char** argv,
                                                             config::ParseResult* result = nullptr) {
    CloudRefProjectConfig config = host::parse_configured_app<CloudRefProjectConfig>(
        argc, argv, cloud_ref_project_config_schema, result);
    validate_atmosphere_environment_options(config.options.atmosphere);
    validate_cloud_environment_options(config.options.clouds);
    config.clouds = clouds_config_from_options(config.options);
    return config;
}

} // namespace cubey::projects::cloud_ref
