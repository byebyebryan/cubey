#include "terrain_config.h"

#include <cubey/engine/atmosphere_environment_schema.h>
#include <cubey/engine/cloud_environment_schema.h>
#include <cubey/host/configured_app.h>

#include <nlohmann/json.hpp>

#include <cmath>
#include <numbers>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace cubey::projects::terrain {
namespace {

using cubey::config::OptionSpec;
using cubey::config::ValueType;

inline constexpr float kTerrainDefaultTimeHours = 9.0F;

[[nodiscard]] OptionSpec option(std::string path, std::string cli_name, std::string label,
                                std::string group_path, std::string help, ValueType type,
                                cubey::config::Range range = {},
                                std::vector<std::string> enum_values = {}) {
    return {
        .path = std::move(path),
        .cli_name = std::move(cli_name),
        .negative_cli_name = {},
        .label = std::move(label),
        .group_path = std::move(group_path),
        .help = std::move(help),
        .type = type,
        .range = range,
        .enum_values = std::move(enum_values),
    };
}

[[nodiscard]] TerrainCameraPreset terrain_camera_preset_from_name(std::string_view name) {
    if (name == "backdrop") {
        return TerrainCameraPreset::Backdrop;
    }
    if (name == "backdrop-stage") {
        return TerrainCameraPreset::BackdropStage;
    }
    throw std::runtime_error("terrain product camera must be backdrop or backdrop-stage");
}

[[nodiscard]] std::string_view terrain_camera_preset_name(TerrainCameraPreset preset) noexcept {
    return preset == TerrainCameraPreset::Backdrop ? "backdrop" : "backdrop-stage";
}

void set_json_string(std::optional<TerrainDebugView>& target, const nlohmann::json& value,
                     std::string_view path) {
    if (value.is_null()) {
        target.reset();
        return;
    }
    if (!value.is_string()) {
        throw std::runtime_error("wrong JSON type for config option: " + std::string(path));
    }
    const std::string text = value.get<std::string>();
    if (text.empty()) {
        target.reset();
    } else {
        target = terrain_debug_view_from_name(text);
    }
}

void bind_debug_view(cubey::config::Schema::Builder& builder, TerrainProjectConfig& config) {
    const OptionSpec spec = option("debug_view", "--debug-view", "Debug View", "Terrain/Debug",
                                   "Terrain backdrop diagnostic view; aliases remain accepted.",
                                   ValueType::String);
    builder.bind_custom(
        spec,
        [&config](std::string_view value) {
            if (value.empty()) {
                config.debug_view.reset();
            } else {
                config.debug_view = terrain_debug_view_from_name(value);
            }
        },
        [&config](const nlohmann::json& value) {
            set_json_string(config.debug_view, value, "debug_view");
        },
        [&config] {
            return config.debug_view.has_value()
                       ? nlohmann::json(std::string(terrain_debug_view_name(
                             config.debug_view.value())))
                       : nlohmann::json(nullptr);
        });
}

void bind_camera_preset(cubey::config::Schema::Builder& builder,
                        TerrainProjectConfig& config) {
    // Keep the historical enum spellings in metadata so those inputs receive
    // the same active-product rejection, while only backdrop and
    // backdrop-stage can be stored by the typed facade.
    const OptionSpec spec = option(
        "terrain.camera_preset", "--terrain-camera-preset", "Camera Preset", "Terrain",
        "Initial terrain review framing; the active product supports backdrop or backdrop-stage.",
        ValueType::Enum, {}, {"oblique", "profile", "top", "surface", "surface-low", "ground",
                              "backdrop", "backdrop-stage", "midground", "coastal-oblique"});
    builder.bind_custom(
        spec,
        [&config](std::string_view value) {
            if (value.empty()) {
                config.terrain.camera_preset.reset();
            } else {
                config.terrain.camera_preset = terrain_camera_preset_from_name(value);
            }
        },
        [&config](const nlohmann::json& value) {
            if (value.is_null()) {
                config.terrain.camera_preset.reset();
            } else if (!value.is_string()) {
                throw std::runtime_error(
                    "wrong JSON type for config option: terrain.camera_preset");
            } else if (value.get<std::string>().empty()) {
                config.terrain.camera_preset.reset();
            } else {
                config.terrain.camera_preset =
                    terrain_camera_preset_from_name(value.get<std::string>());
            }
        },
        [&config] {
            return config.terrain.camera_preset.has_value()
                       ? nlohmann::json(std::string(terrain_camera_preset_name(
                             config.terrain.camera_preset.value())))
                       : nlohmann::json(nullptr);
        });
}

void bind_surface_detail(cubey::config::Schema::Builder& builder,
                         TerrainProjectConfig& config) {
    const OptionSpec spec = option(
        "terrain.surface_detail", "--terrain-surface-detail", "Surface Detail", "Terrain",
        "Terrain material detail mode.", ValueType::Enum, {}, {"flat", "filtered-detail"});
    builder.bind_custom(
        spec,
        [&config](std::string_view value) {
            if (value.empty()) {
                config.terrain.surface_detail.reset();
            } else {
                config.terrain.surface_detail = terrain_material_mode_from_name(value);
            }
        },
        [&config](const nlohmann::json& value) {
            if (value.is_null()) {
                config.terrain.surface_detail.reset();
            } else if (!value.is_string()) {
                throw std::runtime_error(
                    "wrong JSON type for config option: terrain.surface_detail");
            } else if (value.get<std::string>().empty()) {
                config.terrain.surface_detail.reset();
            } else {
                config.terrain.surface_detail =
                    terrain_material_mode_from_name(value.get<std::string>());
            }
        },
        [&config] {
            return config.terrain.surface_detail.has_value()
                       ? nlohmann::json(std::string(terrain_material_mode_name(
                             config.terrain.surface_detail.value())))
                       : nlohmann::json(nullptr);
        });
}

} // namespace

