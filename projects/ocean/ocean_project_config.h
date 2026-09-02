#pragma once

#include "ocean_config.h"

#include <cubey/engine/atmosphere_environment_schema.h>
#include <cubey/engine/cloud_environment_schema.h>
#include <cubey/engine/ocean_surface_schema.h>
#include <cubey/host/configured_app.h>

#include <nlohmann/json.hpp>

#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace cubey::projects::ocean {

inline constexpr float kOceanMaximumCaptureOrbitDegrees = 180.0F;

namespace detail {

using config::OptionSpec;
using config::ValueType;

inline OptionSpec option(std::string path, std::string cli, std::string label, std::string group,
                         std::string help, ValueType type, config::Range range = {},
                         std::vector<std::string> choices = {}) {
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

inline void bind_cascade(config::Schema::Builder& builder, OceanStartupOptions::Ocean& ocean) {
    OptionSpec cascade = option("ocean.cascade", "--ocean-cascade", "Inspect Cascade", "Ocean",
                                "Cascade isolated by ocean debug views.", ValueType::Enum, {},
                                {"all", "0", "1", "2", "3", "4"});
    builder.bind_custom(
        std::move(cascade),
        [&ocean](std::string_view value) {
            if (value == "all") {
                ocean.cascade = -1;
                return;
            }
            if (value.size() != 1U || value < "0" || value > "4") {
                throw std::runtime_error("invalid ocean cascade: " + std::string(value));
            }
            ocean.cascade = static_cast<int>(value.front() - '0');
        },
        [&ocean](const nlohmann::json& value) {
            if (value.is_null()) {
                return;
            }
            if (!value.is_string()) {
                throw std::runtime_error("wrong JSON type for config option: ocean.cascade");
            }
            const std::string text = value.get<std::string>();
            if (text == "all") {
                ocean.cascade = -1;
                return;
            }
            if (text.size() != 1U || text < "0" || text > "4") {
                throw std::runtime_error("invalid ocean cascade: " + text);
            }
            ocean.cascade = static_cast<int>(text.front() - '0');
        },
        [&ocean] {
            return ocean.cascade
                       ? nlohmann::json(*ocean.cascade < 0 ? "all" : std::to_string(*ocean.cascade))
                       : nlohmann::json(nullptr);
        });
}

} // namespace detail

using config::OptionSpec;
using config::ValueType;

struct OceanProjectConfig : OceanStartupOptions {
    host::CommonRunConfig common;
};

inline config::Schema ocean_project_config_schema(OceanProjectConfig& config) {
    auto builder = config::Schema::builder().compose(host::common_run_config_schema(config.common));
    builder.compose(
        cubey::ocean_surface_schema(config.ocean, cubey::OceanSurfaceSchemaMode::OceanProject));
    OptionSpec backdrop =
        detail::option("ocean.backdrop", "--ocean-backdrop", "Ocean Backdrop", "Ocean",
                       "Enable the shared ocean surface as a scene backdrop.", ValueType::Bool);
    backdrop.negative_cli_name = "--no-ocean-backdrop";
    builder.bind(std::move(backdrop), config.ocean.backdrop);
    builder.bind(
        detail::option("ocean.foreground_height_m", "--ocean-foreground-height",
                       "Foreground Height", "Ocean",
                       "Foreground scene height above the ocean datum in meters.", ValueType::Float,
                       {.has_min = true, .has_max = true, .min = -10000.0, .max = 100000.0}),
        config.ocean.foreground_height_m);
    builder.bind(detail::option("ocean.camera_preset", "--ocean-camera-preset", "Camera Preset",
                                "Ocean", "Initial ocean camera preset for repeatable captures.",
                                ValueType::Enum, {},
                                {"default", "low", "mid", "high", "close", "overhead", "wide"}),
                 config.ocean.camera_preset);
    builder.bind(
        detail::option("ocean.camera_orbit_spin_degrees_per_second",
                       "--ocean-camera-orbit-spin-deg-per-sec", "Camera Orbit Spin", "Ocean",
                       "Headless capture orbit-camera yaw spin rate in degrees per second.",
                       ValueType::Float,
                       {.has_min = true, .has_max = true, .min = -360.0, .max = 360.0}),
        config.ocean.camera_orbit_spin_degrees_per_second);
    builder.bind(
        detail::option("ocean.capture.video_orbit_degrees", "--capture-video-orbit-degrees",
                       "Video Orbit", "Capture",
                       "Optional eased video orbit in total degrees; zero keeps the camera fixed.",
                       ValueType::Float,
                       {.has_min = true,
                        .has_max = true,
                        .min = 0.0,
                        .max = kOceanMaximumCaptureOrbitDegrees}),
        config.capture.video_orbit_degrees);
    OptionSpec size_reference = detail::option(
        "ocean.size_reference", "--ocean-size-reference", "Size Reference", "Ocean",
        "Draw the diagnostic ocean scale pillar and its analytical shadow.", ValueType::Bool);
    size_reference.negative_cli_name = "--no-ocean-size-reference";
    builder.bind(std::move(size_reference), config.ocean.size_reference);
    detail::bind_cascade(builder, config.ocean);
    builder.bind(detail::option("ocean.wire_opacity", "--ocean-wire-opacity", "Wire Opacity",
                                "Ocean", "Opacity used by the ocean wire overlay.",
                                ValueType::Float,
                                {.has_min = true, .has_max = true, .min = 0.0, .max = 1.0}),
                 config.ocean.wire_opacity);
    builder.bind(detail::option("ocean.wire_overlay", "--ocean-wire-overlay", "Wire Overlay",
                                "Ocean", "Draw ocean mesh wire overlay.", ValueType::Bool),
                 config.ocean.wire_overlay);
    builder.compose(cubey::atmosphere_environment_schema(config.atmosphere));
    builder.compose(cubey::cloud_environment_schema(config.clouds));
    builder.compose(cubey::pbr_exposure_schema(config.pbr));
    builder.bind(detail::option("debug_view", "--debug-view", "Debug View", "Ocean",
                                "Ocean render view.", ValueType::String),
                 config.debug_view);
    return std::move(builder).build();
}

inline OceanProjectConfig parse_ocean_project_config(int argc, char** argv,
                                                     config::ParseResult* result = nullptr) {
    OceanProjectConfig config = host::parse_configured_app<OceanProjectConfig>(
        argc, argv, ocean_project_config_schema, result);
    validate_atmosphere_environment_options(config.atmosphere);
    validate_cloud_environment_options(config.clouds);
    if (config.capture.video_orbit_degrees.has_value() &&
        config.ocean.camera_orbit_spin_degrees_per_second.has_value()) {
        throw std::runtime_error(
            "Ocean capture cannot combine a bounded video orbit with a continuous orbit spin");
    }
    return config;
}

} // namespace cubey::projects::ocean
