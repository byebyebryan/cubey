#pragma once

#include <cubey/core/config_schema.h>
#include <cubey/engine/cloud_environment_config.h>

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace cubey {

enum class CloudEnvironmentSchemaMode {
    Full,
    CloudReference,
};

namespace cloud_environment_schema_detail {

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

inline void bind_float(config::Schema::Builder& builder, OptionSpec spec,
                       std::optional<float>& target) {
    builder.bind(std::move(spec), target);
}

inline void bind_string(config::Schema::Builder& builder, OptionSpec spec,
                        std::optional<std::string>& target) {
    builder.bind(std::move(spec), target);
}

inline void bind_full_surface_values(config::Schema::Builder& builder,
                                     CloudEnvironmentOptions& c) {
    bind_string(builder,
                option("clouds.quality", "--cloud-quality", "Quality", "Clouds/Sampling",
                        "Cloud render quality preset.", ValueType::Enum, {},
                        {"quarter", "half", "full"}),
                c.quality);
    builder.bind(option("clouds.view_steps", "--cloud-view-steps", "View Steps",
                        "Clouds/Sampling", "Cloud ray-march view steps override.",
                        ValueType::UInt32, {.has_min = true, .has_max = true, .min = 1.0,
                                            .max = 128.0}),
                  c.view_steps);
    builder.bind(option("clouds.view_samples", "--cloud-view-samples", "View Samples",
                        "Clouds/Sampling", "Cloud ray-start samples per pixel.",
                        ValueType::UInt32, {.has_min = true, .has_max = true, .min = 1.0,
                                            .max = 4.0}),
                  c.view_samples);
    bind_string(builder,
                option("clouds.weather_preset", "--cloud-weather-preset", "Weather Preset",
                        "Clouds/Layer", "Cloud coverage, density, scale, and wind preset.",
                        ValueType::Enum, {},
                        {"fair-weather", "broken-cumulus", "overcast-stratus", "storm-cells",
                         "high-cirrus", "clear", "scattered", "inspection", "overcast", "storm",
                         "surface-volume", "reference-parity", "ref-parity", "cloud-ref-parity"}),
                c.weather_preset);
    bind_string(builder,
                option("clouds.sampling_mode", "--cloud-sampling-mode", "Sampling Mode",
                        "Clouds/Sampling",
                        "Cloud ray-start sampling mode: interleaved, bayer, blue-noise, or off.",
                        ValueType::Enum, {}, {"interleaved", "bayer", "blue-noise", "off"}),
                c.sampling_mode);
    bind_string(builder,
                option("clouds.view_sample_mode", "--cloud-view-sample-mode", "View Sample Mode",
                        "Clouds/Sampling", "Cloud view-sample strategy: single-frame or temporal-phased.",
                        ValueType::Enum, {}, {"single-frame", "temporal-phased"}),
                c.view_sample_mode);
    bind_string(builder,
                option("clouds.density_model", "--cloud-density-model", "Density Model",
                        "Clouds/Shape", "Cloud density model.", ValueType::Enum, {},
                        {"surface-volume", "experimental-aerial-orbit", "ref-density",
                         "cloud-ref-compatible", "cloud-ref", "compatible", "procedural", "active",
                         "legacy-procedural"}),
                c.density_model);
    bind_string(builder,
                option("clouds.resolve_mode", "--cloud-resolve-mode", "Resolve Mode",
                        "Clouds/Lighting", "Cloud final resolve mode: terrain-post or metadata-bilateral.",
                        ValueType::Enum, {}, {"terrain-post", "metadata-bilateral"}),
                c.resolve_mode);
    bind_string(builder,
                option("clouds.distance_mode", "--cloud-distance-mode", "Distance Mode",
                        "Clouds/Deferred Aerial Orbit", "Cloud distance regime.", ValueType::Enum,
                        {}, {"auto", "local", "orbit-shell", "blend-debug"}),
                c.distance_mode);
    bind_string(builder,
                option("clouds.orbit_representation", "--cloud-orbit-representation",
                        "Orbit Representation", "Clouds/Deferred Aerial Orbit",
                        "Deferred orbit cloud representation: volume or surface-shell.",
                        ValueType::Enum, {}, {"volume", "surface-shell"}),
                c.orbit_representation);

    const auto floating = [&builder](const char* path, const char* cli, const char* label,
                                     const char* group, const char* help, config::Range range,
                                     std::optional<float>& target) {
        bind_float(builder, option(path, cli, label, group, help, ValueType::Float, range), target);
    };
    floating("clouds.planet_radius_m", "--cloud-planet-radius-m", "Planet Radius", "Clouds/Layer",
             "Planet radius used by the cloud shell in meters.", {.has_min = true, .min = 1.0},
             c.planet_radius_m);
    floating("clouds.bottom_altitude_m", "--cloud-bottom-altitude-m", "Cloud Bottom", "Clouds/Layer",
             "Cloud layer bottom altitude above the planet surface in meters.",
             {.has_min = true, .min = 0.0}, c.bottom_altitude_m);
    floating("clouds.top_altitude_m", "--cloud-top-altitude-m", "Cloud Top", "Clouds/Layer",
             "Cloud layer top altitude above the planet surface in meters.",
             {.has_min = true, .min = 0.0}, c.top_altitude_m);
    floating("clouds.coverage", "--cloud-coverage", "Coverage", "Clouds/Layer",
             "Base cloud coverage fraction.", {.has_min = true, .has_max = true, .min = 0.0,
                                                .max = 1.0},
             c.coverage);
    floating("clouds.density", "--cloud-density", "Density", "Clouds/Layer",
             "Cloud extinction density multiplier.", {.has_min = true, .min = 0.0}, c.density);
    floating("clouds.weather_scale_km", "--cloud-weather-scale-km", "Weather Scale", "Clouds/Layer",
             "Approximate broad cloud weather feature size in kilometers.",
             {.has_min = true, .min = 0.001}, c.weather_scale_km);
    floating("clouds.shape_domain_km", "--cloud-shape-domain-km", "Shape Domain", "Clouds/Layer",
             "Approximate local cloud density texture domain size in kilometers.",
             {.has_min = true, .min = 0.001}, c.shape_domain_km);
    floating("clouds.footprint_filter_strength", "--cloud-footprint-filter-strength",
             "Footprint Filter", "Clouds/Shape",
             "Strength of deterministic footprint filtering for far and grazing cloud detail.",
             {.has_min = true, .has_max = true, .min = 0.0, .max = 2.0},
             c.footprint_filter_strength);
    floating("clouds.edge_softness", "--cloud-edge-softness", "Edge Softness", "Clouds/Shape",
             "Strength of footprint-aware density edge softening.",
             {.has_min = true, .has_max = true, .min = 0.0, .max = 2.0}, c.edge_softness);
    floating("clouds.edge_detail_fade", "--cloud-edge-detail-fade", "Edge Detail Fade", "Clouds/Shape",
             "Amount of unresolved high-frequency detail erosion faded at cloud edges.",
             {.has_min = true, .has_max = true, .min = 0.0, .max = 2.0}, c.edge_detail_fade);
    floating("clouds.edge_resolve_strength", "--cloud-edge-resolve-strength", "Edge Resolve", "Clouds/Shape",
             "Strength of edge-aware cloud resolve in the final composite.",
             {.has_min = true, .has_max = true, .min = 0.0, .max = 1.0}, c.edge_resolve_strength);
    floating("clouds.vertical_shear_fraction", "--cloud-vertical-shear-fraction", "Vertical Shear", "Clouds/Shape",
             "Fraction of weather feature size used for altitude-dependent cloud shear.",
             {.has_min = true, .has_max = true, .min = 0.0, .max = 0.5}, c.vertical_shear_fraction);
    floating("clouds.wind_speed_mps", "--cloud-wind-speed-mps", "Wind Speed", "Clouds/Layer",
             "Cloud weather-map wind speed in meters per second.", {.has_min = true, .min = 0.0},
             c.wind_speed_mps);
    floating("clouds.shadow_strength", "--cloud-shadow-strength", "Shadow Strength", "Clouds/Lighting",
             "Strength of prototype cloud shadows on the standalone cloud ground proxy.",
             {.has_min = true, .has_max = true, .min = 0.0, .max = 2.0}, c.shadow_strength);
    floating("clouds.horizon_strength", "--cloud-horizon-strength", "Horizon Strength", "Clouds/Lighting",
             "Strength of Cloud V1 surface horizon fill and glow.",
             {.has_min = true, .has_max = true, .min = 0.0, .max = 2.0}, c.horizon_strength);
    floating("clouds.weather_fronts", "--cloud-weather-fronts", "Weather Fronts", "Clouds/Shape",
             "Feature-isolation weight for frontal cloud structures.",
             {.has_min = true, .has_max = true, .min = 0.0, .max = 1.0}, c.weather_fronts);
    floating("clouds.weather_cells", "--cloud-weather-cells", "Weather Cells", "Clouds/Shape",
             "Feature-isolation weight for cellular cloud structures.",
             {.has_min = true, .has_max = true, .min = 0.0, .max = 1.0}, c.weather_cells);
    floating("clouds.weather_streaks", "--cloud-weather-streaks", "Weather Streaks", "Clouds/Shape",
             "Feature-isolation weight for wind-aligned streak structures.",
             {.has_min = true, .has_max = true, .min = 0.0, .max = 1.0}, c.weather_streaks);
    floating("clouds.weather_softness", "--cloud-weather-softness", "Weather Softness", "Clouds/Shape",
             "Softness of broad weather bias transitions.",
             {.has_min = true, .has_max = true, .min = 0.02, .max = 0.6}, c.weather_softness);
    floating("clouds.weather_influence", "--cloud-weather-influence", "Weather Influence", "Clouds/Shape",
             "How strongly the broad weather map biases local cloud density.",
             {.has_min = true, .has_max = true, .min = 0.0, .max = 1.0}, c.weather_influence);
    floating("clouds.detail_erosion", "--cloud-detail-erosion", "Detail Erosion", "Clouds/Shape",
             "Feature-isolation weight for high-frequency cloud erosion.",
             {.has_min = true, .has_max = true, .min = 0.0, .max = 1.0}, c.detail_erosion);
    floating("clouds.ambient_strength", "--cloud-ambient-strength", "Ambient Strength", "Clouds/Lighting",
             "Cloud ambient-light multiplier used by the production cloud renderer.",
             {.has_min = true, .has_max = true, .min = 0.0, .max = 3.0}, c.ambient_strength);
    floating("clouds.direct_strength", "--cloud-direct-strength", "Direct Strength", "Clouds/Lighting",
             "Cloud direct sun-light multiplier used by the production cloud renderer.",
             {.has_min = true, .has_max = true, .min = 0.0, .max = 3.0}, c.direct_strength);
    floating("clouds.phase_strength", "--cloud-phase-strength", "Phase Strength", "Clouds/Lighting",
             "Cloud forward/rim phase-light multiplier used by the production cloud renderer.",
             {.has_min = true, .has_max = true, .min = 0.0, .max = 3.0}, c.phase_strength);
    floating("clouds.twilight_color_strength", "--cloud-twilight-color-strength", "Twilight Color", "Clouds/Lighting",
             "Low-sun cloud color contribution from warm sun and horizon sky radiance.",
             {.has_min = true, .has_max = true, .min = 0.0, .max = 2.0}, c.twilight_color_strength);
    floating("clouds.twilight_edge_strength", "--cloud-twilight-edge-strength", "Twilight Edge", "Clouds/Lighting",
             "Low-sun color boost for cloud optical edges and rim response.",
             {.has_min = true, .has_max = true, .min = 0.0, .max = 2.0}, c.twilight_edge_strength);
    floating("clouds.twilight_saturation_strength", "--cloud-twilight-saturation-strength", "Twilight Saturation", "Clouds/Lighting",
             "Amount of cloud color saturation preserved during twilight final composite.",
             {.has_min = true, .has_max = true, .min = 0.0, .max = 2.0}, c.twilight_saturation_strength);
    floating("clouds.afterglow_strength", "--cloud-afterglow-strength", "Afterglow", "Clouds/Lighting",
             "Art-directed red, pink, or purple low-sun cloud edge accent.",
             {.has_min = true, .has_max = true, .min = 0.0, .max = 2.0}, c.afterglow_strength);
    floating("clouds.powder_strength", "--cloud-powder-strength", "Powder Strength", "Clouds/Lighting",
             "Powder-style brightening strength for thin cloud edges.",
             {.has_min = true, .has_max = true, .min = 0.0, .max = 1.0}, c.powder_strength);
    floating("clouds.final_contrast", "--cloud-final-contrast", "Final Contrast", "Clouds/Lighting",
             "Final cloud composite contrast multiplier.",
             {.has_min = true, .has_max = true, .min = 0.0, .max = 3.0}, c.final_contrast);
    floating("clouds.final_saturation", "--cloud-final-saturation", "Final Saturation", "Clouds/Lighting",
             "Final cloud composite saturation multiplier.",
             {.has_min = true, .has_max = true, .min = 0.0, .max = 3.0}, c.final_saturation);
    floating("clouds.resolve_strength", "--cloud-resolve-strength", "Resolve Strength", "Clouds/Lighting",
             "Amount of alpha-aware cloud product resolve in final view.",
             {.has_min = true, .has_max = true, .min = 0.0, .max = 1.0}, c.resolve_strength);
    floating("clouds.horizon_glow_strength", "--cloud-horizon-glow-strength", "Horizon Glow", "Clouds/Lighting",
             "Final composite horizon fill/glow multiplier.",
             {.has_min = true, .has_max = true, .min = 0.0, .max = 3.0}, c.horizon_glow_strength);
    floating("clouds.sun_glare_strength", "--cloud-sun-glare-strength", "Sun Glare", "Clouds/Lighting",
             "Final composite sun halo and glare multiplier.",
             {.has_min = true, .has_max = true, .min = 0.0, .max = 3.0}, c.sun_glare_strength);
    floating("clouds.jitter_strength", "--cloud-jitter-strength", "Jitter Strength", "Clouds/Sampling",
             "Cloud ray-start jitter amount applied by the selected sampling mode.",
             {.has_min = true, .has_max = true, .min = 0.0, .max = 1.0}, c.jitter_strength);
    floating("clouds.orbit_transition_start_m", "--cloud-orbit-transition-start-m", "Orbit Transition Start", "Clouds/Deferred Aerial Orbit",
             "Deferred camera altitude where the broad orbit cloud shell starts blending in.",
             {.has_min = true, .min = 0.0}, c.orbit_transition_start_m);
    floating("clouds.orbit_transition_end_m", "--cloud-orbit-transition-end-m", "Orbit Transition End", "Clouds/Deferred Aerial Orbit",
             "Deferred camera altitude where the broad orbit cloud shell fully replaces local clouds.",
             {.has_min = true, .min = 0.0}, c.orbit_transition_end_m);
    floating("clouds.far_shell_start_m", "--cloud-far-shell-start-m", "Far Shell Start", "Clouds/Deferred Aerial Orbit",
             "Deferred view-ray distance where high-altitude rays start preferring the orbit shell.",
             {.has_min = true, .min = 0.0}, c.far_shell_start_m);
    floating("clouds.far_shell_end_m", "--cloud-far-shell-end-m", "Far Shell End", "Clouds/Deferred Aerial Orbit",
             "Deferred view-ray distance where high-altitude rays fully prefer the orbit shell.",
             {.has_min = true, .min = 0.0}, c.far_shell_end_m);
    floating("clouds.far_shell_strength", "--cloud-far-shell-strength", "Far Shell Strength", "Clouds/Deferred Aerial Orbit",
             "Deferred far cloud shell contribution behind high-view local volume.",
             {.has_min = true, .has_max = true, .min = 0.0, .max = 1.5}, c.far_shell_strength);
    floating("clouds.orbit_detail_strength", "--cloud-orbit-detail-strength", "Orbit Detail", "Clouds/Deferred Aerial Orbit",
             "Deferred high-frequency detail retained by the broad orbit shell.",
             {.has_min = true, .has_max = true, .min = 0.0, .max = 1.0}, c.orbit_detail_strength);
    floating("clouds.orbit_density_scale", "--cloud-orbit-density-scale", "Orbit Density", "Clouds/Deferred Aerial Orbit",
             "Deferred density multiplier for the broad orbit cloud shell.",
             {.has_min = true, .has_max = true, .min = 0.0, .max = 2.0}, c.orbit_density_scale);
    floating("clouds.orbit_fill", "--cloud-orbit-fill", "Orbit Fill", "Clouds/Deferred Aerial Orbit",
             "Deferred fill bias for broad orbit cloud weather coverage.",
             {.has_min = true, .has_max = true, .min = 0.0, .max = 2.0}, c.orbit_fill);
    floating("clouds.orbit_motion_strength", "--cloud-orbit-motion-strength", "Orbit Motion", "Clouds/Deferred Aerial Orbit",
             "Deferred motion multiplier for procedural orbit weather advection.",
             {.has_min = true, .has_max = true, .min = 0.0, .max = 4.0}, c.orbit_motion_strength);
    floating("clouds.orbit_shell_extinction", "--cloud-orbit-shell-extinction", "Orbit Extinction", "Clouds/Deferred Aerial Orbit",
             "Deferred extinction multiplier for cloud-top shell optical depth.",
             {.has_min = true, .has_max = true, .min = 0.0, .max = 8.0}, c.orbit_shell_extinction);
}

inline void bind_full(config::Schema::Builder& builder, CloudEnvironmentOptions& c) {
    bind_string(builder,
                option("clouds.debug_view", "--cloud-debug-view", "Debug View",
                        "Clouds/Reference Diagnostics",
                        "Cloud layer debug or diagnostic view; intended for inspection, not production configs.",
                        ValueType::String),
                c.debug_view);
    bind_full_surface_values(builder, c);
    OptionSpec enabled = option("clouds.enabled", "--clouds", "Clouds", "Clouds/General",
                                "Enable cloud layer rendering in shared atmosphere-backed projects.",
                                ValueType::Bool);
    enabled.negative_cli_name = "--no-clouds";
    builder.bind(std::move(enabled), c.enabled);
    OptionSpec temporal = option("clouds.temporal", "--cloud-temporal", "Temporal", "Clouds/Sampling",
                                 "Enable experimental temporal reconstruction for the cloud product; off by default until shimmer is solved.",
                                 ValueType::Bool);
    temporal.negative_cli_name = "--no-cloud-temporal";
    builder.bind(std::move(temporal), c.temporal);
    OptionSpec local = option("clouds.local_volume", "--cloud-local-volume", "Local Volume", "Clouds/Shape",
                              "Enable near and overhead volumetric cloud marching.", ValueType::Bool);
    local.negative_cli_name = "--no-cloud-local-volume";
    builder.bind(std::move(local), c.local_volume);
    OptionSpec horizon = option("clouds.horizon_layer", "--cloud-horizon-layer", "Horizon Layer", "Clouds/Shape",
                                "Enable the Cloud V1 surface horizon handoff; disable for local-only reference A/B.",
                                ValueType::Bool);
    horizon.negative_cli_name = "--no-cloud-horizon-layer";
    builder.bind(std::move(horizon), c.horizon_layer);
}

inline void bind_cloud_reference(config::Schema::Builder& builder, CloudEnvironmentOptions& c) {
    bind_string(builder,
                option("clouds.quality", "--cloud-quality", "Quality", "Clouds/Sampling",
                        "Cloud quality preset.", ValueType::Enum, {}, {"quarter", "half", "full"}),
                c.quality);
    builder.bind(option("clouds.view_steps", "--cloud-view-steps", "View Steps",
                        "Clouds/Sampling", "Cloud view steps.", ValueType::UInt32,
                        {.has_min = true, .has_max = true, .min = 1.0, .max = 128.0}),
                  c.view_steps);
    builder.bind(option("clouds.view_samples", "--cloud-view-samples", "View Samples",
                        "Clouds/Sampling", "Cloud view samples.", ValueType::UInt32,
                        {.has_min = true, .has_max = true, .min = 1.0, .max = 4.0}),
                  c.view_samples);
    bind_string(builder,
                option("clouds.weather_preset", "--cloud-weather-preset", "Weather Preset",
                        "Clouds/Layer", "Cloud weather preset.", ValueType::Enum, {},
                        {"fair-weather", "clear", "broken-cumulus", "scattered", "inspection",
                         "overcast-stratus", "overcast", "storm-cells", "storm", "high-cirrus"}),
                c.weather_preset);
    bind_string(builder,
                option("clouds.resolve_mode", "--cloud-resolve-mode", "Resolve Mode",
                        "Clouds/Lighting", "Cloud resolve mode.", ValueType::Enum, {},
                        {"terrain-post", "terrain", "gaussian", "metadata-bilateral", "bilateral",
                         "metadata"}),
                c.resolve_mode);
    builder.bind(option("clouds.camera_altitude_m", "--cloud-camera-altitude-m", "Camera Altitude",
                        "Clouds/Reference Diagnostics",
                        "Reference/capture app camera altitude above the planet surface in meters.",
                        ValueType::Float, {.has_min = true, .min = 0.0}), c.camera_altitude_m);

    const auto floating = [&builder](const char* path, const char* cli, const char* label,
                                     const char* help, config::Range range,
                                     std::optional<float>& target) {
        bind_float(builder, option(path, cli, label, "Cloud Ref", help, ValueType::Float, range),
                   target);
    };
    floating("clouds.planet_radius_m", "--cloud-planet-radius-m", "Planet Radius",
             "Planet radius used by the cloud shell in meters.", {.has_min = true, .min = 1.0},
             c.planet_radius_m);
    floating("clouds.bottom_altitude_m", "--cloud-bottom-altitude-m", "Cloud Bottom",
             "Cloud layer bottom altitude above the planet surface in meters.",
             {.has_min = true, .min = 0.0}, c.bottom_altitude_m);
    floating("clouds.top_altitude_m", "--cloud-top-altitude-m", "Cloud Top",
             "Cloud layer top altitude above the planet surface in meters.",
             {.has_min = true, .min = 0.0}, c.top_altitude_m);
    floating("clouds.coverage", "--cloud-coverage", "Coverage", "Base cloud coverage fraction.",
             {.has_min = true, .has_max = true, .min = 0.0, .max = 1.0}, c.coverage);
    floating("clouds.density", "--cloud-density", "Density", "Cloud extinction density multiplier.",
             {.has_min = true, .min = 0.0}, c.density);
    floating("clouds.weather_scale_km", "--cloud-weather-scale-km", "Weather Scale",
             "Approximate broad cloud weather feature size in kilometers.",
             {.has_min = true, .min = 0.001}, c.weather_scale_km);
    floating("clouds.wind_speed_mps", "--cloud-wind-speed-mps", "Wind Speed",
             "Cloud weather-map wind speed in meters per second.", {.has_min = true, .min = 0.0},
             c.wind_speed_mps);
    floating("clouds.shadow_strength", "--cloud-shadow-strength", "Shadow Strength",
             "Strength of prototype cloud shadows on the standalone cloud ground proxy.",
             {.has_min = true, .has_max = true, .min = 0.0, .max = 2.0}, c.shadow_strength);
    floating("clouds.horizon_strength", "--cloud-horizon-strength", "Horizon Strength",
             "Strength of Cloud V1 surface horizon fill and glow.",
             {.has_min = true, .has_max = true, .min = 0.0, .max = 2.0}, c.horizon_strength);
    floating("clouds.weather_fronts", "--cloud-weather-fronts", "Weather Fronts",
             "Feature-isolation weight for frontal cloud structures.",
             {.has_min = true, .has_max = true, .min = 0.0, .max = 1.0}, c.weather_fronts);
    floating("clouds.weather_cells", "--cloud-weather-cells", "Weather Cells",
             "Feature-isolation weight for cellular cloud structures.",
             {.has_min = true, .has_max = true, .min = 0.0, .max = 1.0}, c.weather_cells);
    floating("clouds.weather_streaks", "--cloud-weather-streaks", "Weather Streaks",
             "Feature-isolation weight for wind-aligned streak structures.",
             {.has_min = true, .has_max = true, .min = 0.0, .max = 1.0}, c.weather_streaks);
    floating("clouds.detail_erosion", "--cloud-detail-erosion", "Detail Erosion",
             "Feature-isolation weight for high-frequency cloud erosion.",
             {.has_min = true, .has_max = true, .min = 0.0, .max = 1.0}, c.detail_erosion);
    floating("clouds.ambient_strength", "--cloud-ambient-strength", "Ambient Strength",
             "Cloud ambient-light multiplier used by the reference renderer.",
             {.has_min = true, .has_max = true, .min = 0.0, .max = 3.0}, c.ambient_strength);
    floating("clouds.direct_strength", "--cloud-direct-strength", "Direct Strength",
             "Cloud direct sun-light multiplier used by the reference renderer.",
             {.has_min = true, .has_max = true, .min = 0.0, .max = 3.0}, c.direct_strength);
    floating("clouds.phase_strength", "--cloud-phase-strength", "Phase Strength",
             "Cloud phase-light multiplier used by the reference renderer.",
             {.has_min = true, .has_max = true, .min = 0.0, .max = 3.0}, c.phase_strength);
    floating("clouds.powder_strength", "--cloud-powder-strength", "Powder Strength",
             "Powder-style brightening strength for thin cloud edges.",
             {.has_min = true, .has_max = true, .min = 0.0, .max = 1.0}, c.powder_strength);
    floating("clouds.final_contrast", "--cloud-final-contrast", "Final Contrast",
             "Final cloud composite contrast multiplier.",
             {.has_min = true, .has_max = true, .min = 0.0, .max = 3.0}, c.final_contrast);
    floating("clouds.final_saturation", "--cloud-final-saturation", "Final Saturation",
             "Final cloud composite saturation multiplier.",
             {.has_min = true, .has_max = true, .min = 0.0, .max = 3.0}, c.final_saturation);
    floating("clouds.resolve_strength", "--cloud-resolve-strength", "Resolve Strength",
             "Amount of post-resolve cloud blur.", {.has_min = true, .has_max = true, .min = 0.0,
                                                     .max = 1.0},
             c.resolve_strength);
    floating("clouds.horizon_glow_strength", "--cloud-horizon-glow-strength", "Horizon Glow",
             "Final composite horizon fill/glow multiplier.",
             {.has_min = true, .has_max = true, .min = 0.0, .max = 3.0}, c.horizon_glow_strength);
    floating("clouds.sun_glare_strength", "--cloud-sun-glare-strength", "Sun Glare",
             "Final composite sun halo and glare multiplier.",
             {.has_min = true, .has_max = true, .min = 0.0, .max = 3.0}, c.sun_glare_strength);
    OptionSpec temporal = option("clouds.temporal", "--cloud-temporal", "Temporal", "Clouds/Sampling",
                                 "Enable temporal reconstruction.", ValueType::Bool);
    temporal.negative_cli_name = "--no-cloud-temporal";
    builder.bind(std::move(temporal), c.temporal);
    OptionSpec local = option("clouds.local_volume", "--cloud-local-volume", "Local Volume", "Clouds/Shape",
                              "Enable local volume.", ValueType::Bool);
    local.negative_cli_name = "--no-cloud-local-volume";
    builder.bind(std::move(local), c.local_volume);
    OptionSpec horizon = option("clouds.horizon_layer", "--cloud-horizon-layer", "Horizon Layer", "Clouds/Shape",
                                "Enable horizon layer.", ValueType::Bool);
    horizon.negative_cli_name = "--no-cloud-horizon-layer";
    builder.bind(std::move(horizon), c.horizon_layer);
}

} // namespace cloud_environment_schema_detail

[[nodiscard]] inline config::Schema cloud_environment_schema(
    CloudEnvironmentOptions& options,
    CloudEnvironmentSchemaMode mode = CloudEnvironmentSchemaMode::Full) {
    auto builder = config::Schema::builder();
    if (mode == CloudEnvironmentSchemaMode::CloudReference) {
        cloud_environment_schema_detail::bind_cloud_reference(builder, options);
    } else {
        cloud_environment_schema_detail::bind_full(builder, options);
    }
    return std::move(builder).build();
}

} // namespace cubey