std::string_view terrain_debug_view_name(TerrainDebugView view) noexcept {
    switch (view) {
    case TerrainDebugView::Surface:
        return "surface";
    case TerrainDebugView::Height:
        return "height";
    case TerrainDebugView::Slope:
        return "slope";
    case TerrainDebugView::Clay:
        return "clay";
    case TerrainDebugView::Normal:
        return "normal";
    case TerrainDebugView::MaterialWeights:
        return "material-weights";
    case TerrainDebugView::AmbientVisibility:
        return "ambient-visibility";
    case TerrainDebugView::ProjectedEdge:
        return "projected-edge";
    case TerrainDebugView::MaterialAlbedo:
        return "material-albedo";
    case TerrainDebugView::MaterialNormal:
        return "material-normal";
    case TerrainDebugView::MaterialRoughness:
        return "material-roughness";
    case TerrainDebugView::SunVisibility:
        return "sun-visibility";
    case TerrainDebugView::ClassificationNormal:
        return "classification-normal";
    case TerrainDebugView::Vegetation:
        return "vegetation";
    case TerrainDebugView::Moisture:
        return "moisture";
    case TerrainDebugView::AmbientLighting:
        return "ambient-light";
    case TerrainDebugView::DirectLighting:
        return "direct-light";
    case TerrainDebugView::StageOwnership:
        return "stage-ownership";
    }
    return "surface";
}

TerrainDebugView terrain_debug_view_from_name(std::string_view name) {
    if (name.empty() || name == "surface" || name == "final") {
        return TerrainDebugView::Surface;
    }
    if (name == "height") {
        return TerrainDebugView::Height;
    }
    if (name == "slope") {
        return TerrainDebugView::Slope;
    }
    if (name == "clay") {
        return TerrainDebugView::Clay;
    }
    if (name == "normal" || name == "normals") {
        return TerrainDebugView::Normal;
    }
    if (name == "material-weights" || name == "materials") {
        return TerrainDebugView::MaterialWeights;
    }
    if (name == "ambient-visibility" || name == "ambient") {
        return TerrainDebugView::AmbientVisibility;
    }
    if (name == "projected-edge" || name == "edge") {
        return TerrainDebugView::ProjectedEdge;
    }
    if (name == "material-albedo" || name == "albedo") {
        return TerrainDebugView::MaterialAlbedo;
    }
    if (name == "material-normal" || name == "detail-normal") {
        return TerrainDebugView::MaterialNormal;
    }
    if (name == "material-roughness" || name == "roughness") {
        return TerrainDebugView::MaterialRoughness;
    }
    if (name == "sun-visibility" || name == "shadow") {
        return TerrainDebugView::SunVisibility;
    }
    if (name == "classification-normal" || name == "macro-normal") {
        return TerrainDebugView::ClassificationNormal;
    }
    if (name == "vegetation") {
        return TerrainDebugView::Vegetation;
    }
    if (name == "moisture") {
        return TerrainDebugView::Moisture;
    }
    if (name == "ambient-light") {
        return TerrainDebugView::AmbientLighting;
    }
    if (name == "direct-light") {
        return TerrainDebugView::DirectLighting;
    }
    if (name == "stage-ownership" || name == "ownership") {
        return TerrainDebugView::StageOwnership;
    }
    throw std::runtime_error("unsupported terrain diagnostic: " + std::string(name));
}

