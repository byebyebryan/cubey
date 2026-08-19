#pragma once

#include <cubey/core/config_schema.h>
#include <cubey/engine/ocean_surface_runtime.h>

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace cubey {

enum class OceanSurfaceSchemaMode {
    Common,
    OceanProject,
};

namespace ocean_surface_schema_detail {

using config::OptionSpec;
using config::ValueType;

inline OptionSpec option(std::string path, std::string cli, std::string label,
                         std::string help, ValueType type, config::Range range = {},
                         std::vector<std::string> choices = {}) {
    return {.path = std::move(path),
            .cli_name = std::move(cli),
            .negative_cli_name = {},
            .label = std::move(label),
            .group_path = "Ocean",
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

inline void bind_common(config::Schema::Builder& builder, OceanSurfaceOptions& o) {
    bind_string(builder,
                option("ocean.sea_state", "--ocean-sea-state", "Sea State",
                        "Tuned ocean surface state: calm, windy, or stormy.", ValueType::Enum, {},
                        {"calm", "windy", "stormy"}),
                o.sea_state);
    builder.bind(option("ocean.map_size", "--ocean-map-size", "Map Size",
                        "FFT map size for the active ocean project.", ValueType::UInt32,
                        {.has_min = true, .min = 1.0}), o.map_size);
    bind_string(builder,
                option("ocean.field_precision", "--ocean-field-precision", "Field Precision",
                        "Storage precision for ocean FFT wave fields.", ValueType::Enum, {},
                        {"full", "half"}),
                o.field_precision);
    bind_string(builder,
                option("ocean.surface_mode", "--ocean-surface-mode", "Surface Mode",
                        "Ocean surface mapping mode: flat or curved far field.", ValueType::Enum, {},
                        {"flat", "curved-far"}),
                o.surface_mode);
    builder.bind(option("ocean.mesh_cells", "--ocean-mesh-cells", "Mesh Cells",
                        "Maximum grid resolution per ocean clipmap patch.", ValueType::UInt32,
                        {.has_min = true, .has_max = true, .min = 32.0, .max = 512.0}),
                  o.mesh_cells);
    builder.bind(option("ocean.mesh_lod_levels", "--ocean-mesh-lod-levels", "Mesh LOD Levels",
                        "Minimum number of concentric ocean clipmap levels.", ValueType::UInt32,
                        {.has_min = true, .has_max = true, .min = 1.0, .max = 6.0}),
                  o.mesh_lod_levels);
    bind_float(builder,
               option("ocean.horizon_target_near_cell_m", "--ocean-horizon-target-near-cell-m",
                      "Horizon Near Cell",
                      "Preferred near-field cell size for automatic horizon coverage.",
                      ValueType::Float, {.has_min = true, .has_max = true, .min = 0.25,
                                         .max = 16.0}),
               o.horizon_target_near_cell_m);
    bind_string(builder,
                option("ocean.surface_shading_policy", "--ocean-surface-shading-policy",
                        "Surface Shading Policy", "Surface shading work policy: fixed or footprint adaptive.",
                        ValueType::Enum, {}, {"fixed", "footprint"}),
                o.surface_shading_policy);
    bind_float(builder,
               option("ocean.self_shadow_strength", "--ocean-self-shadow-strength",
                      "Self Shadow Strength", "Strength of heightfield ray-marched wave self-shadowing.",
                      ValueType::Float, {.has_min = true, .has_max = true, .min = 0.0,
                                         .max = 1.0}),
               o.self_shadow_strength);
    builder.bind(option("ocean.self_shadow_steps", "--ocean-self-shadow-steps", "Self Shadow Steps",
                        "Heightfield samples used by wave self-shadowing.", ValueType::UInt32,
                        {.has_min = true, .has_max = true, .min = 1.0, .max = 24.0}),
                  o.self_shadow_steps);
    builder.bind(option("ocean.self_shadow_far_steps", "--ocean-self-shadow-far-steps",
                        "Far Self Shadow Steps",
                        "Heightfield samples used when footprint-adaptive wave shadows are unresolved.",
                        ValueType::UInt32,
                        {.has_min = true, .has_max = true, .min = 1.0, .max = 24.0}),
                  o.self_shadow_far_steps);
    bind_float(builder,
               option("ocean.shape_anti_repeat_strength", "--ocean-shape-anti-repeat-strength",
                      "Shape Anti Repeat",
                      "Strength of the secondary displacement domain used to break tiling.",
                      ValueType::Float, {.has_min = true, .has_max = true, .min = 0.0,
                                         .max = 1.0}),
               o.shape_anti_repeat_strength);
    bind_float(builder,
               option("ocean.detail_anti_repeat_strength", "--ocean-detail-anti-repeat-strength",
                      "Detail Anti Repeat", "Strength of far-field normal and foam domain perturbation.",
                      ValueType::Float, {.has_min = true, .has_max = true, .min = 0.0,
                                         .max = 1.0}),
               o.detail_anti_repeat_strength);
    bind_string(builder,
                option("ocean.detail_filter", "--ocean-detail-filter", "Detail Filter",
                        "Normal and foam filtering mode: adaptive, bilinear, or bicubic.",
                        ValueType::Enum, {}, {"adaptive", "bilinear", "bicubic"}),
                o.detail_filter);
    OptionSpec spectral = option("ocean.spectral_domains", "--ocean-spectral-domains",
                                 "Spectral Domains",
                                 "Enable wavelength-domain separation between ocean cascades.",
                                 ValueType::Bool);
    spectral.negative_cli_name = "--no-ocean-spectral-domains";
    builder.bind(std::move(spectral), o.spectral_domains);
    OptionSpec terrain = option("ocean.terrain_fields", "--ocean-terrain-fields", "Terrain Fields",
                                "Enable terrain-ocean fields as an ocean influence.", ValueType::Bool);
    terrain.negative_cli_name = "--no-ocean-terrain-fields";
    builder.bind(std::move(terrain), o.terrain_fields);
    bind_float(builder,
               option("ocean.planet_radius_scale", "--ocean-planet-radius-scale",
                      "Planet Radius Scale",
                      "Scale applied to the atmosphere planet radius for ocean surface curvature.",
                      ValueType::Float, {.has_min = true, .has_max = true, .min = 0.01,
                                         .max = 10.0}),
               o.planet_radius_scale);
    bind_float(builder,
               option("ocean.curvature_start_ratio", "--ocean-curvature-start-ratio", "Curvature Start",
                      "Fraction of horizon distance where far-surface curvature starts.",
                      ValueType::Float, {.has_min = true, .has_max = true, .min = 0.0,
                                         .max = 1.0}),
               o.curvature_start_ratio);
    bind_float(builder,
               option("ocean.curvature_end_ratio", "--ocean-curvature-end-ratio", "Curvature End",
                      "Fraction of horizon distance where far-surface curvature reaches full strength.",
                      ValueType::Float, {.has_min = true, .has_max = true, .min = 0.0,
                                         .max = 1.0}),
               o.curvature_end_ratio);
    bind_float(builder,
               option("ocean.curvature_strength", "--ocean-curvature-strength", "Curvature Strength",
                      "Blend strength for curved far-ocean mapping.", ValueType::Float,
                      {.has_min = true, .has_max = true, .min = 0.0, .max = 1.0}),
               o.curvature_strength);
    bind_float(builder,
               option("ocean.cloud_reflection_strength", "--ocean-cloud-reflection-strength",
                      "Cloud Reflection", "Strength of the selected cloud environment in ocean reflections.",
                      ValueType::Float, {.has_min = true, .has_max = true, .min = 0.0,
                                         .max = 1.0}),
               o.cloud_reflection_strength);
    bind_float(builder,
               option("ocean.cloud_shadow_strength", "--ocean-cloud-shadow-strength", "Cloud Shadow",
                      "Strength of shared projected cloud transmittance on ocean direct lighting.",
                      ValueType::Float, {.has_min = true, .has_max = true, .min = 0.0,
                                         .max = 1.0}),
               o.cloud_shadow_strength);
}

inline void bind_ocean_project_extras(config::Schema::Builder& builder, OceanSurfaceOptions& o) {
    bind_string(builder,
                option("ocean.cloud_reflection_source", "--ocean-cloud-reflection-source",
                        "Cloud Reflection Source", "Cloud reflection source: planar or cached environment.",
                        ValueType::Enum, {}, {"cached", "planar"}),
                o.cloud_reflection_source);
    builder.bind(option("ocean.cloud_environment_extent", "--ocean-cloud-environment-extent",
                        "Cloud Probe Extent",
                        "Resolution per face of the cached cloud reflection environment.",
                        ValueType::UInt32, {.has_min = true, .has_max = true, .min = 32.0,
                                            .max = 128.0}),
                  o.cloud_environment_extent);
    bind_float(builder,
               option("ocean.cloud_environment_update_hz", "--ocean-cloud-environment-update-hz",
                      "Cloud Probe Rate",
                      "Refresh rate of the coherent cached cloud reflection environment.",
                      ValueType::Float, {.has_min = true, .has_max = true, .min = 0.5,
                                         .max = 30.0}),
               o.cloud_environment_update_hz);
    bind_float(builder,
               option("ocean.cloud_planar_resolution_scale", "--ocean-cloud-planar-resolution-scale",
                      "Planar Cloud Resolution", "Resolution scale of the every-frame reflected cloud view.",
                      ValueType::Float, {.has_min = true, .has_max = true, .min = 0.25,
                                         .max = 1.0}),
               o.cloud_planar_resolution_scale);
    builder.bind(option("ocean.cloud_planar_view_steps", "--ocean-cloud-planar-view-steps",
                        "Planar Cloud Steps", "View-march steps used by the reflected cloud view.",
                        ValueType::UInt32, {.has_min = true, .has_max = true, .min = 8.0,
                                            .max = 128.0}),
                  o.cloud_planar_view_steps);
    bind_float(builder,
               option("ocean.cloud_planar_guard_band", "--ocean-cloud-planar-guard-band",
                      "Planar Cloud Guard Band",
                      "Extra reflected field of view reserved for wave-facet directions.",
                      ValueType::Float, {.has_min = true, .has_max = true, .min = 0.0,
                                         .max = 0.5}),
               o.cloud_planar_guard_band);
}

} // namespace ocean_surface_schema_detail

[[nodiscard]] inline config::Schema ocean_surface_schema(
    OceanSurfaceOptions& options,
    OceanSurfaceSchemaMode mode = OceanSurfaceSchemaMode::Common) {
    auto builder = config::Schema::builder();
    ocean_surface_schema_detail::bind_common(builder, options);
    if (mode == OceanSurfaceSchemaMode::OceanProject) {
        ocean_surface_schema_detail::bind_ocean_project_extras(builder, options);
    }
    return std::move(builder).build();
}

} // namespace cubey
