#pragma once

#include <cubey/engine/atmosphere_environment_schema.h>
#include <cubey/engine/cloud_environment_schema.h>
#include <cubey/engine/ocean_surface_runtime.h>
#include <cubey/engine/ocean_surface_schema.h>
#include <cubey/engine/pbr_environment_schema.h>
#include <cubey/host/configured_app.h>
#include <cubey/render/pbr.h>

#include <nlohmann/json.hpp>

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <numbers>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace cubey::projects::gltf_viewer {

inline constexpr float kGltfViewerMaximumCaptureOrbitDegrees = 180.0F;
inline constexpr float kGltfViewerMinimumCaptureCameraDistanceScale = 0.1F;
inline constexpr float kGltfViewerMaximumCaptureCameraDistanceScale = 2.0F;
inline constexpr float kGltfViewerMinimumCaptureCameraYawDegrees = -180.0F;
inline constexpr float kGltfViewerMaximumCaptureCameraYawDegrees = 180.0F;
inline constexpr float kGltfViewerMinimumCaptureCameraPitchDegrees = -89.0F;
inline constexpr float kGltfViewerMaximumCaptureCameraPitchDegrees = 89.0F;
inline constexpr float kGltfViewerDefaultCaptureCameraFovYDegrees = 60.0F;
inline constexpr float kGltfViewerMinimumCaptureCameraFovYDegrees = 1.0F;
inline constexpr float kGltfViewerMaximumCaptureCameraFovYDegrees = 179.0F;

struct GltfViewerCaptureOptions {
    std::optional<float> video_orbit_degrees{};
    std::optional<float> camera_distance_scale{};
    std::optional<float> camera_yaw_degrees{};
    std::optional<float> camera_pitch_degrees{};
    std::optional<float> camera_fov_y_degrees{};
};

// This is deliberately a profiling trigger rather than an asset-loading
// policy. It lets a windowed measurement start the requested import after the
// renderer has reached a stable cadence.
struct GltfViewerProfileOptions {
    std::uint32_t import_delay_frames = 0U;
    std::optional<double> upload_owner_cpu_target_milliseconds{};
    std::optional<std::uint64_t> upload_step_byte_cap{};
    std::optional<double> windowed_frame_pacing_hertz{};
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
    GltfViewerProfileOptions profile{};
    struct Pbr : cubey::PbrStaticIblOptions {
        std::optional<std::string> environment_source{};
        std::optional<std::string> tonemap{};
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

// The selected PBR source is a viewer-wide rendering policy, not a collection
// of independent background and lighting toggles.  Keeping the derived
// behavior here makes static captures reproducible: no procedural atmosphere
// product is allowed to leak into an otherwise HDR/generated IBL render.
enum class GltfViewerEnvironmentSource : std::uint8_t {
    StaticIbl,
    Atmosphere,
};

struct GltfViewerEnvironmentPolicy {
    GltfViewerEnvironmentSource source = GltfViewerEnvironmentSource::Atmosphere;

    [[nodiscard]] bool uses_atmosphere_resources() const noexcept {
        return source == GltfViewerEnvironmentSource::Atmosphere;
    }

    [[nodiscard]] bool uses_atmosphere_background() const noexcept {
        return uses_atmosphere_resources();
    }

    [[nodiscard]] bool uses_atmosphere_diffuse_irradiance() const noexcept {
        return uses_atmosphere_resources();
    }

    [[nodiscard]] bool uses_procedural_direct_light() const noexcept {
        return uses_atmosphere_resources();
    }

    [[nodiscard]] bool uses_atmosphere_auto_exposure() const noexcept {
        return uses_atmosphere_resources();
    }

    [[nodiscard]] bool advances_atmosphere() const noexcept {
        return uses_atmosphere_resources();
    }

    [[nodiscard]] bool shows_atmosphere_controls() const noexcept {
        return uses_atmosphere_resources();
    }

    [[nodiscard]] bool uses_ibl_skybox() const noexcept {
        return source == GltfViewerEnvironmentSource::StaticIbl;
    }
};

[[nodiscard]] inline GltfViewerEnvironmentPolicy
resolve_gltf_viewer_environment_policy(const std::optional<std::string>& environment_source) {
    if (!environment_source.has_value() || *environment_source == "atmosphere") {
        return {.source = GltfViewerEnvironmentSource::Atmosphere};
    }
    if (*environment_source == "static") {
        return {.source = GltfViewerEnvironmentSource::StaticIbl};
    }
    throw std::runtime_error("glTF PBR environment source must be static or atmosphere");
}

// The viewer deliberately exposes only the two display transforms already
// supported by the forward PBR renderer.  Keeping this opt-in avoids changing
// the established ACES presentation while allowing controlled capture work to
// request the linear, clamped post path explicitly.
[[nodiscard]] inline cubey::render::PbrTonemap
resolve_gltf_viewer_pbr_tonemap(const std::optional<std::string>& tonemap) {
    if (!tonemap.has_value() || *tonemap == "aces") {
        return cubey::render::PbrTonemap::Aces;
    }
    if (*tonemap == "linear") {
        return cubey::render::PbrTonemap::Linear;
    }
    throw std::runtime_error("glTF PBR tonemap must be linear or aces");
}

[[nodiscard]] inline float gltf_viewer_capture_camera_fovy_radians(
    const std::optional<float>& fov_y_degrees) {
    const float degrees = fov_y_degrees.value_or(kGltfViewerDefaultCaptureCameraFovYDegrees);
    if (!std::isfinite(degrees) || degrees < kGltfViewerMinimumCaptureCameraFovYDegrees ||
        degrees > kGltfViewerMaximumCaptureCameraFovYDegrees) {
        throw std::runtime_error("glTF capture camera vertical FOV is outside its supported range");
    }
    return degrees * (std::numbers::pi_v<float> / 180.0F);
}

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
    builder.bind(detail::option("gltf.capture.camera_yaw_degrees", "--capture-camera-yaw",
                                "Camera Yaw", "Capture",
                                "Optional absolute capture camera yaw in degrees.",
                                ValueType::Float,
                                {.has_min = true,
                                 .has_max = true,
                                 .min = kGltfViewerMinimumCaptureCameraYawDegrees,
                                 .max = kGltfViewerMaximumCaptureCameraYawDegrees}),
                 config.capture.camera_yaw_degrees);
    builder.bind(detail::option("gltf.capture.camera_pitch_degrees", "--capture-camera-pitch",
                                "Camera Pitch", "Capture",
                                "Optional absolute capture camera pitch in degrees.",
                                ValueType::Float,
                                {.has_min = true,
                                 .has_max = true,
                                 .min = kGltfViewerMinimumCaptureCameraPitchDegrees,
                                 .max = kGltfViewerMaximumCaptureCameraPitchDegrees}),
                 config.capture.camera_pitch_degrees);
    builder.bind(detail::option("gltf.capture.camera_fov_y_degrees",
                                "--capture-camera-fov-y-degrees", "Camera Vertical FOV",
                                "Capture",
                                "Optional capture camera vertical field of view in degrees.",
                                ValueType::Float,
                                {.has_min = true,
                                 .has_max = true,
                                 .min = kGltfViewerMinimumCaptureCameraFovYDegrees,
                                 .max = kGltfViewerMaximumCaptureCameraFovYDegrees}),
                 config.capture.camera_fov_y_degrees);

    builder.bind(detail::option("profile.import_delay_frames", "--profile-import-delay-frames",
                                "Import Delay Frames", "Profiling",
                                "Delay the initial windowed glTF import until this frame; "
                                "profiling-only and ignored by ordinary runs when zero.",
                                ValueType::UInt32),
                 config.profile.import_delay_frames);
    builder.bind(detail::option("profile.upload_owner_cpu_target_ms",
                                "--profile-upload-owner-target-ms", "Upload Owner Target",
                                "Profiling",
                                "Profiling-only glTF upload owner CPU target in milliseconds.",
                                ValueType::Float, {.has_min = true, .min = 0.001}),
                 config.profile.upload_owner_cpu_target_milliseconds);
    builder.bind(detail::option("profile.upload_step_byte_cap", "--profile-upload-step-byte-cap",
                                "Upload Step Byte Cap", "Profiling",
                                "Profiling-only glTF physical upload-step cap in bytes.",
                                ValueType::UInt64, {.has_min = true, .min = 1.0}),
                 config.profile.upload_step_byte_cap);
    builder.bind(detail::option("profile.windowed_frame_pacing_hz", "--profile-frame-pace-hz",
                                "Windowed Frame Pace", "Profiling",
                                "Profiling-only whole-frame windowed pacing frequency in hertz.",
                                ValueType::Float,
                                {.has_min = true, .has_max = true, .min = 1.0, .max = 1000.0}),
                 config.profile.windowed_frame_pacing_hertz);

    builder.compose(cubey::pbr_static_ibl_schema(config.pbr));
    builder.bind(detail::option("pbr.environment_source", "--pbr-environment-source",
                                "Environment Source", "PBR",
                                "Choose static IBL or the procedural atmosphere environment.",
                                ValueType::Enum, {}, {"static", "atmosphere"}),
                 config.pbr.environment_source);
    builder.bind(detail::option("pbr.tonemap", "--pbr-tonemap", "Tone Map", "PBR",
                                "Choose the final PBR display transform.", ValueType::Enum, {},
                                {"linear", "aces"}),
                 config.pbr.tonemap);

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
    if (config.profile.upload_owner_cpu_target_milliseconds.has_value() &&
        (!std::isfinite(config.profile.upload_owner_cpu_target_milliseconds.value()) ||
         config.profile.upload_owner_cpu_target_milliseconds.value() <= 0.0)) {
        throw std::runtime_error("glTF profile upload owner target must be finite and positive");
    }
    if (config.profile.windowed_frame_pacing_hertz.has_value() &&
        (!std::isfinite(config.profile.windowed_frame_pacing_hertz.value()) ||
         config.profile.windowed_frame_pacing_hertz.value() <= 0.0)) {
        throw std::runtime_error("glTF profile frame pacing must be finite and positive");
    }
    return config;
}

} // namespace cubey::projects::gltf_viewer