std::string_view terrain_placement_mode_name(TerrainPlacementMode mode) noexcept {
    switch (mode) {
    case TerrainPlacementMode::Selected:
        return "selected";
    case TerrainPlacementMode::RawCenter:
        return "raw-center";
    case TerrainPlacementMode::RawSample:
        return "raw-sample";
    }
    return "selected";
}

TerrainPlacementMode terrain_placement_mode_from_name(std::string_view name) {
    if (name.empty() || name == "selected") {
        return TerrainPlacementMode::Selected;
    }
    if (name == "raw-center") {
        return TerrainPlacementMode::RawCenter;
    }
    if (name == "raw-sample") {
        return TerrainPlacementMode::RawSample;
    }
    throw std::runtime_error("unsupported terrain placement: " + std::string(name));
}

std::string_view terrain_material_mode_name(TerrainMaterialMode mode) noexcept {
    return mode == TerrainMaterialMode::Flat ? "flat" : "filtered-detail";
}

TerrainMaterialMode terrain_material_mode_from_name(std::string_view name) {
    if (name.empty() || name == "filtered-detail" || name == "detail") {
        return TerrainMaterialMode::FilteredDetail;
    }
    if (name == "flat") {
        return TerrainMaterialMode::Flat;
    }
    throw std::runtime_error("unsupported terrain material: " + std::string(name));
}

std::string_view terrain_surface_model_name(TerrainSurfaceModel model) noexcept {
    switch (model) {
    case TerrainSurfaceModel::MineralControl:
        return "mineral-control";
    case TerrainSurfaceModel::LandformTransition:
        return "landform-transition";
    case TerrainSurfaceModel::ClimateTransition:
        return "climate-transition";
    }
    return "mineral-control";
}

TerrainSurfaceModel terrain_surface_model_from_name(std::string_view name) {
    if (name.empty() || name == "mineral-control") {
        return TerrainSurfaceModel::MineralControl;
    }
    if (name == "landform-transition") {
        return TerrainSurfaceModel::LandformTransition;
    }
    if (name == "climate-transition") {
        return TerrainSurfaceModel::ClimateTransition;
    }
    throw std::runtime_error("unsupported terrain surface model: " + std::string(name));
}

