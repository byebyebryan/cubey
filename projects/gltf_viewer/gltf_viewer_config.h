#pragma once

#include <cubey/engine/atmosphere_environment_schema.h>
#include <cubey/engine/cloud_environment_schema.h>
#include <cubey/engine/ocean_surface_runtime.h>
#include <cubey/engine/ocean_surface_schema.h>
#include <cubey/engine/pbr_environment_schema.h>
#include <cubey/host/configured_app.h>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace cubey::projects::gltf_viewer {

inline constexpr float kGltfViewerMaximumCaptureOrbitDegrees = 180.0F;
inline constexpr float kGltfViewerMinimumCaptureCameraDistanceScale = 0.5F;
inline constexpr float kGltfViewerMaximumCaptureCameraDistanceScale = 2.0F;

struct GltfViewerCaptureOptions {
    std::optional<float> video_orbit_degrees{};
    std::optional<float> camera_distance_scale{};
};

struct GltfViewerStartupOptions {
    struct Gltf {
        std::filesystem::path input_path{};
        std::uint32_t animation_index = 0U;
        float animation_speed = 1.0F;
        bool animation_paused = false;
    } gltf;

    struct Ocean : cubey::OceanSurfaceOptions {
        std::optional<bool> backdrop{};
        std::optional<float> foreground_height_m{};
    } ocean;

    cubey::AtmosphereEnvironmentOptions atmosphere;
    cubey::CloudEnvironmentOptions clouds;
    GltfViewerCaptureOptions capture{};
    struct Pbr : cubey::PbrStaticIblOptions {
        std::optional<std::string> environment_source{};
    } pbr;

    std::string debug_view{};

    struct Terrain {
        std::optional<std::filesystem::path> heightfield_path{};
        std::optional<std::uint32_t> render_stride{};
        std::optional<std::string> surface_detail{};
        std::optional<float> foreground_height_m{};
        std::optional<bool> shadows{};
    } terrain;
};

struct GltfViewerProjectConfig : GltfViewerStartupOptions {
    host::CommonRunConfig common;
};

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

} // namespace detail

using config::OptionSpec;
using config::ValueType;

inline config::Schema gltf_viewer_project_config_schema(GltfViewerProjectConfig& config) {
    auto builder = config::Schema::builder().compose(host::common_run_config_schema(config.common));
    builder.bind(detail::option("gltf.input", "--input", "Input", "glTF", "glTF or GLB asset path.",
                                ValueType::Path),
                 config.gltf.input_path);
    builder.bind(detail::option("gltf.animation_index", "--animation-index", "Animation Index",
                                "glTF", "Animation clip index to play.", ValueType::UInt32),
                 config.gltf.animation_index);
    builder.bind(detail::option("gltf.animation_speed", "--animation-speed", "Animation Speed",
                                "glTF", "Animation playback speed multiplier.", ValueType::Float),
                 config.gltf.animation_speed);
    builder.bind(detail::option("gltf.animation_paused", "--pause-animation", "Pause Animation",
                                "glTF", "Start glTF animation playback paused.", ValueType::Bool),
                 config.gltf.animation_paused);

    builder.bind(detail::option("gltf.capture.video_orbit_degrees", "--capture-video-orbit-degrees",
                                "Video Orbit", "Capture",
                                "Optional bounded glTF video orbit in total degrees; smoothstep "
                                "easing runs from the initial to final scene-bounds view.",
                                ValueType::Float,
                                {.has_min = true,
                                 .has_max = true,
                                 .min = 0.0,
                                 .max = kGltfViewerMaximumCaptureOrbitDegrees}),
                 config.capture.video_orbit_degrees);
    builder.bind(detail::option("gltf.capture.camera_distance_scale",
                                "--capture-camera-distance-scale", "Camera Distance Scale",
                                "Capture",
                                "Optional scene-bounds-relative capture camera distance scale.",
                                ValueType::Float,
                                {.has_min = true,
                                 .has_max = true,
                                 .min = kGltfViewerMinimumCaptureCameraDistanceScale,
                                 .max = kGltfViewerMaximumCaptureCameraDistanceScale}),
                 config.capture.camera_distance_scale);

    builder.compose(cubey::pbr_static_ibl_schema(config.pbr));
    builder.bind(detail::option("pbr.environment_source", "--pbr-environment-source",
                                "Environment Source", "PBR",
                                "Choose static IBL or the procedural atmosphere environment.",
                                ValueType::Enum, {}, {"static", "atmosphere"}),
                 config.pbr.environment_source);

    builder.compose(cubey::atmosphere_environment_schema(config.atmosphere));
    builder.compose(cubey::cloud_environment_schema(config.clouds));
    builder.compose(cubey::ocean_surface_schema(config.ocean));
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
    builder.bind(detail::option("debug_view", "--debug-view", "Debug View", "PBR",
                                "PBR debug view.", ValueType::String),
                 config.debug_view);

    builder.bind(detail::option("terrain.heightfield", "--terrain-heightfield", "Heightfield",
                                "Terrain", "Terrain backdrop heightfield.", ValueType::Path),
                 config.terrain.heightfield_path);
    builder.bind(detail::option(
                     "terrain.render_stride", "--terrain-render-stride", "Render Stride", "Terrain",
                     "Cached topology stride used for terrain geometry comparison captures.",
                     ValueType::UInt32, {.has_min = true, .has_max = true, .min = 1.0, .max = 3.0}),
                 config.terrain.render_stride);
    builder.bind(detail::option("terrain.surface_detail", "--terrain-surface-detail",
                                "Surface Detail", "Terrain", "Terrain material detail.",
                                ValueType::Enum, {}, {"flat", "filtered-detail"}),
                 config.terrain.surface_detail);
    builder.bind(detail::option("terrain.foreground_height_m", "--terrain-foreground-height",
                                "Foreground Height", "Terrain", "Terrain foreground height.",
                                ValueType::Float,
                                {.has_min = true, .has_max = true, .min = 0.0, .max = 1000.0}),
                 config.terrain.foreground_height_m);
    OptionSpec terrain_shadows = detail::option("terrain.shadows", "--terrain-shadows", "Shadows",
                                                "Terrain", "Terrain shadows.", ValueType::Bool);
    terrain_shadows.negative_cli_name = "--no-terrain-shadows";
    builder.bind(std::move(terrain_shadows), config.terrain.shadows);
    return std::move(builder).build();
}

// The glTF viewer shares the ocean surface runtime for its optional backdrop,
// but keeps the legacy viewer contract: its PBR debug view does not select an
// ocean diagnostic pass and cloud reflections use the cached environment.
inline cubey::render::OceanSurfaceConfig
gltf_viewer_ocean_config_from_options(const GltfViewerStartupOptions& config) {
    cubey::render::OceanSurfaceConfig ocean =
        cubey::ocean_surface_config_from_options(config.ocean);
    ocean.render_view = cubey::render::OceanRenderView::Final;
    ocean.cloud_reflection_source = cubey::render::OceanCloudReflectionSource::CachedEnvironment;
    ocean.exposure = config.pbr.exposure;
    return ocean;
}

inline GltfViewerProjectConfig
parse_gltf_viewer_project_config(int argc, char** argv, config::ParseResult* result = nullptr) {
    GltfViewerProjectConfig config = host::parse_configured_app<GltfViewerProjectConfig>(
        argc, argv, gltf_viewer_project_config_schema, result);
    validate_atmosphere_environment_options(config.atmosphere);
    validate_cloud_environment_options(config.clouds);
    return config;
}

} // namespace cubey::projects::gltf_viewer