void validate_terrain_runtime_config(const TerrainRuntimeConfig& config) {
    if (config.heightfield_path.empty()) {
        throw std::runtime_error("terrain heightfield path must not be empty");
    }
    if (config.surface_model == TerrainSurfaceModel::ClimateTransition &&
        config.surface_fields_path.empty()) {
        throw std::runtime_error("climate terrain surface model requires surface fields");
    }
    if (!std::isfinite(config.initial_foreground_height_m) ||
        config.initial_foreground_height_m < 2.0F ||
        config.initial_foreground_height_m > 1'000.0F) {
        throw std::runtime_error("terrain foreground height must be within [2, 1000] meters");
    }
    if (config.initial_azimuth_radians.has_value() &&
        !std::isfinite(config.initial_azimuth_radians.value())) {
        throw std::runtime_error("terrain initial azimuth must be finite");
    }
    if (config.initial_orbit_radius_m.has_value() &&
        (!std::isfinite(config.initial_orbit_radius_m.value()) ||
         config.initial_orbit_radius_m.value() < 50.0F ||
         config.initial_orbit_radius_m.value() > 1'000.0F)) {
        throw std::runtime_error("terrain orbit radius must be within [50, 1000] meters");
    }
    constexpr float maximum_elevation = 30.0F * std::numbers::pi_v<float> / 180.0F;
    if (config.initial_elevation_radians.has_value() &&
        (!std::isfinite(config.initial_elevation_radians.value()) ||
         config.initial_elevation_radians.value() < 0.0F ||
         config.initial_elevation_radians.value() > maximum_elevation)) {
        throw std::runtime_error("terrain orbit elevation must be within [0, 30] degrees");
    }
    if (config.render_stride < 1U || config.render_stride > 3U) {
        throw std::runtime_error("terrain render stride must be 1, 2, or 3");
    }
    if (!std::isfinite(config.aerial_perspective_strength) ||
        config.aerial_perspective_strength < 0.0F || config.aerial_perspective_strength > 1.0F) {
        throw std::runtime_error("terrain aerial perspective must be within [0, 1]");
    }
}

TerrainRuntimeConfig terrain_runtime_config_from_options(
    const TerrainStartupOptions& options, TerrainDebugView debug_view,
    const std::filesystem::path& default_heightfield_path,
    const std::filesystem::path& default_surface_fields_path) {
    TerrainRuntimeConfig result;
    result.heightfield_path =
        options.heightfield_path.has_value() && !options.heightfield_path->empty()
            ? options.heightfield_path.value()
            : default_heightfield_path;
    if (options.surface_fields_path.has_value() && !options.surface_fields_path->empty()) {
        result.surface_fields_path = options.surface_fields_path.value();
    } else if (!options.heightfield_path.has_value() || options.heightfield_path->empty()) {
        result.surface_fields_path = default_surface_fields_path;
    }
    result.surface_model = options.surface_model.value_or(TerrainSurfaceModel::MineralControl);
    result.expected_seed = options.seed;
    result.placement = options.placement.value_or(TerrainPlacementMode::Selected);
    result.placement_index = options.placement_index.value_or(0U);
    result.initial_foreground_height_m = options.foreground_height_m.value_or(200.0F);
    if (options.camera_preset.has_value()) {
        result.foreground_sphere =
            options.camera_preset.value() == TerrainCameraPreset::BackdropStage;
    }
    result.material = options.surface_detail.value_or(TerrainMaterialMode::FilteredDetail);
    result.aerial_perspective_strength =
        options.aerial_perspective_strength.value_or(kTerrainDefaultAerialPerspectiveStrength);
    result.shadows = options.shadows.value_or(true);
    result.debug_view = debug_view;
    constexpr float degrees_to_radians = std::numbers::pi_v<float> / 180.0F;
    if (options.backdrop_azimuth_degrees.has_value()) {
        result.initial_azimuth_radians =
            options.backdrop_azimuth_degrees.value() * degrees_to_radians;
    }
    result.initial_orbit_radius_m = options.backdrop_orbit_radius_m;
    if (options.backdrop_elevation_degrees.has_value()) {
        result.initial_elevation_radians =
            options.backdrop_elevation_degrees.value() * degrees_to_radians;
    }
    result.render_stride = options.render_stride.value_or(3U);
    validate_terrain_runtime_config(result);
    return result;
}

cubey::AtmosphereEnvironmentRunState terrain_atmosphere_state_from_options(
    const cubey::AtmosphereEnvironmentOptions& atmosphere) {
    cubey::validate_atmosphere_environment_options(atmosphere);
    cubey::AtmosphereEnvironmentOptions resolved = atmosphere;
    const bool explicit_clock = resolved.time_hours.has_value() ||
                                resolved.day_of_year.has_value() ||
                                resolved.latitude_degrees.has_value();
    const bool explicit_sun = resolved.sun_elevation_degrees.has_value() ||
                              resolved.sun_azimuth_degrees.has_value();
    if (!resolved.time_of_day_mode.has_value() && !explicit_clock && !explicit_sun) {
        resolved.time_of_day_mode = "solar";
        resolved.time_hours = kTerrainDefaultTimeHours;
    }
    return cubey::atmosphere_environment_run_state_from_config(
        resolved,
        {
            .sun_elevation_degrees = 38.0F,
            .sun_azimuth_degrees = -42.0F,
            .ground_mode = cubey::render::AtmosphereEnvironmentGroundMode::SkyOnlyNoGroundOcclusion,
            .reference_geometry_enabled = false,
        });
}

cubey::CloudEnvironmentConfig terrain_cloud_config_from_options(
    const cubey::CloudEnvironmentOptions& clouds,
    const cubey::render::AtmosphereEnvironmentConfig& atmosphere) {
    cubey::CloudEnvironmentConfig result{};
    cubey::apply_cloud_environment_weather_preset(
        result, cubey::CloudEnvironmentWeatherPreset::FairWeather);
    cubey::apply_cloud_environment_options(result, clouds);
    cubey::apply_cloud_environment_surface_v1_policy(result);
    result.layer.planet_radius_m = atmosphere.bottom_radius_km * 1000.0F;
    result.layer.background_mode = cubey::render::CloudLayerBackgroundMode::Atmosphere;
    result.layer.distance_mode = cubey::render::CloudLayerDistanceMode::Local;
    return result;
}

cubey::config::Schema terrain_project_config_schema(TerrainProjectConfig& config) {
    auto builder = cubey::config::Schema::builder().compose(
        cubey::host::common_run_config_schema(config.common));
    bind_debug_view(builder, config);
    builder
        .bind(option("terrain.heightfield", "--terrain-heightfield", "Heightfield", "Terrain",
                     "Runtime raster heightfield manifest or directory.", ValueType::Path),
              config.terrain.heightfield_path)
        .bind(option("terrain.surface_fields", "--terrain-surface-fields", "Surface Fields",
                     "Terrain", "Optional climate companion manifest or directory.",
                     ValueType::Path),
              config.terrain.surface_fields_path)
        .bind(option("terrain.seed", "--terrain-seed", "Seed", "Terrain",
                     "Expected source seed; assignment is preserved explicitly.",
                     ValueType::UInt64),
              config.terrain.seed)
        .bind(option("terrain.surface_model", "--terrain-surface-model", "Surface Model",
                     "Terrain", "Terrain surface material model.", ValueType::Enum, {},
                     {"mineral-control", "landform-transition", "climate-transition"}),
              config.terrain.surface_model)
        .bind(option("terrain.placement", "--terrain-placement", "Placement", "Terrain",
                     "Startup terrain source placement.", ValueType::Enum, {},
                     {"selected", "raw-center", "raw-sample"}),
              config.terrain.placement)
        .bind(option("terrain.placement_index", "--terrain-placement-index", "Placement Index",
                     "Terrain", "Deterministic raw-sample placement index.", ValueType::UInt32),
              config.terrain.placement_index)
        .bind(option("terrain.foreground_height_m", "--terrain-foreground-height",
                     "Foreground Height", "Terrain",
                     "Initial foreground and orbit focus height in meters.", ValueType::Float,
                     {.has_min = true, .has_max = true, .min = 0.0, .max = 1000.0}),
              config.terrain.foreground_height_m);
    bind_camera_preset(builder, config);
    bind_surface_detail(builder, config);
    OptionSpec terrain_shadows = option(
        "terrain.shadows", "--terrain-shadows", "Shadows", "Terrain",
        "Enable cached directional terrain shadows.", ValueType::Bool);
    terrain_shadows.negative_cli_name = "--no-terrain-shadows";
    builder
        .bind(std::move(terrain_shadows), config.terrain.shadows)
        .bind(option("terrain.aerial_perspective_strength", "--terrain-aerial-perspective",
                     "Aerial Perspective", "Terrain",
                     "Aerial perspective strength for the terrain backdrop.", ValueType::Float,
                     {.has_min = true, .has_max = true, .min = 0.0, .max = 1.0}),
              config.terrain.aerial_perspective_strength)
        .bind(option("terrain.render_stride", "--terrain-render-stride", "Render Stride",
                     "Terrain", "Cached terrain topology stride.", ValueType::UInt32,
                     {.has_min = true, .has_max = true, .min = 1.0, .max = 3.0}),
              config.terrain.render_stride)
        .bind(option("terrain.backdrop_azimuth_degrees", "--terrain-backdrop-azimuth",
                     "Initial Azimuth", "Terrain", "Initial backdrop orbit azimuth in degrees.",
                     ValueType::Float,
                     {.has_min = true, .has_max = true, .min = -360.0, .max = 360.0}),
              config.terrain.backdrop_azimuth_degrees)
        .bind(option("terrain.backdrop_orbit_radius_m", "--terrain-backdrop-orbit-radius",
                     "Orbit Radius", "Terrain", "Initial backdrop orbit radius in meters.",
                     ValueType::Float,
                     {.has_min = true, .has_max = true, .min = 50.0, .max = 1000.0}),
              config.terrain.backdrop_orbit_radius_m)
        .bind(option("terrain.backdrop_elevation_degrees", "--terrain-backdrop-elevation",
                     "Orbit Elevation", "Terrain", "Initial backdrop orbit elevation in degrees.",
                     ValueType::Float,
                     {.has_min = true, .has_max = true, .min = 0.0, .max = 30.0}),
              config.terrain.backdrop_elevation_degrees);
    builder.compose(cubey::atmosphere_environment_schema(config.atmosphere));
    builder.compose(cubey::cloud_environment_schema(config.clouds));
    return std::move(builder).build();
}

TerrainProjectConfig parse_terrain_project_config(int argc, char** argv,
                                                  cubey::config::ParseResult* result) {
    TerrainProjectConfig config = cubey::host::parse_configured_app<TerrainProjectConfig>(
        argc, argv, terrain_project_config_schema, result);
    cubey::validate_atmosphere_environment_options(config.atmosphere);
    cubey::validate_cloud_environment_options(config.clouds);
    return config;
}

void resolve_terrain_project_config(TerrainProjectConfig& config,
                                    const std::filesystem::path& default_heightfield_path,
                                    const std::filesystem::path& default_surface_fields_path) {
    config.runtime = terrain_runtime_config_from_options(
        config.terrain, config.debug_view.value_or(TerrainDebugView::Surface),
        default_heightfield_path, default_surface_fields_path);
}

} // namespace cubey::projects::terrain
