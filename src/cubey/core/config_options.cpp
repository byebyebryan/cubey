#include <cubey/core/config_options.h>

#include <lazy/adapters/json_nlohmann.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using cubey::ConfigEnumChoices;
using cubey::ConfigOptionDescriptor;
using cubey::ConfigOptionRange;
using cubey::ConfigOptionType;
using cubey::RunConfig;
using cubey::RunConfigOptionId;

constexpr ConfigOptionRange no_range() {
    return {};
}

constexpr ConfigOptionRange min_range(double min) {
    return {.has_min = true, .min = min};
}

constexpr ConfigOptionRange bounded_range(double min, double max) {
    return {.has_min = true, .has_max = true, .min = min, .max = max};
}

template <std::size_t Count>
constexpr ConfigEnumChoices enum_choices(const std::array<std::string_view, Count>& values) {
    return {.values = values.data(), .count = values.size()};
}

constexpr std::array<std::string_view, 2> kCaptureModes{"png", "video"};
constexpr std::array<std::string_view, 2> kPbrEnvironmentSources{"static", "atmosphere"};
constexpr std::array<std::string_view, 6> kOceanCascades{"all", "0", "1", "2", "3", "4"};
constexpr std::array<std::string_view, 2> kOceanFieldPrecisions{"full", "half"};
constexpr std::array<std::string_view, 2> kOceanSurfaceModes{"flat", "curved-far"};
constexpr std::array<std::string_view, 7> kOceanCameraPresets{
    "default", "low", "mid", "high", "close", "overhead", "wide",
};
constexpr std::array<std::string_view, 2> kPlanetScalePresets{"earthlike", "mini"};
constexpr std::array<std::string_view, 2> kPlanetCameraModes{"orbit", "surface"};
constexpr std::array<std::string_view, 3> kPlanetSurfaceLooks{"default", "sun", "antisun"};
constexpr std::array<std::string_view, 3> kPlanetAtmosphereModes{"analytic", "physical",
                                                                 "physical-preview"};
constexpr std::array<std::string_view, 2> kTimeOfDayModes{"manual", "solar"};
constexpr std::array<std::string_view, 2> kNightSkyModes{"human", "camera"};
constexpr std::array<std::string_view, 6> kMilkyWayLayers{
    "final", "stellar-emission", "dust-tau", "star-clouds", "hii-emission", "speckles",
};
constexpr std::array<std::string_view, 6> kCloudCameraModes{
    "surface", "surface-up", "high", "high-oblique", "orbit", "orbit-terminator"};
constexpr std::array<std::string_view, 3> kCloudQualities{"quarter", "half", "full"};
constexpr std::array<std::string_view, 4> kCloudCacheFrames{"4", "16", "64", "256"};
constexpr std::array<std::string_view, 4> kCloudRenderPaths{
    "cached", "direct", "diff", "alpha-diff"};
constexpr std::array<std::string_view, 4> kCloudSamplingModes{
    "interleaved", "bayer", "blue-noise", "off"};
constexpr std::array<std::string_view, 3> kCloudDensityModels{"ref-density", "procedural",
                                                              "cloud-ref-compatible"};
constexpr std::array<std::string_view, 2> kCloudResolveModes{"terrain-post",
                                                             "metadata-bilateral"};
constexpr std::array<std::string_view, 2> kCloudBackgroundModes{"atmosphere", "water-context"};
constexpr std::array<std::string_view, 4> kCloudDistanceModes{"auto", "local", "orbit-shell",
                                                              "blend-debug"};
constexpr std::array<std::string_view, 2> kCloudOrbitRepresentations{"volume",
                                                                     "surface-shell"};
constexpr std::array<std::string_view, 10> kCloudWeatherPresets{
    "fair-weather",     "broken-cumulus", "overcast-stratus", "storm-cells",
    "high-cirrus",      "clear",          "scattered",        "inspection",
    "overcast",         "storm",
};
constexpr std::array<std::string_view, 3> kSmokePressureSolvers{"jacobi", "rbgs",
                                                                "red-black-gauss-seidel"};
constexpr std::array<std::string_view, 4> kWaterTransferModes{"apic", "pic-flip", "picflip",
                                                              "pic/flip"};
constexpr std::array<std::string_view, 4> kWater3DP2GModes{"active", "active-faces", "tiled",
                                                           "tiled-faces"};
constexpr std::array<std::string_view, 6> kTerrainLabSlicePresets{
    "arid-mesa-canyon",
    "temperate-mountain-rivers",
    "desert-dunes",
    "alpine-glacial-valley",
    "mountain-ridges-peaks",
    "temperate-mountain-watershed",
};
constexpr std::array<std::string_view, 2> kTerrainLabCameraPresets{
    "orbit",
    "profile",
};
constexpr std::array<std::string_view, 3> kTerrainLabNoiseSources{
    "legacy-value",
    "fastnoise-lite",
    "fastnoise-lite-warped",
};
constexpr double kPlanetMaxPatchResolution = 128.0;
constexpr double kPlanetMaxLiveLodLevel = 12.0;
constexpr double kPlanetMaxLocalDetailLodLevels = 8.0;
constexpr double kPlanetMaxLocalDetailCellsPerAxis = 512.0;

constexpr ConfigOptionDescriptor option(RunConfigOptionId id, std::string_view path,
                                        std::string_view cli_name, std::string_view label,
                                        std::string_view group_path, std::string_view help,
                                        ConfigOptionType type, ConfigOptionRange range = no_range(),
                                        ConfigEnumChoices choices = {},
                                        std::string_view negative_cli_name = {}) {
    return {
        .id = id,
        .path = path,
        .cli_name = cli_name,
        .negative_cli_name = negative_cli_name,
        .label = label,
        .group_path = group_path,
        .help = help,
        .type = type,
        .range = range,
        .enum_choices = choices,
    };
}

constexpr std::array<ConfigOptionDescriptor, 241> kRunConfigOptions{
    option(RunConfigOptionId::Title, "title", "--title", "Title", "App",
           "Window title. Project defaults are applied when this remains cubey.",
           ConfigOptionType::String),
    option(RunConfigOptionId::Width, "width", "--width", "Width", "Window",
           "Initial render width in pixels.", ConfigOptionType::UInt32, min_range(1.0)),
    option(RunConfigOptionId::Height, "height", "--height", "Height", "Window",
           "Initial render height in pixels.", ConfigOptionType::UInt32, min_range(1.0)),
    option(RunConfigOptionId::Frames, "frames", "--frames", "Frames", "Capture",
           "Frame limit. Zero keeps a windowed app running until closed.",
           ConfigOptionType::UInt32),
    option(RunConfigOptionId::Fps, "fps", "--fps", "FPS", "Capture", "Capture playback frame rate.",
           ConfigOptionType::UInt32, min_range(1.0)),
    option(RunConfigOptionId::OutputPath, "output", "--output", "Output", "Capture",
           "Output path for headless PNG or video capture.", ConfigOptionType::Path),
    option(RunConfigOptionId::DebugView, "debug_view", "--debug-view", "Debug View", "Debug",
           "Project-specific debug view name.", ConfigOptionType::String),
    option(RunConfigOptionId::Headless, "headless", "--headless", "Headless", "Host",
           "Run without opening a window.", ConfigOptionType::Bool),
    option(RunConfigOptionId::Validation, "validation", "--validation", "Validation", "Host",
           "Enable Vulkan validation when available.", ConfigOptionType::Bool, no_range(), {},
           "--no-validation"),
    option(RunConfigOptionId::RequireValidation, "require_validation", "--require-validation",
           "Require Validation", "Host", "Fail startup if validation layers are unavailable.",
           ConfigOptionType::Bool),
    option(RunConfigOptionId::PrintFrameStats, "print_frame_stats", "--print-frame-stats",
           "Print Frame Stats", "Host", "Print periodic frame statistics to stdout.",
           ConfigOptionType::Bool),
    option(RunConfigOptionId::CaptureMode, "capture", "--capture", "Capture", "Capture",
           "Capture output mode.", ConfigOptionType::Enum, no_range(), enum_choices(kCaptureModes)),
    option(RunConfigOptionId::GridWidth, "grid.width", "--grid-width", "Width", "Grid",
           "Project grid width. Zero or null leaves the project default in place.",
           ConfigOptionType::UInt32, min_range(1.0)),
    option(RunConfigOptionId::GridHeight, "grid.height", "--grid-height", "Height", "Grid",
           "Project grid height. Zero or null leaves the project default in place.",
           ConfigOptionType::UInt32, min_range(1.0)),
    option(RunConfigOptionId::GridDepth, "grid.depth", "--grid-depth", "Depth", "Grid",
           "Project grid depth. Zero or null leaves the project default in place.",
           ConfigOptionType::UInt32, min_range(1.0)),
    option(RunConfigOptionId::ProfileOutput, "profile.output", "--profile-output", "Profile Output",
           "Profiling",
           "Profile output prefix. Relative bare names are written under outputs/profiles.",
           ConfigOptionType::Path),
    option(RunConfigOptionId::ProfileWarmupFrames, "profile.warmup_frames",
           "--profile-warmup-frames", "Warmup Frames", "Profiling",
           "Frames to skip before profiling output starts.", ConfigOptionType::UInt32),
    option(RunConfigOptionId::ProfileDiagnostics, "profile.diagnostics", "--profile-diagnostics",
           "Diagnostics", "Profiling", "Record project diagnostics into profile output.",
           ConfigOptionType::Bool, no_range(), {}, "--no-profile-diagnostics"),
    option(RunConfigOptionId::ProfileDiagnosticInterval, "profile.diagnostic_interval",
           "--profile-diagnostic-interval", "Diagnostic Interval", "Profiling",
           "Frame interval for profile diagnostics.", ConfigOptionType::UInt32, min_range(1.0)),
    option(RunConfigOptionId::GltfInput, "gltf.input", "--input", "Input", "glTF",
           "glTF or GLB asset path.", ConfigOptionType::Path),
    option(RunConfigOptionId::GltfAnimationIndex, "gltf.animation_index", "--animation-index",
           "Animation Index", "glTF", "Animation clip index to play.", ConfigOptionType::UInt32),
    option(RunConfigOptionId::GltfAnimationSpeed, "gltf.animation_speed", "--animation-speed",
           "Animation Speed", "glTF", "Animation playback speed multiplier.",
           ConfigOptionType::Float),
    option(RunConfigOptionId::GltfAnimationPaused, "gltf.animation_paused", "--pause-animation",
           "Pause Animation", "glTF", "Start glTF animation playback paused.",
           ConfigOptionType::Bool),
    option(RunConfigOptionId::PbrEnvironment, "pbr.environment", "--environment", "Environment",
           "PBR", "HDR environment path for static IBL.", ConfigOptionType::Path),
    option(RunConfigOptionId::PbrEnvironmentSource, "pbr.environment_source",
           "--pbr-environment-source", "Environment Source", "PBR",
           "Choose static IBL or the procedural atmosphere environment.", ConfigOptionType::Enum,
           no_range(), enum_choices(kPbrEnvironmentSources)),
    option(RunConfigOptionId::PbrIblIntensity, "pbr.ibl_intensity", "--ibl-intensity",
           "IBL Intensity", "PBR", "Multiplier for image-based lighting.", ConfigOptionType::Float,
           min_range(0.0)),
    option(RunConfigOptionId::PbrEnvironmentRotation, "pbr.environment_rotation_degrees",
           "--environment-rotation-degrees", "Environment Rotation", "PBR",
           "Yaw rotation applied to the environment in degrees.", ConfigOptionType::Float),
    option(RunConfigOptionId::PbrExposure, "pbr.exposure", "--exposure", "Exposure", "PBR",
           "Manual exposure bias in stops.", ConfigOptionType::Float, bounded_range(-4.0, 4.0)),
    option(RunConfigOptionId::OceanMapSize, "ocean.map_size", "--ocean-map-size", "Map Size",
           "Ocean", "FFT map size for the active ocean project.", ConfigOptionType::UInt32,
           min_range(1.0)),
    option(RunConfigOptionId::OceanFieldPrecision, "ocean.field_precision",
           "--ocean-field-precision", "Field Precision", "Ocean",
           "Storage precision for ocean FFT wave fields.", ConfigOptionType::Enum, no_range(),
           enum_choices(kOceanFieldPrecisions)),
    option(RunConfigOptionId::OceanSurfaceMode, "ocean.surface_mode", "--ocean-surface-mode",
           "Surface Mode", "Ocean", "Ocean surface mapping mode: flat or curved far field.",
           ConfigOptionType::Enum, no_range(), enum_choices(kOceanSurfaceModes)),
    option(RunConfigOptionId::OceanCameraPreset, "ocean.camera_preset",
           "--ocean-camera-preset", "Camera Preset", "Ocean",
           "Initial ocean camera preset for repeatable captures.", ConfigOptionType::Enum,
           no_range(), enum_choices(kOceanCameraPresets)),
    option(RunConfigOptionId::OceanPlanetRadiusScale, "ocean.planet_radius_scale",
           "--ocean-planet-radius-scale", "Planet Radius Scale", "Ocean",
           "Scale applied to the atmosphere planet radius for ocean surface curvature.",
           ConfigOptionType::Float, bounded_range(0.01, 10.0)),
    option(RunConfigOptionId::OceanCurvatureStartRatio, "ocean.curvature_start_ratio",
           "--ocean-curvature-start-ratio", "Curvature Start", "Ocean",
           "Fraction of horizon distance where far-surface curvature starts.",
           ConfigOptionType::Float, bounded_range(0.0, 1.0)),
    option(RunConfigOptionId::OceanCurvatureEndRatio, "ocean.curvature_end_ratio",
           "--ocean-curvature-end-ratio", "Curvature End", "Ocean",
           "Fraction of horizon distance where far-surface curvature reaches full strength.",
           ConfigOptionType::Float, bounded_range(0.0, 1.0)),
    option(RunConfigOptionId::OceanCurvatureStrength, "ocean.curvature_strength",
           "--ocean-curvature-strength", "Curvature Strength", "Ocean",
           "Blend strength for curved far-ocean mapping.", ConfigOptionType::Float,
           bounded_range(0.0, 1.0)),
    option(RunConfigOptionId::OceanCascade, "ocean.cascade", "--ocean-cascade", "Inspect Cascade",
           "Ocean", "Cascade isolated by ocean debug views.", ConfigOptionType::Enum, no_range(),
           enum_choices(kOceanCascades)),
    option(RunConfigOptionId::OceanSpectralDomains, "ocean.spectral_domains",
           "--ocean-spectral-domains", "Spectral Domains", "Ocean",
           "Enable wavelength-domain separation between ocean cascades.", ConfigOptionType::Bool,
           no_range(), {}, "--no-ocean-spectral-domains"),
    option(RunConfigOptionId::OceanTerrainFields, "ocean.terrain_fields", "--ocean-terrain-fields",
           "Terrain Fields", "Ocean", "Enable terrain-ocean fields as an ocean influence.",
           ConfigOptionType::Bool, no_range(), {}, "--no-ocean-terrain-fields"),
    option(RunConfigOptionId::OceanWireOverlay, "ocean.wire_overlay", "--ocean-wire-overlay",
           "Wire Overlay", "Ocean", "Draw ocean mesh wire overlay.", ConfigOptionType::Bool),
    option(RunConfigOptionId::OceanWireOpacity, "ocean.wire_opacity", "--ocean-wire-opacity",
           "Wire Opacity", "Ocean", "Opacity used by the ocean wire overlay.",
           ConfigOptionType::Float, bounded_range(0.0, 1.0)),
    option(RunConfigOptionId::PlanetScalePreset, "planet.scale_preset", "--planet-scale-preset",
           "Scale Preset", "Planet", "Planet scale defaults: earthlike or mini.",
           ConfigOptionType::Enum, no_range(), enum_choices(kPlanetScalePresets)),
    option(RunConfigOptionId::PlanetRadius, "planet.radius_m", "--planet-radius-m", "Radius",
           "Planet", "Planet radius in meters.", ConfigOptionType::Float, min_range(1.0)),
    option(RunConfigOptionId::PlanetAtmosphereHeight, "planet.atmosphere_height_m",
           "--planet-atmosphere-height-m", "Atmosphere Height", "Planet",
           "Atmosphere shell height above the planet surface in meters.", ConfigOptionType::Float,
           min_range(0.0)),
    option(RunConfigOptionId::PlanetCameraAltitude, "planet.camera_altitude_m",
           "--planet-camera-altitude-m", "Camera Altitude", "Planet",
           "Default camera altitude above the planet surface in meters.", ConfigOptionType::Float,
           min_range(0.0)),
    option(RunConfigOptionId::PlanetCameraOrbitSpin,
           "planet.camera_orbit_spin_degrees_per_second",
           "--planet-camera-orbit-spin-deg-per-sec", "Orbit Spin", "Planet",
           "Headless capture orbit-camera yaw spin rate in degrees per second.",
           ConfigOptionType::Float, bounded_range(-360.0, 360.0)),
    option(RunConfigOptionId::PlanetCameraSurfacePitch, "planet.camera_surface_pitch_degrees",
           "--planet-camera-surface-pitch-deg", "Surface Pitch", "Planet",
           "Headless capture initial surface-camera pitch offset in degrees.",
           ConfigOptionType::Float, bounded_range(-90.0, 90.0)),
    option(RunConfigOptionId::PlanetCameraSurfaceYaw, "planet.camera_surface_yaw_degrees",
           "--planet-camera-surface-yaw-deg", "Surface Yaw", "Planet",
           "Headless capture initial surface-camera yaw offset in degrees.",
           ConfigOptionType::Float, bounded_range(-360.0, 360.0)),
    option(RunConfigOptionId::PlanetCameraSurfaceLook, "planet.camera_surface_look",
           "--planet-camera-surface-look", "Surface Look", "Planet",
           "Headless capture surface-camera heading preset: default, sun, or antisun.",
           ConfigOptionType::Enum, no_range(), enum_choices(kPlanetSurfaceLooks)),
    option(RunConfigOptionId::PlanetPatchesPerFace, "planet.patches_per_face",
           "--planet-patches-per-face", "Patches / Face", "Planet",
           "Root patch count per cube-sphere face.", ConfigOptionType::UInt32,
           bounded_range(1.0, 8.0)),
    option(RunConfigOptionId::PlanetPatchResolution, "planet.patch_resolution",
           "--planet-patch-resolution", "Patch Resolution", "Planet",
           "Reusable grid resolution per planet surface patch.", ConfigOptionType::UInt32,
           bounded_range(1.0, kPlanetMaxPatchResolution)),
    option(RunConfigOptionId::PlanetMaxLodLevel, "planet.max_lod_level", "--planet-max-lod-level",
           "Max LOD", "Planet", "Maximum live planet surface LOD level.", ConfigOptionType::UInt32,
           bounded_range(0.0, kPlanetMaxLiveLodLevel)),
    option(RunConfigOptionId::PlanetLodTargetEdge, "planet.lod_target_edge_px",
           "--planet-lod-target-edge-px", "LOD Target Edge", "Planet",
           "Target projected planet surface cell edge length in pixels.", ConfigOptionType::Float,
           min_range(0.000001)),
    option(RunConfigOptionId::PlanetLodHysteresis, "planet.lod_hysteresis",
           "--planet-lod-hysteresis", "LOD Hysteresis", "Planet",
           "Relative planet LOD split and merge deadband.", ConfigOptionType::Float,
           bounded_range(0.0, 0.95)),
    option(RunConfigOptionId::PlanetLocalDetailLodLevels, "planet.local_detail_lod_levels",
           "--planet-local-detail-lod-levels", "Local Detail LODs", "Planet",
           "Viewer-centered local detail clipmap LOD levels.", ConfigOptionType::UInt32,
           bounded_range(1.0, kPlanetMaxLocalDetailLodLevels)),
    option(RunConfigOptionId::PlanetLocalDetailCells, "planet.local_detail_cells_per_axis",
           "--planet-local-detail-cells", "Local Detail Cells", "Planet",
           "Cell count per local-detail clipmap axis.", ConfigOptionType::UInt32,
           bounded_range(1.0, kPlanetMaxLocalDetailCellsPerAxis)),
    option(RunConfigOptionId::PlanetLocalDetailOuterExtent,
           "planet.local_detail_outer_half_extent_m", "--planet-local-detail-outer-extent-m",
           "Local Detail Extent", "Planet",
           "Outer half extent in meters for the viewer-centered local detail clipmap.",
           ConfigOptionType::Float, min_range(1.0)),
    option(RunConfigOptionId::PlanetLocalDetail, "planet.local_detail_enabled",
           "--planet-local-detail", "Local Detail", "Planet",
           "Enable viewer-centered near-field terrain detail.", ConfigOptionType::Bool, no_range(),
           {}, "--no-planet-local-detail"),
    option(RunConfigOptionId::PlanetLocalDetailHeight, "planet.local_detail_height_strength_m",
           "--planet-local-detail-height-m", "Local Detail Height", "Planet",
           "Maximum near-field local terrain detail height in meters.", ConfigOptionType::Float,
           min_range(0.0)),
    option(RunConfigOptionId::PlanetLocalDetailScale, "planet.local_detail_scale_m",
           "--planet-local-detail-scale-m", "Local Detail Scale", "Planet",
           "World-space scale in meters for near-field local terrain detail.",
           ConfigOptionType::Float, min_range(0.000001)),
    option(RunConfigOptionId::PlanetWireOverlay, "planet.wire_overlay", "--planet-wire-overlay",
           "Wire Overlay", "Planet", "Draw planet patch wire overlay.", ConfigOptionType::Bool,
           no_range(), {}, "--no-planet-wire-overlay"),
    option(RunConfigOptionId::PlanetSkirts, "planet.skirts_enabled", "--planet-skirts",
           "Patch Skirts", "Planet", "Enable planet patch skirt geometry.", ConfigOptionType::Bool,
           no_range(), {}, "--no-planet-skirts"),
    option(RunConfigOptionId::PlanetSkirtDepthScale, "planet.skirt_depth_scale",
           "--planet-skirt-depth-scale", "Skirt Depth", "Planet",
           "Planet patch skirt depth relative to selected cell edge length.",
           ConfigOptionType::Float, min_range(0.000001)),
    option(RunConfigOptionId::PlanetTerrain, "planet.terrain_enabled", "--planet-terrain",
           "Terrain", "Planet", "Enable procedural planet terrain displacement.",
           ConfigOptionType::Bool, no_range(), {}, "--no-planet-terrain"),
    option(RunConfigOptionId::PlanetTerrainHeightScale, "planet.terrain_height_scale_m",
           "--planet-terrain-height-scale-m", "Terrain Height", "Planet",
           "Project-local terrain height scale in meters.", ConfigOptionType::Float, min_range(0.0)),
    option(RunConfigOptionId::PlanetTerrainNoiseScale, "planet.terrain_noise_scale",
           "--planet-terrain-noise-scale", "Terrain Noise", "Planet",
           "Project-local procedural terrain noise scale.", ConfigOptionType::Float,
           min_range(0.000001)),
    option(RunConfigOptionId::PlanetTerrainMidDetailStrength, "planet.terrain_mid_detail_strength",
           "--planet-terrain-mid-detail-strength", "Terrain Mid Detail", "Planet",
           "Project-local terrain mid-scale ridge strength.", ConfigOptionType::Float,
           min_range(0.0)),
    option(RunConfigOptionId::PlanetTerrainFineDetailStrength,
           "planet.terrain_fine_detail_strength", "--planet-terrain-fine-detail-strength",
           "Terrain Fine Detail", "Planet", "Project-local terrain fine detail strength.",
           ConfigOptionType::Float, min_range(0.0)),
    option(RunConfigOptionId::PlanetTerrainFineDetailScale, "planet.terrain_fine_detail_scale",
           "--planet-terrain-fine-detail-scale", "Terrain Fine Scale", "Planet",
           "Frequency scale used by project-local terrain fine detail.", ConfigOptionType::Float,
           min_range(0.000001)),
    option(RunConfigOptionId::PlanetTerrainSeed, "planet.terrain_seed", "--planet-terrain-seed",
           "Terrain Seed", "Planet", "Project-local planet terrain seed.",
           ConfigOptionType::UInt32),
    option(RunConfigOptionId::PlanetSeaLevel, "planet.sea_level_m", "--planet-sea-level-m",
           "Sea Level", "Planet", "Planet terrain waterline elevation in meters.",
           ConfigOptionType::Float),
    option(RunConfigOptionId::PlanetBathymetryDepthScale, "planet.bathymetry_depth_scale_m",
           "--planet-bathymetry-depth-scale-m", "Bathymetry Depth", "Planet",
           "Depth in meters that maps to full bathymetry debug intensity.", ConfigOptionType::Float,
           min_range(0.000001)),
    option(RunConfigOptionId::PlanetShorelineWidth, "planet.shoreline_width_m",
           "--planet-shoreline-width-m", "Shoreline Width", "Planet",
           "Height band around sea level used by shoreline debug masks.", ConfigOptionType::Float,
           min_range(0.000001)),
    option(RunConfigOptionId::PlanetAtmosphereHazeStrength, "planet.atmosphere_haze_strength",
           "--planet-atmosphere-haze-strength", "Surface Haze", "Planet",
           "Strength of distance haze used by the planet surface atmosphere.",
           ConfigOptionType::Float, bounded_range(0.0, 1.0)),
    option(RunConfigOptionId::PlanetAtmosphereHazeStart, "planet.atmosphere_haze_start",
           "--planet-atmosphere-haze-start", "Haze Start", "Planet",
           "Fraction of horizon distance where analytic planet surface haze starts.",
           ConfigOptionType::Float, bounded_range(0.0, 1.0)),
    option(RunConfigOptionId::PlanetAtmosphereHazeEnd, "planet.atmosphere_haze_end",
           "--planet-atmosphere-haze-end", "Haze End", "Planet",
           "Fraction of horizon distance where analytic planet surface haze reaches full strength.",
           ConfigOptionType::Float, bounded_range(0.0, 1.5)),
    option(RunConfigOptionId::PlanetAtmosphereAerialStrength, "planet.atmosphere_aerial_strength",
           "--planet-atmosphere-aerial-strength", "Aerial Strength", "Planet",
           "Blend strength for physical planet surface aerial perspective.",
           ConfigOptionType::Float, bounded_range(0.0, 1.0)),
    option(RunConfigOptionId::PlanetDayOfYear, "planet.day_of_year", "--planet-day-of-year",
           "Day Of Year", "Planet", "Initial planet solar-clock day of year.",
           ConfigOptionType::Float, bounded_range(1.0, 365.2422)),
    option(RunConfigOptionId::PlanetTimeHours, "planet.time_hours", "--planet-time-hours",
           "Time Hours", "Planet", "Initial planet mean solar-clock time in hours.",
           ConfigOptionType::Float, bounded_range(0.0, 24.0)),
    option(RunConfigOptionId::PlanetTimeSpeed, "planet.time_speed_hours_per_second",
           "--planet-time-speed-hours-per-second", "Time Speed", "Planet",
           "Simulated planet hours advanced per real second.", ConfigOptionType::Float),
    option(RunConfigOptionId::PlanetTimePaused, "planet.time_paused", "--planet-pause-time",
           "Pause Time", "Planet", "Start planet solar time paused.", ConfigOptionType::Bool),
    option(RunConfigOptionId::PlanetCameraMode, "planet.camera_mode", "--planet-camera-mode",
           "Camera Mode", "Planet", "Initial planet camera mode.", ConfigOptionType::Enum,
           no_range(), enum_choices(kPlanetCameraModes)),
    option(RunConfigOptionId::PlanetAtmosphereMode, "planet.atmosphere_mode",
           "--planet-atmosphere-mode", "Atmosphere Mode", "Planet", "Planet sky atmosphere mode.",
           ConfigOptionType::Enum, no_range(), enum_choices(kPlanetAtmosphereModes)),
    option(RunConfigOptionId::TerrainSeed, "terrain.seed", "--terrain-seed", "Seed", "Terrain",
           "Deterministic procedural terrain seed.", ConfigOptionType::UInt64),
    option(RunConfigOptionId::TerrainCellSize, "terrain.cell_size", "--terrain-cell-size",
           "Cell Size", "Terrain", "World-space terrain heightfield cell size.",
           ConfigOptionType::Float, min_range(0.0)),
    option(RunConfigOptionId::TerrainSeaLevel, "terrain.sea_level", "--terrain-sea-level",
           "Sea Level", "Terrain", "Terrain waterline elevation.", ConfigOptionType::Float),
    option(RunConfigOptionId::TerrainLandExtent, "terrain.land_extent", "--terrain-land-extent",
           "Land Extent", "Terrain", "Approximate normalized above-water land footprint.",
           ConfigOptionType::Float),
    option(RunConfigOptionId::TerrainCoastNoise, "terrain.coast_noise", "--terrain-coast-noise",
           "Coast Noise", "Terrain", "Coastline perturbation strength.", ConfigOptionType::Float,
           min_range(0.0)),
    option(RunConfigOptionId::TerrainRelief, "terrain.relief", "--terrain-relief", "Relief",
           "Terrain", "Overall terrain relief multiplier.", ConfigOptionType::Float,
           min_range(0.0)),
    option(RunConfigOptionId::TerrainRidges, "terrain.ridges", "--terrain-ridges", "Ridges",
           "Terrain", "Ridged terrain contribution.", ConfigOptionType::Float, min_range(0.0)),
    option(RunConfigOptionId::TerrainValleys, "terrain.valleys", "--terrain-valleys", "Valleys",
           "Terrain", "Valley terrain contribution.", ConfigOptionType::Float, min_range(0.0)),
    option(RunConfigOptionId::TerrainWaterSurface, "terrain.water_surface",
           "--terrain-water-surface", "Water Surface", "Terrain",
           "Enable the terrain water surface.", ConfigOptionType::Bool, no_range(), {},
           "--no-terrain-water-surface"),
    option(RunConfigOptionId::TerrainLabSlicePreset, "terrain_lab.slice_preset",
           "--terrain-lab-slice", "Slice Preset", "Terrain Lab",
           "Terrain Lab biome slice preset.", ConfigOptionType::Enum, no_range(),
           enum_choices(kTerrainLabSlicePresets)),
    option(RunConfigOptionId::TerrainLabCameraPreset, "terrain_lab.camera_preset",
           "--terrain-lab-camera-preset", "Camera Preset", "Terrain Lab",
           "Initial Terrain Lab review camera framing.", ConfigOptionType::Enum, no_range(),
           enum_choices(kTerrainLabCameraPresets)),
    option(RunConfigOptionId::TerrainLabNoiseSource, "terrain_lab.noise_source",
           "--terrain-lab-noise-source", "Noise Source", "Terrain Lab",
           "Terrain Lab CPU source-field noise backend.", ConfigOptionType::Enum, no_range(),
           enum_choices(kTerrainLabNoiseSources)),
    option(RunConfigOptionId::AtmospherePreset, "atmosphere.preset", "--atmosphere-preset",
           "Preset", "Atmosphere", "Atmosphere preset name.", ConfigOptionType::String),
    option(RunConfigOptionId::AtmosphereTimeOfDayMode, "atmosphere.time_of_day_mode",
           "--time-of-day-mode", "Time Mode", "Atmosphere",
           "Manual sun angles or solar-clock mode.", ConfigOptionType::Enum, no_range(),
           enum_choices(kTimeOfDayModes)),
    option(RunConfigOptionId::AtmosphereNightSkyMode, "atmosphere.night_sky_mode",
           "--night-sky-mode", "Night Sky Mode", "Atmosphere", "Night-sky visibility model.",
           ConfigOptionType::Enum, no_range(), enum_choices(kNightSkyModes)),
    option(RunConfigOptionId::AtmosphereMilkyWayLayer, "atmosphere.milky_way_layer",
           "--milky-way-layer", "Milky Way Layer", "Atmosphere",
           "Generated Milky Way atlas layer to inspect.", ConfigOptionType::Enum, no_range(),
           enum_choices(kMilkyWayLayers)),
    option(RunConfigOptionId::AtmosphereSunElevation, "atmosphere.sun_elevation_degrees",
           "--sun-elevation", "Sun Elevation", "Atmosphere", "Manual sun elevation in degrees.",
           ConfigOptionType::Float, bounded_range(-90.0, 90.0)),
    option(RunConfigOptionId::AtmosphereSunAzimuth, "atmosphere.sun_azimuth_degrees",
           "--sun-azimuth", "Sun Azimuth", "Atmosphere", "Manual sun azimuth in degrees.",
           ConfigOptionType::Float, bounded_range(-360.0, 360.0)),
    option(RunConfigOptionId::AtmosphereCameraAltitude, "atmosphere.camera_altitude_km",
           "--camera-altitude-km", "Camera Altitude", "Atmosphere",
           "Observer altitude above sea level.", ConfigOptionType::Float, min_range(0.0)),
    option(RunConfigOptionId::AtmosphereCameraYawOffset,
           "atmosphere.camera_yaw_offset_degrees", "--camera-yaw-offset-deg",
           "Camera Yaw Offset", "Atmosphere",
           "Additional yaw offset from the default atmosphere review direction.",
           ConfigOptionType::Float, bounded_range(-360.0, 360.0)),
    option(RunConfigOptionId::AtmosphereCameraPitchOffset,
           "atmosphere.camera_pitch_offset_degrees", "--camera-pitch-offset-deg",
           "Camera Pitch Offset", "Atmosphere",
           "Additional pitch offset from the default atmosphere review direction.",
           ConfigOptionType::Float, bounded_range(-89.0, 89.0)),
    option(RunConfigOptionId::AtmosphereRayleighScale, "atmosphere.rayleigh_scale",
           "--rayleigh-scale", "Rayleigh Scale", "Atmosphere",
           "Rayleigh molecular scattering density multiplier.", ConfigOptionType::Float,
           min_range(0.0)),
    option(RunConfigOptionId::AtmosphereMieScale, "atmosphere.mie_scale", "--mie-scale",
           "Mie Scale", "Atmosphere", "Mie aerosol density multiplier.", ConfigOptionType::Float,
           min_range(0.0)),
    option(RunConfigOptionId::AtmosphereOzoneScale, "atmosphere.ozone_scale", "--ozone-scale",
           "Ozone Scale", "Atmosphere", "Ozone absorption density multiplier.",
           ConfigOptionType::Float, min_range(0.0)),
    option(RunConfigOptionId::AtmosphereTimeHours, "atmosphere.time_hours", "--time-hours",
           "Time Hours", "Atmosphere", "Solar-clock time in hours.", ConfigOptionType::Float,
           bounded_range(0.0, 24.0)),
    option(RunConfigOptionId::AtmosphereDayOfYear, "atmosphere.day_of_year", "--day-of-year",
           "Day Of Year", "Atmosphere", "Solar-clock day of year.", ConfigOptionType::Float,
           bounded_range(1.0, 366.0)),
    option(RunConfigOptionId::AtmosphereLatitude, "atmosphere.latitude_degrees",
           "--latitude-degrees", "Latitude", "Atmosphere", "Solar-clock observer latitude.",
           ConfigOptionType::Float, bounded_range(-90.0, 90.0)),
    option(RunConfigOptionId::AtmosphereSunAzimuthOffset, "atmosphere.sun_azimuth_offset_degrees",
           "--sun-azimuth-offset", "Sun Azimuth Offset", "Atmosphere",
           "Offset applied to computed solar azimuth.", ConfigOptionType::Float,
           bounded_range(-360.0, 360.0)),
    option(RunConfigOptionId::AtmosphereTimeSpeed, "atmosphere.time_speed_hours_per_second",
           "--time-speed-hours-per-second", "Time Speed", "Atmosphere",
           "Simulated hours advanced per real second.", ConfigOptionType::Float, min_range(0.0)),
    option(RunConfigOptionId::AtmosphereTimePaused, "atmosphere.time_paused", "--pause-time",
           "Pause Time", "Atmosphere", "Start atmosphere time paused.", ConfigOptionType::Bool),
    option(RunConfigOptionId::AtmosphereAutoExposure, "atmosphere.auto_exposure", "--auto-exposure",
           "Auto Exposure", "Atmosphere", "Enable atmosphere-driven automatic exposure.",
           ConfigOptionType::Bool, no_range(), {}, "--no-auto-exposure"),
    option(RunConfigOptionId::AtmosphereExposureBias, "atmosphere.exposure_bias", "--exposure-bias",
           "Exposure Bias", "Atmosphere", "Exposure bias applied to automatic exposure.",
           ConfigOptionType::Float, bounded_range(-4.0, 4.0)),
    option(RunConfigOptionId::AtmosphereTwilightStrength, "atmosphere.twilight_strength",
           "--twilight-strength", "Twilight", "Atmosphere", "Twilight brightness multiplier.",
           ConfigOptionType::Float, bounded_range(0.0, 4.0)),
    option(RunConfigOptionId::AtmosphereTwilightHorizonWarmth, "atmosphere.twilight_horizon_warmth",
           "--twilight-horizon-warmth", "Horizon Warmth", "Atmosphere",
           "Warm color bias near the twilight horizon.", ConfigOptionType::Float,
           bounded_range(0.0, 2.0)),
    option(RunConfigOptionId::AtmosphereStarIntensity, "atmosphere.star_intensity",
           "--star-intensity", "Stars", "Atmosphere", "Procedural star brightness.",
           ConfigOptionType::Float, bounded_range(0.0, 4.0)),
    option(RunConfigOptionId::AtmosphereStarDensity, "atmosphere.star_density", "--star-density",
           "Star Density", "Atmosphere", "Procedural star density.", ConfigOptionType::Float,
           bounded_range(0.0, 1.0)),
    option(RunConfigOptionId::AtmosphereMilkyWayIntensity, "atmosphere.milky_way_intensity",
           "--milky-way-intensity", "Milky Way", "Atmosphere", "Generated Milky Way brightness.",
           ConfigOptionType::Float, bounded_range(0.0, 4.0)),
    option(RunConfigOptionId::AtmosphereMilkyWayContrast, "atmosphere.milky_way_contrast",
           "--milky-way-contrast", "Milky Way Contrast", "Atmosphere",
           "Generated Milky Way contrast.", ConfigOptionType::Float, bounded_range(0.0, 4.0)),
    option(RunConfigOptionId::AtmosphereLightPollution, "atmosphere.light_pollution",
           "--light-pollution", "Light Pollution", "Atmosphere",
           "Skyglow amount that suppresses night-sky features.", ConfigOptionType::Float,
           bounded_range(0.0, 1.0)),
    option(RunConfigOptionId::AtmosphereMilkyWayVariation, "atmosphere.milky_way_variation",
           "--milky-way-variation", "Milky Way Variation", "Atmosphere",
           "Procedural variation phase for Milky Way generation.", ConfigOptionType::Float,
           bounded_range(0.0, 16.0)),
    option(RunConfigOptionId::AtmosphereMoonIntensity, "atmosphere.moon_intensity",
           "--moon-intensity", "Moon", "Atmosphere", "Visible moon brightness.",
           ConfigOptionType::Float, bounded_range(0.0, 4.0)),
    option(RunConfigOptionId::AtmosphereMoonlightIntensity, "atmosphere.moonlight_intensity",
           "--moonlight-intensity", "Moonlight", "Atmosphere", "Moonlight contribution.",
           ConfigOptionType::Float, bounded_range(0.0, 4.0)),
    option(RunConfigOptionId::AtmosphereMoonPhaseOffset, "atmosphere.moon_phase_offset_days",
           "--moon-phase-offset-days", "Moon Phase Offset", "Atmosphere",
           "Offset in days through the lunar phase cycle.", ConfigOptionType::Float,
           bounded_range(0.0, 29.530588)),
    option(RunConfigOptionId::AtmosphereMoonSizeScale, "atmosphere.moon_size_scale",
           "--moon-size-scale", "Moon Size", "Atmosphere", "Visual moon scale.",
           ConfigOptionType::Float, bounded_range(0.000001, 8.0)),
    option(RunConfigOptionId::AtmosphereMoon, "atmosphere.moon", "--moon", "Moon", "Atmosphere",
           "Enable the visible moon and moonlight.", ConfigOptionType::Bool, no_range(), {},
           "--no-moon"),
    option(RunConfigOptionId::AtmosphereReferenceGeometry, "atmosphere.reference_geometry",
           "--reference-geometry", "Reference Geometry", "Atmosphere",
           "Enable the standalone atmosphere ground reference grid.", ConfigOptionType::Bool,
           no_range(), {}, "--no-reference-geometry"),
    option(RunConfigOptionId::CloudEnabled, "clouds.enabled", "--clouds", "Clouds", "Clouds",
           "Enable cloud layer rendering in shared atmosphere-backed projects.",
           ConfigOptionType::Bool, no_range(), {}, "--no-clouds"),
    option(RunConfigOptionId::CloudDebugView, "clouds.debug_view", "--cloud-debug-view",
           "Debug View", "Clouds", "Cloud layer debug or diagnostic view.",
           ConfigOptionType::String),
    option(RunConfigOptionId::CloudCameraMode, "clouds.camera_mode", "--cloud-camera-mode",
           "Camera Mode", "Clouds", "Initial cloud camera mode.", ConfigOptionType::Enum,
           no_range(), enum_choices(kCloudCameraModes)),
    option(RunConfigOptionId::CloudQuality, "clouds.quality", "--cloud-quality", "Quality",
           "Clouds", "Cloud render quality preset.", ConfigOptionType::Enum, no_range(),
           enum_choices(kCloudQualities)),
    option(RunConfigOptionId::CloudWeatherPreset, "clouds.weather_preset",
           "--cloud-weather-preset", "Weather Preset", "Clouds",
           "Cloud coverage, density, scale, and wind preset.", ConfigOptionType::Enum, no_range(),
           enum_choices(kCloudWeatherPresets)),
    option(RunConfigOptionId::CloudCacheFrames, "clouds.cache_frames",
           "--cloud-cache-frames", "Cache Frames", "Clouds",
           "Frames used to refresh one complete cached cloud sky texture.", ConfigOptionType::Enum,
           no_range(), enum_choices(kCloudCacheFrames)),
    option(RunConfigOptionId::CloudCacheTextureSize, "clouds.cache_texture_size",
           "--cloud-cache-texture-size", "Cache Texture Size", "Clouds",
           "Square cached cloud sky texture size in pixels.", ConfigOptionType::UInt32,
           min_range(1.0)),
    option(RunConfigOptionId::CloudRenderPath, "clouds.render_path",
           "--cloud-render-path", "Render Path", "Clouds",
           "Cloud validation render path: cached, direct, diff, or alpha-diff.",
           ConfigOptionType::Enum, no_range(), enum_choices(kCloudRenderPaths)),
    option(RunConfigOptionId::CloudSamplingMode, "clouds.sampling_mode",
           "--cloud-sampling-mode", "Sampling Mode", "Clouds",
           "Cloud ray-start sampling mode: interleaved, bayer, blue-noise, or off.",
           ConfigOptionType::Enum, no_range(), enum_choices(kCloudSamplingModes)),
    option(RunConfigOptionId::CloudDensityModel, "clouds.density_model",
           "--cloud-density-model", "Density Model", "Clouds",
           "Cloud density and placement model: ref-density, procedural, or cloud-ref-compatible.",
           ConfigOptionType::Enum, no_range(), enum_choices(kCloudDensityModels)),
    option(RunConfigOptionId::CloudResolveMode, "clouds.resolve_mode",
           "--cloud-resolve-mode", "Resolve Mode", "Clouds",
           "Cloud final resolve mode: terrain-post or metadata-bilateral.",
           ConfigOptionType::Enum, no_range(), enum_choices(kCloudResolveModes)),
    option(RunConfigOptionId::CloudBackgroundMode, "clouds.background_mode",
           "--cloud-background-mode", "Background Mode", "Clouds",
           "Standalone cloud background mode: atmosphere or water-context.",
           ConfigOptionType::Enum, no_range(), enum_choices(kCloudBackgroundModes)),
    option(RunConfigOptionId::CloudDistanceMode, "clouds.distance_mode",
           "--cloud-distance-mode", "Distance Mode", "Clouds",
           "Cloud distance regime: auto, local, orbit-shell, or blend-debug.",
           ConfigOptionType::Enum, no_range(), enum_choices(kCloudDistanceModes)),
    option(RunConfigOptionId::CloudOrbitRepresentation, "clouds.orbit_representation",
           "--cloud-orbit-representation", "Orbit Representation", "Clouds",
           "Orbit cloud representation: volume or surface-shell.", ConfigOptionType::Enum,
           no_range(), enum_choices(kCloudOrbitRepresentations)),
    option(RunConfigOptionId::CloudPlanetRadius, "clouds.planet_radius_m",
           "--cloud-planet-radius-m", "Planet Radius", "Clouds",
           "Planet radius used by the cloud shell in meters.", ConfigOptionType::Float,
           min_range(1.0)),
    option(RunConfigOptionId::CloudCameraAltitude, "clouds.camera_altitude_m",
           "--cloud-camera-altitude-m", "Camera Altitude", "Clouds",
           "Cloud camera altitude above the planet surface in meters.", ConfigOptionType::Float,
           min_range(0.0)),
    option(RunConfigOptionId::CloudBottomAltitude, "clouds.bottom_altitude_m",
           "--cloud-bottom-altitude-m", "Cloud Bottom", "Clouds",
           "Cloud layer bottom altitude above the planet surface in meters.",
           ConfigOptionType::Float, min_range(0.0)),
    option(RunConfigOptionId::CloudTopAltitude, "clouds.top_altitude_m",
           "--cloud-top-altitude-m", "Cloud Top", "Clouds",
           "Cloud layer top altitude above the planet surface in meters.",
           ConfigOptionType::Float, min_range(0.0)),
    option(RunConfigOptionId::CloudCoverage, "clouds.coverage", "--cloud-coverage", "Coverage",
           "Clouds", "Base cloud coverage fraction.", ConfigOptionType::Float,
           bounded_range(0.0, 1.0)),
    option(RunConfigOptionId::CloudDensity, "clouds.density", "--cloud-density", "Density",
           "Clouds", "Cloud extinction density multiplier.", ConfigOptionType::Float,
           min_range(0.0)),
    option(RunConfigOptionId::CloudWeatherScale, "clouds.weather_scale_km",
           "--cloud-weather-scale-km", "Weather Scale", "Clouds",
           "Approximate broad cloud weather feature size in kilometers.",
           ConfigOptionType::Float, min_range(0.001)),
    option(RunConfigOptionId::CloudShapeDomain, "clouds.shape_domain_km",
           "--cloud-shape-domain-km", "Shape Domain", "Clouds",
           "Approximate local cloud density texture domain size in kilometers.",
           ConfigOptionType::Float, min_range(0.001)),
    option(RunConfigOptionId::CloudFootprintFilterStrength, "clouds.footprint_filter_strength",
           "--cloud-footprint-filter-strength", "Footprint Filter", "Clouds",
           "Strength of deterministic footprint filtering for far and grazing cloud detail.",
           ConfigOptionType::Float, bounded_range(0.0, 2.0)),
    option(RunConfigOptionId::CloudEdgeSoftness, "clouds.edge_softness",
           "--cloud-edge-softness", "Edge Softness", "Clouds",
           "Strength of footprint-aware density edge softening.", ConfigOptionType::Float,
           bounded_range(0.0, 2.0)),
    option(RunConfigOptionId::CloudEdgeDetailFade, "clouds.edge_detail_fade",
           "--cloud-edge-detail-fade", "Edge Detail Fade", "Clouds",
           "Amount of unresolved high-frequency detail erosion faded at cloud edges.",
           ConfigOptionType::Float, bounded_range(0.0, 2.0)),
    option(RunConfigOptionId::CloudEdgeResolveStrength, "clouds.edge_resolve_strength",
           "--cloud-edge-resolve-strength", "Edge Resolve", "Clouds",
           "Strength of edge-aware cloud resolve in the final composite.",
           ConfigOptionType::Float, bounded_range(0.0, 1.0)),
    option(RunConfigOptionId::CloudVerticalShearFraction, "clouds.vertical_shear_fraction",
           "--cloud-vertical-shear-fraction", "Vertical Shear", "Clouds",
           "Fraction of weather feature size used for altitude-dependent cloud shear.",
           ConfigOptionType::Float, bounded_range(0.0, 0.5)),
    option(RunConfigOptionId::CloudWindSpeed, "clouds.wind_speed_mps", "--cloud-wind-speed-mps",
           "Wind Speed", "Clouds", "Cloud weather-map wind speed in meters per second.",
           ConfigOptionType::Float, min_range(0.0)),
    option(RunConfigOptionId::CloudShadowStrength, "clouds.shadow_strength",
           "--cloud-shadow-strength", "Shadow Strength", "Clouds",
           "Strength of prototype cloud shadows on the standalone cloud ground proxy.",
           ConfigOptionType::Float, bounded_range(0.0, 2.0)),
    option(RunConfigOptionId::CloudHorizonStrength, "clouds.horizon_strength",
           "--cloud-horizon-strength", "Horizon Strength", "Clouds",
           "Strength of the dedicated far-horizon cloud layer.", ConfigOptionType::Float,
           bounded_range(0.0, 2.0)),
    option(RunConfigOptionId::CloudWeatherFronts, "clouds.weather_fronts",
           "--cloud-weather-fronts", "Weather Fronts", "Clouds",
           "Feature-isolation weight for frontal cloud structures.", ConfigOptionType::Float,
           bounded_range(0.0, 1.0)),
    option(RunConfigOptionId::CloudWeatherCells, "clouds.weather_cells", "--cloud-weather-cells",
           "Weather Cells", "Clouds", "Feature-isolation weight for cellular cloud structures.",
           ConfigOptionType::Float, bounded_range(0.0, 1.0)),
    option(RunConfigOptionId::CloudWeatherStreaks, "clouds.weather_streaks",
           "--cloud-weather-streaks", "Weather Streaks", "Clouds",
           "Feature-isolation weight for wind-aligned streak structures.",
           ConfigOptionType::Float, bounded_range(0.0, 1.0)),
    option(RunConfigOptionId::CloudWeatherSoftness, "clouds.weather_softness",
           "--cloud-weather-softness", "Weather Softness", "Clouds",
           "Softness of broad weather bias transitions.", ConfigOptionType::Float,
           bounded_range(0.02, 0.6)),
    option(RunConfigOptionId::CloudWeatherInfluence, "clouds.weather_influence",
           "--cloud-weather-influence", "Weather Influence", "Clouds",
           "How strongly the broad weather map biases local cloud density.",
           ConfigOptionType::Float, bounded_range(0.0, 1.0)),
    option(RunConfigOptionId::CloudDetailErosion, "clouds.detail_erosion",
           "--cloud-detail-erosion", "Detail Erosion", "Clouds",
           "Feature-isolation weight for high-frequency cloud erosion.", ConfigOptionType::Float,
           bounded_range(0.0, 1.0)),
    option(RunConfigOptionId::CloudAmbientStrength, "clouds.ambient_strength",
           "--cloud-ambient-strength", "Ambient Strength", "Clouds",
           "Cloud ambient-light multiplier used by the production cloud renderer.",
           ConfigOptionType::Float, bounded_range(0.0, 3.0)),
    option(RunConfigOptionId::CloudDirectStrength, "clouds.direct_strength",
           "--cloud-direct-strength", "Direct Strength", "Clouds",
           "Cloud direct sun-light multiplier used by the production cloud renderer.",
           ConfigOptionType::Float, bounded_range(0.0, 3.0)),
    option(RunConfigOptionId::CloudPhaseStrength, "clouds.phase_strength",
           "--cloud-phase-strength", "Phase Strength", "Clouds",
           "Cloud forward/rim phase-light multiplier used by the production cloud renderer.",
           ConfigOptionType::Float, bounded_range(0.0, 3.0)),
    option(RunConfigOptionId::CloudFinalContrast, "clouds.final_contrast",
           "--cloud-final-contrast", "Final Contrast", "Clouds",
           "Final cloud composite contrast multiplier.", ConfigOptionType::Float,
           bounded_range(0.0, 3.0)),
    option(RunConfigOptionId::CloudFinalSaturation, "clouds.final_saturation",
           "--cloud-final-saturation", "Final Saturation", "Clouds",
           "Final cloud composite saturation multiplier.", ConfigOptionType::Float,
           bounded_range(0.0, 3.0)),
    option(RunConfigOptionId::CloudResolveStrength, "clouds.resolve_strength",
           "--cloud-resolve-strength", "Resolve Strength", "Clouds",
           "Amount of alpha-aware cloud product resolve in final view.", ConfigOptionType::Float,
           bounded_range(0.0, 1.0)),
    option(RunConfigOptionId::CloudResolveRadius, "clouds.resolve_radius_px",
           "--cloud-resolve-radius-px", "Resolve Radius", "Clouds",
           "Cloud final resolve blur radius in pixels.", ConfigOptionType::Float,
           bounded_range(0.0, 8.0)),
    option(RunConfigOptionId::CloudHorizonGlowStrength, "clouds.horizon_glow_strength",
           "--cloud-horizon-glow-strength", "Horizon Glow", "Clouds",
           "Final composite horizon fill/glow multiplier.", ConfigOptionType::Float,
           bounded_range(0.0, 3.0)),
    option(RunConfigOptionId::CloudSunGlareStrength, "clouds.sun_glare_strength",
           "--cloud-sun-glare-strength", "Sun Glare", "Clouds",
           "Final composite sun halo and glare multiplier.", ConfigOptionType::Float,
           bounded_range(0.0, 3.0)),
    option(RunConfigOptionId::CloudJitterStrength, "clouds.jitter_strength",
           "--cloud-jitter-strength", "Jitter Strength", "Clouds",
           "Cloud ray-start jitter amount applied by the selected sampling mode.",
           ConfigOptionType::Float, bounded_range(0.0, 1.0)),
    option(RunConfigOptionId::CloudOrbitTransitionStart, "clouds.orbit_transition_start_m",
           "--cloud-orbit-transition-start-m", "Orbit Transition Start", "Clouds",
           "Camera altitude where the broad orbit cloud shell starts blending in.",
           ConfigOptionType::Float, min_range(0.0)),
    option(RunConfigOptionId::CloudOrbitTransitionEnd, "clouds.orbit_transition_end_m",
           "--cloud-orbit-transition-end-m", "Orbit Transition End", "Clouds",
           "Camera altitude where the broad orbit cloud shell fully replaces local clouds.",
           ConfigOptionType::Float, min_range(0.0)),
    option(RunConfigOptionId::CloudFarShellStart, "clouds.far_shell_start_m",
           "--cloud-far-shell-start-m", "Far Shell Start", "Clouds",
           "View-ray distance where high-altitude rays start preferring the orbit shell.",
           ConfigOptionType::Float, min_range(0.0)),
    option(RunConfigOptionId::CloudFarShellEnd, "clouds.far_shell_end_m",
           "--cloud-far-shell-end-m", "Far Shell End", "Clouds",
           "View-ray distance where high-altitude rays fully prefer the orbit shell.",
           ConfigOptionType::Float, min_range(0.0)),
    option(RunConfigOptionId::CloudFarShellStrength, "clouds.far_shell_strength",
           "--cloud-far-shell-strength", "Far Shell Strength", "Clouds",
           "Strength of the far cloud shell contribution behind high-view local volume.",
           ConfigOptionType::Float, bounded_range(0.0, 1.5)),
    option(RunConfigOptionId::CloudOrbitDetailStrength, "clouds.orbit_detail_strength",
           "--cloud-orbit-detail-strength", "Orbit Detail", "Clouds",
           "Amount of high-frequency detail retained by the broad orbit shell.",
           ConfigOptionType::Float, bounded_range(0.0, 1.0)),
    option(RunConfigOptionId::CloudOrbitDensityScale, "clouds.orbit_density_scale",
           "--cloud-orbit-density-scale", "Orbit Density", "Clouds",
           "Density multiplier for the broad orbit cloud shell.", ConfigOptionType::Float,
           bounded_range(0.0, 2.0)),
    option(RunConfigOptionId::CloudOrbitFill, "clouds.orbit_fill",
           "--cloud-orbit-fill", "Orbit Fill", "Clouds",
           "Fill bias for broad orbit cloud weather coverage.", ConfigOptionType::Float,
           bounded_range(0.0, 2.0)),
    option(RunConfigOptionId::CloudOrbitMotionStrength, "clouds.orbit_motion_strength",
           "--cloud-orbit-motion-strength", "Orbit Motion", "Clouds",
           "Motion multiplier for procedural orbit weather advection.", ConfigOptionType::Float,
           bounded_range(0.0, 4.0)),
    option(RunConfigOptionId::CloudOrbitShellExtinction, "clouds.orbit_shell_extinction",
           "--cloud-orbit-shell-extinction", "Orbit Extinction", "Clouds",
           "Extinction multiplier for cloud-top shell optical depth.", ConfigOptionType::Float,
           bounded_range(0.0, 8.0)),
    option(RunConfigOptionId::CloudTemporal, "clouds.temporal", "--cloud-temporal",
           "Temporal", "Clouds", "Enable temporal reconstruction for the cloud product.",
           ConfigOptionType::Bool, no_range(), {}, "--no-cloud-temporal"),
    option(RunConfigOptionId::CloudLocalVolume, "clouds.local_volume", "--cloud-local-volume",
           "Local Volume", "Clouds", "Enable near and overhead volumetric cloud marching.",
           ConfigOptionType::Bool, no_range(), {}, "--no-cloud-local-volume"),
    option(RunConfigOptionId::CloudHorizonLayer, "clouds.horizon_layer", "--cloud-horizon-layer",
           "Horizon Layer", "Clouds", "Enable the dedicated far-horizon cloud layer.",
           ConfigOptionType::Bool, no_range(), {}, "--no-cloud-horizon-layer"),
    option(RunConfigOptionId::SmokeInjectors, "smoke.injectors", "--smoke-injectors", "Injectors",
           "Smoke 2D", "Number of built-in smoke injectors.", ConfigOptionType::UInt32,
           min_range(1.0)),
    option(RunConfigOptionId::SmokePressureIterations, "smoke.pressure_iterations",
           "--smoke-pressure-iterations", "Pressure Iterations", "Smoke 2D",
           "Pressure projection iteration count.", ConfigOptionType::UInt32, min_range(1.0)),
    option(RunConfigOptionId::SmokePressureSolver, "smoke.pressure_solver",
           "--smoke-pressure-solver", "Pressure Solver", "Smoke 2D",
           "Pressure solver implementation.", ConfigOptionType::Enum, no_range(),
           enum_choices(kSmokePressureSolvers)),
    option(RunConfigOptionId::SmokeDyeDecay, "smoke.dye_decay", "--smoke-dye-decay", "Dye Decay",
           "Smoke 2D", "Per-frame dye retention.", ConfigOptionType::Float,
           bounded_range(0.0, 1.0)),
    option(RunConfigOptionId::SmokeVelocityDecay, "smoke.velocity_decay", "--smoke-velocity-decay",
           "Velocity Decay", "Smoke 2D", "Per-frame velocity retention.", ConfigOptionType::Float,
           bounded_range(0.0, 1.0)),
    option(RunConfigOptionId::SmokeInjectorRadius, "smoke.injector_radius",
           "--smoke-injector-radius", "Injector Radius", "Smoke 2D",
           "Smoke injector radius in normalized simulation space.", ConfigOptionType::Float,
           min_range(0.000001)),
    option(RunConfigOptionId::SmokeInjectorForce, "smoke.injector_force", "--smoke-injector-force",
           "Injector Force", "Smoke 2D", "Force applied by smoke injectors.",
           ConfigOptionType::Float, min_range(0.0)),
    option(RunConfigOptionId::SmokeInjectorPropulsion, "smoke.injector_propulsion",
           "--smoke-injector-propulsion", "Injector Propulsion", "Smoke 2D",
           "Propulsion applied opposite injector motion.", ConfigOptionType::Float, min_range(0.0)),
    option(RunConfigOptionId::SmokeInjectorOrbitRadius, "smoke.injector_orbit_radius",
           "--smoke-injector-orbit-radius", "Orbit Radius", "Smoke 2D",
           "Base injector orbit radius.", ConfigOptionType::Float, min_range(0.000001)),
    option(RunConfigOptionId::SmokeInjectorOrbitRadiusSpread, "smoke.injector_orbit_radius_spread",
           "--smoke-injector-orbit-radius-spread", "Orbit Radius Spread", "Smoke 2D",
           "Injector orbit radius variation.", ConfigOptionType::Float, min_range(0.0)),
    option(RunConfigOptionId::SmokeInjectorOrbitAngularSpeed, "smoke.injector_orbit_angular_speed",
           "--smoke-injector-orbit-angular-speed", "Orbit Speed", "Smoke 2D",
           "Base injector angular speed.", ConfigOptionType::Float),
    option(RunConfigOptionId::SmokeInjectorOrbitAngularSpeedSpread,
           "smoke.injector_orbit_angular_speed_spread",
           "--smoke-injector-orbit-angular-speed-spread", "Orbit Speed Spread", "Smoke 2D",
           "Injector angular speed variation.", ConfigOptionType::Float, min_range(0.0)),
    option(RunConfigOptionId::SmokeInjectorOrbitPhaseSpread, "smoke.injector_orbit_phase_spread",
           "--smoke-injector-orbit-phase-spread", "Orbit Phase Spread", "Smoke 2D",
           "Injector orbit phase variation.", ConfigOptionType::Float, min_range(0.0)),
    option(RunConfigOptionId::SmokeVorticity, "smoke.vorticity", "--smoke-vorticity", "Vorticity",
           "Smoke 2D", "Vorticity confinement strength.", ConfigOptionType::Float, min_range(0.0)),
    option(RunConfigOptionId::PyroShadowGridWidth, "pyro.shadow_grid.width", "--shadow-grid-width",
           "Shadow Width", "Pyro 3D", "Shadow-volume grid width.", ConfigOptionType::UInt32,
           min_range(1.0)),
    option(RunConfigOptionId::PyroShadowGridHeight, "pyro.shadow_grid.height",
           "--shadow-grid-height", "Shadow Height", "Pyro 3D", "Shadow-volume grid height.",
           ConfigOptionType::UInt32, min_range(1.0)),
    option(RunConfigOptionId::PyroShadowGridDepth, "pyro.shadow_grid.depth", "--shadow-grid-depth",
           "Shadow Depth", "Pyro 3D", "Shadow-volume grid depth.", ConfigOptionType::UInt32,
           min_range(1.0)),
    option(RunConfigOptionId::PyroShadowSteps, "pyro.shadow_steps", "--shadow-steps",
           "Shadow Steps", "Pyro 3D", "Raymarch steps for volumetric shadowing.",
           ConfigOptionType::UInt32, min_range(1.0)),
    option(RunConfigOptionId::PyroShadowUpdateInterval, "pyro.shadow_update_interval",
           "--shadow-update-interval", "Shadow Update Interval", "Pyro 3D",
           "Frame interval for updating the shadow volume.", ConfigOptionType::UInt32,
           min_range(1.0)),
    option(RunConfigOptionId::PyroSources, "pyro.sources", "--pyro-sources", "Sources", "Pyro 3D",
           "Number of pyro source emitters.", ConfigOptionType::UInt32, min_range(1.0)),
    option(RunConfigOptionId::PyroSourceHeight, "pyro.source_height", "--pyro-source-height",
           "Source Height", "Pyro 3D", "Source height in normalized volume coordinates.",
           ConfigOptionType::Float, bounded_range(0.0, 1.0)),
    option(RunConfigOptionId::PyroSourceRadius, "pyro.source_radius", "--pyro-source-radius",
           "Source Radius", "Pyro 3D", "Source radius in normalized volume coordinates.",
           ConfigOptionType::Float, min_range(0.000001)),
    option(RunConfigOptionId::PyroSourceForce, "pyro.source_force", "--pyro-source-force",
           "Source Force", "Pyro 3D", "Velocity force injected by pyro sources.",
           ConfigOptionType::Float, min_range(0.0)),
    option(RunConfigOptionId::PyroSoot, "pyro.soot", "--pyro-soot", "Soot", "Pyro 3D",
           "Soot or smoke amount injected by sources.", ConfigOptionType::Float, min_range(0.0)),
    option(RunConfigOptionId::PyroTemperature, "pyro.temperature", "--pyro-temperature",
           "Temperature", "Pyro 3D", "Temperature injected by sources.", ConfigOptionType::Float,
           min_range(0.0)),
    option(RunConfigOptionId::PyroFuel, "pyro.fuel", "--pyro-fuel", "Fuel", "Pyro 3D",
           "Fuel injected by sources.", ConfigOptionType::Float, min_range(0.0)),
    option(RunConfigOptionId::PyroBuoyancy, "pyro.buoyancy", "--pyro-buoyancy", "Buoyancy",
           "Pyro 3D", "Thermal buoyancy strength.", ConfigOptionType::Float, min_range(0.0)),
    option(RunConfigOptionId::PyroIgnitionTemperature, "pyro.ignition_temperature",
           "--pyro-ignition-temperature", "Ignition", "Pyro 3D",
           "Temperature threshold for combustion.", ConfigOptionType::Float, min_range(0.0)),
    option(RunConfigOptionId::PyroBurnRate, "pyro.burn_rate", "--pyro-burn-rate", "Burn Rate",
           "Pyro 3D", "Fuel burn rate.", ConfigOptionType::Float, min_range(0.0)),
    option(RunConfigOptionId::PyroHeatOutput, "pyro.heat_output", "--pyro-heat-output",
           "Heat Output", "Pyro 3D", "Heat produced by combustion.", ConfigOptionType::Float,
           min_range(0.0)),
    option(RunConfigOptionId::PyroSootYield, "pyro.soot_yield", "--pyro-soot-yield", "Soot Yield",
           "Pyro 3D", "Soot produced by combustion.", ConfigOptionType::Float, min_range(0.0)),
    option(RunConfigOptionId::PyroExpansion, "pyro.expansion", "--pyro-expansion", "Expansion",
           "Pyro 3D", "Combustion expansion force.", ConfigOptionType::Float, min_range(0.0)),
    option(RunConfigOptionId::PyroFlameCooling, "pyro.flame_cooling", "--pyro-flame-cooling",
           "Flame Cooling", "Pyro 3D", "Cooling rate for visible flame.", ConfigOptionType::Float,
           min_range(0.0)),
    option(RunConfigOptionId::PyroShredding, "pyro.shredding", "--pyro-shredding", "Shredding",
           "Pyro 3D", "Small-scale flame breakup strength.", ConfigOptionType::Float,
           min_range(0.0)),
    option(RunConfigOptionId::PyroTurbulence, "pyro.turbulence", "--pyro-turbulence", "Turbulence",
           "Pyro 3D", "Source turbulence amount.", ConfigOptionType::Float, min_range(0.0)),
    option(RunConfigOptionId::PyroObstacleHeight, "pyro.obstacle_height", "--pyro-obstacle-height",
           "Obstacle Height", "Pyro 3D", "Ball obstacle center height.", ConfigOptionType::Float,
           bounded_range(0.0, 1.0)),
    option(RunConfigOptionId::PyroObstacleRadius, "pyro.obstacle_radius", "--pyro-obstacle-radius",
           "Obstacle Radius", "Pyro 3D", "Ball obstacle radius.", ConfigOptionType::Float,
           bounded_range(0.0, 0.5)),
    option(RunConfigOptionId::PyroExplosionInterval, "pyro.explosion_interval_seconds",
           "--explosion-interval", "Explosion Interval", "Pyro 3D",
           "Seconds between explosion impulses.", ConfigOptionType::Float, min_range(0.000001)),
    option(RunConfigOptionId::PyroExplosionDuration, "pyro.explosion_duration_seconds",
           "--explosion-duration", "Explosion Duration", "Pyro 3D",
           "Seconds spent in the explosion impulse.", ConfigOptionType::Float, min_range(0.000001)),
    option(RunConfigOptionId::PyroExplosionBoost, "pyro.explosion_boost", "--explosion-boost",
           "Explosion Boost", "Pyro 3D", "Impulse multiplier for explosion mode.",
           ConfigOptionType::Float, min_range(0.0)),
    option(RunConfigOptionId::Water2DTransfer, "water2d.transfer", "--water2d-transfer", "Transfer",
           "Water 2D", "Particle-grid transfer mode.", ConfigOptionType::Enum, no_range(),
           enum_choices(kWaterTransferModes)),
    option(RunConfigOptionId::Water2DTransferLimit, "water2d.transfer_limit",
           "--water2d-transfer-limit", "Transfer Limit", "Water 2D",
           "Particle samples consumed per grid cell.", ConfigOptionType::UInt32, min_range(1.0)),
    option(RunConfigOptionId::Water2DHose, "water2d.hose", "--water2d-hose", "Hose", "Water 2D",
           "Enable hose injection.", ConfigOptionType::Bool, no_range(), {}, "--no-water2d-hose"),
    option(RunConfigOptionId::Water2DDrain, "water2d.drain", "--water2d-drain", "Drain", "Water 2D",
           "Enable draining.", ConfigOptionType::Bool, no_range(), {}, "--no-water2d-drain"),
    option(RunConfigOptionId::Water2DWave, "water2d.wave", "--water2d-wave", "Wave", "Water 2D",
           "Enable wave forcing.", ConfigOptionType::Bool, no_range(), {}, "--no-water2d-wave"),
    option(RunConfigOptionId::Water3DTransfer, "water3d.transfer", "--water3d-transfer", "Transfer",
           "Water 3D", "Particle-grid transfer mode.", ConfigOptionType::Enum, no_range(),
           enum_choices(kWaterTransferModes)),
    option(RunConfigOptionId::Water3DTransferLimit, "water3d.transfer_limit",
           "--water3d-transfer-limit", "Transfer Limit", "Water 3D",
           "Particle samples consumed per grid cell.", ConfigOptionType::UInt32, min_range(1.0)),
    option(RunConfigOptionId::Water3DP2GMode, "water3d.p2g_mode", "--water3d-p2g-mode", "P2G Mode",
           "Water 3D", "Particle-to-grid implementation mode.", ConfigOptionType::Enum, no_range(),
           enum_choices(kWater3DP2GModes)),
    option(RunConfigOptionId::Water3DHose, "water3d.hose", "--water3d-hose", "Hose", "Water 3D",
           "Enable hose injection.", ConfigOptionType::Bool, no_range(), {}, "--no-water3d-hose"),
    option(RunConfigOptionId::Water3DDrain, "water3d.drain", "--water3d-drain", "Drain", "Water 3D",
           "Enable draining.", ConfigOptionType::Bool, no_range(), {}, "--no-water3d-drain"),
    option(RunConfigOptionId::Water3DRain, "water3d.rain", "--water3d-rain", "Rain", "Water 3D",
           "Enable rain injection.", ConfigOptionType::Bool, no_range(), {}, "--no-water3d-rain"),
    option(RunConfigOptionId::Water3DWave, "water3d.wave", "--water3d-wave", "Wave", "Water 3D",
           "Enable wave forcing.", ConfigOptionType::Bool, no_range(), {}, "--no-water3d-wave"),
    option(RunConfigOptionId::Water3DWhitewater, "water3d.whitewater", "--water3d-whitewater",
           "Whitewater", "Water 3D", "Enable whitewater particles.", ConfigOptionType::Bool,
           no_range(), {}, "--no-water3d-whitewater"),
};

template <typename T>
T parse_number(std::string_view value, const ConfigOptionDescriptor& option, const char* kind) {
    T parsed{};
    const char* begin = value.data();
    const char* end = value.data() + value.size();
    const auto result = std::from_chars(begin, end, parsed);
    if (result.ec != std::errc{} || result.ptr != end) {
        throw std::runtime_error("invalid " + std::string(kind) + " for " +
                                 std::string(option.path));
    }
    return parsed;
}

float parse_config_float(std::string_view value, const ConfigOptionDescriptor& option) {
    const float parsed = parse_number<float>(value, option, "float");
    if (!std::isfinite(parsed)) {
        throw std::runtime_error("invalid float for " + std::string(option.path));
    }
    return parsed;
}

bool parse_config_bool(std::string_view value, const ConfigOptionDescriptor& option) {
    if (value == "true" || value == "1" || value == "yes" || value == "on") {
        return true;
    }
    if (value == "false" || value == "0" || value == "no" || value == "off") {
        return false;
    }
    throw std::runtime_error("invalid bool for " + std::string(option.path));
}

void validate_range(double value, const ConfigOptionDescriptor& option) {
    if (option.range.has_min && value < option.range.min) {
        throw std::runtime_error(std::string(option.path) + " is below its minimum");
    }
    if (option.range.has_max && value > option.range.max) {
        throw std::runtime_error(std::string(option.path) + " is above its maximum");
    }
}

int parse_ocean_cascade(std::string_view value, const ConfigOptionDescriptor& option) {
    if (value == "all") {
        return -1;
    }
    if (value.size() == 1 && value[0] >= '0' && value[0] <= '4') {
        return static_cast<int>(value[0] - '0');
    }
    throw std::runtime_error("invalid ocean cascade for " + std::string(option.path));
}

std::filesystem::path profile_output_prefix(std::string_view value,
                                            const ConfigOptionDescriptor& option) {
    std::filesystem::path prefix{std::string(value)};
    if (prefix.empty()) {
        throw std::runtime_error(std::string(option.path) + " must not be empty");
    }
    if (!prefix.has_parent_path() && !prefix.is_absolute()) {
        prefix = std::filesystem::path("outputs") / "profiles" / prefix;
    }
    return prefix;
}

std::string json_path_string(std::string_view prefix, std::string_view key) {
    if (prefix.empty()) {
        return std::string(key);
    }
    return std::string(prefix) + "." + std::string(key);
}

bool json_matches_type(const nlohmann::json& value, const ConfigOptionDescriptor& option) {
    if (value.is_null()) {
        return true;
    }
    switch (option.type) {
    case ConfigOptionType::Bool:
        return value.is_boolean();
    case ConfigOptionType::Int:
        return value.is_number_integer();
    case ConfigOptionType::UInt32:
    case ConfigOptionType::UInt64:
        return value.is_number_integer() &&
               value.get<std::int64_t>() >= static_cast<std::int64_t>(0);
    case ConfigOptionType::Float:
        return value.is_number();
    case ConfigOptionType::String:
    case ConfigOptionType::Path:
    case ConfigOptionType::Enum:
        return value.is_string();
    }
    return false;
}

std::string json_value_to_set_string(const nlohmann::json& value) {
    if (value.is_boolean()) {
        return value.get<bool>() ? "true" : "false";
    }
    if (value.is_string()) {
        return value.get<std::string>();
    }
    return value.dump();
}

void validate_json_options(const nlohmann::json& object, std::string_view prefix,
                           bool& output_path_set) {
    if (!object.is_object()) {
        throw std::runtime_error("config file root must be a JSON object");
    }

    for (const auto& [key, value] : object.items()) {
        const std::string path = json_path_string(prefix, key);
        if (value.is_object()) {
            validate_json_options(value, path, output_path_set);
            continue;
        }

        const ConfigOptionDescriptor* option = cubey::find_run_config_option(path);
        if (option == nullptr) {
            throw std::runtime_error("unknown config option: " + path);
        }
        if (!json_matches_type(value, *option)) {
            throw std::runtime_error("wrong JSON type for config option: " + path);
        }
        if (!value.is_null() && option->type == ConfigOptionType::Enum &&
            !cubey::config_option_has_choice(*option, value.get<std::string>())) {
            throw std::runtime_error("invalid enum value for config option: " + path);
        }
        if (!value.is_null() && option->type == ConfigOptionType::Float) {
            validate_range(value.get<double>(), *option);
        }
        if (!value.is_null() &&
            (option->type == ConfigOptionType::UInt32 || option->type == ConfigOptionType::UInt64 ||
             option->type == ConfigOptionType::Int)) {
            validate_range(value.get<double>(), *option);
        }
        if (option->id == RunConfigOptionId::OutputPath && !value.is_null()) {
            output_path_set = true;
        }
    }
}

void apply_json_options(const nlohmann::json& object, std::string_view prefix, RunConfig& config) {
    for (const auto& [key, value] : object.items()) {
        const std::string path = json_path_string(prefix, key);
        if (value.is_object()) {
            apply_json_options(value, path, config);
            continue;
        }
        if (value.is_null()) {
            continue;
        }
        cubey::set_run_config_option_from_string(config, path, json_value_to_set_string(value));
    }
}

void set_json_path(nlohmann::json& root, std::string_view path, nlohmann::json value) {
    nlohmann::json* cursor = &root;
    std::size_t begin = 0;
    while (begin < path.size()) {
        const std::size_t dot = path.find('.', begin);
        const std::string key(path.substr(
            begin, dot == std::string_view::npos ? std::string_view::npos : dot - begin));
        if (dot == std::string_view::npos) {
            (*cursor)[key] = std::move(value);
            return;
        }
        cursor = &((*cursor)[key]);
        begin = dot + 1U;
    }
}

nlohmann::json option_to_json(const RunConfig& config, const ConfigOptionDescriptor& option) {
    const auto optional_float = [](float value) -> nlohmann::json {
        if (!cubey::run_config_float_is_set(value)) {
            return nullptr;
        }
        return value;
    };
    const auto optional_uint32 = [](std::uint32_t value) -> nlohmann::json {
        if (value == 0U) {
            return nullptr;
        }
        return value;
    };
    const auto optional_bool = [](int value) -> nlohmann::json {
        if (value < 0) {
            return nullptr;
        }
        return value != 0;
    };

    switch (option.id) {
    case RunConfigOptionId::Title:
        return config.title;
    case RunConfigOptionId::Width:
        return config.width;
    case RunConfigOptionId::Height:
        return config.height;
    case RunConfigOptionId::Frames:
        return config.frames;
    case RunConfigOptionId::Fps:
        return config.fps;
    case RunConfigOptionId::OutputPath:
        return config.output_path.string();
    case RunConfigOptionId::DebugView:
        return config.debug_view;
    case RunConfigOptionId::Headless:
        return config.headless;
    case RunConfigOptionId::Validation:
        return config.validation;
    case RunConfigOptionId::RequireValidation:
        return config.require_validation;
    case RunConfigOptionId::PrintFrameStats:
        return config.print_frame_stats;
    case RunConfigOptionId::CaptureMode:
        return config.capture_mode == cubey::CaptureMode::Video ? "video" : "png";
    case RunConfigOptionId::GridWidth:
        return optional_uint32(config.grid.width);
    case RunConfigOptionId::GridHeight:
        return optional_uint32(config.grid.height);
    case RunConfigOptionId::GridDepth:
        return optional_uint32(config.grid.depth);
    case RunConfigOptionId::ProfileOutput:
        return config.profile_output_prefix.empty()
                   ? nlohmann::json(nullptr)
                   : nlohmann::json(config.profile_output_prefix.string());
    case RunConfigOptionId::ProfileWarmupFrames:
        return config.profile_warmup_frames;
    case RunConfigOptionId::ProfileDiagnostics:
        return config.profile_diagnostics;
    case RunConfigOptionId::ProfileDiagnosticInterval:
        return config.profile_diagnostic_interval;
    case RunConfigOptionId::GltfInput:
        return config.gltf.input_path.string();
    case RunConfigOptionId::GltfAnimationIndex:
        return config.gltf.animation_index;
    case RunConfigOptionId::GltfAnimationSpeed:
        return config.gltf.animation_speed;
    case RunConfigOptionId::GltfAnimationPaused:
        return config.gltf.animation_paused;
    case RunConfigOptionId::PbrEnvironment:
        return config.pbr.environment_path.string();
    case RunConfigOptionId::PbrEnvironmentSource:
        return config.pbr.environment_source;
    case RunConfigOptionId::PbrIblIntensity:
        return config.pbr.ibl_intensity;
    case RunConfigOptionId::PbrEnvironmentRotation:
        return config.pbr.environment_rotation_degrees;
    case RunConfigOptionId::PbrExposure:
        return config.pbr.exposure;
    case RunConfigOptionId::OceanMapSize:
        return config.ocean.map_size == 0U ? nlohmann::json(nullptr)
                                           : nlohmann::json(config.ocean.map_size);
    case RunConfigOptionId::OceanFieldPrecision:
        return config.ocean.field_precision.empty() ? nlohmann::json(nullptr)
                                                    : nlohmann::json(config.ocean.field_precision);
    case RunConfigOptionId::OceanSurfaceMode:
        return config.ocean.surface_mode.empty() ? nlohmann::json(nullptr)
                                                 : nlohmann::json(config.ocean.surface_mode);
    case RunConfigOptionId::OceanCameraPreset:
        return config.ocean.camera_preset.empty() ? nlohmann::json(nullptr)
                                                  : nlohmann::json(config.ocean.camera_preset);
    case RunConfigOptionId::OceanPlanetRadiusScale:
        return optional_float(config.ocean.planet_radius_scale);
    case RunConfigOptionId::OceanCurvatureStartRatio:
        return optional_float(config.ocean.curvature_start_ratio);
    case RunConfigOptionId::OceanCurvatureEndRatio:
        return optional_float(config.ocean.curvature_end_ratio);
    case RunConfigOptionId::OceanCurvatureStrength:
        return optional_float(config.ocean.curvature_strength);
    case RunConfigOptionId::OceanCascade:
        return config.ocean.cascade < 0 ? nlohmann::json("all")
                                        : nlohmann::json(std::to_string(config.ocean.cascade));
    case RunConfigOptionId::OceanSpectralDomains:
        return optional_bool(config.ocean.spectral_domains);
    case RunConfigOptionId::OceanTerrainFields:
        return optional_bool(config.ocean.terrain_fields);
    case RunConfigOptionId::OceanWireOverlay:
        return config.ocean.wire_overlay;
    case RunConfigOptionId::OceanWireOpacity:
        return optional_float(config.ocean.wire_opacity);
    case RunConfigOptionId::PlanetScalePreset:
        return config.planet.scale_preset.empty() ? nlohmann::json(nullptr)
                                                  : nlohmann::json(config.planet.scale_preset);
    case RunConfigOptionId::PlanetRadius:
        return optional_float(config.planet.radius_m);
    case RunConfigOptionId::PlanetAtmosphereHeight:
        return optional_float(config.planet.atmosphere_height_m);
    case RunConfigOptionId::PlanetCameraAltitude:
        return optional_float(config.planet.camera_altitude_m);
    case RunConfigOptionId::PlanetCameraOrbitSpin:
        return optional_float(config.planet.camera_orbit_spin_degrees_per_second);
    case RunConfigOptionId::PlanetCameraSurfacePitch:
        return optional_float(config.planet.camera_surface_pitch_degrees);
    case RunConfigOptionId::PlanetCameraSurfaceYaw:
        return optional_float(config.planet.camera_surface_yaw_degrees);
    case RunConfigOptionId::PlanetCameraSurfaceLook:
        return config.planet.camera_surface_look.empty()
                   ? nlohmann::json(nullptr)
                   : nlohmann::json(config.planet.camera_surface_look);
    case RunConfigOptionId::PlanetPatchesPerFace:
        return optional_uint32(config.planet.patches_per_face);
    case RunConfigOptionId::PlanetPatchResolution:
        return optional_uint32(config.planet.patch_resolution);
    case RunConfigOptionId::PlanetMaxLodLevel:
        return config.planet.max_lod_level_set ? nlohmann::json(config.planet.max_lod_level)
                                               : nlohmann::json(nullptr);
    case RunConfigOptionId::PlanetLodTargetEdge:
        return optional_float(config.planet.lod_target_edge_px);
    case RunConfigOptionId::PlanetLodHysteresis:
        return optional_float(config.planet.lod_hysteresis);
    case RunConfigOptionId::PlanetLocalDetailLodLevels:
        return optional_uint32(config.planet.local_detail_lod_levels);
    case RunConfigOptionId::PlanetLocalDetailCells:
        return optional_uint32(config.planet.local_detail_cells_per_axis);
    case RunConfigOptionId::PlanetLocalDetailOuterExtent:
        return optional_float(config.planet.local_detail_outer_half_extent_m);
    case RunConfigOptionId::PlanetLocalDetail:
        return optional_bool(config.planet.local_detail_enabled);
    case RunConfigOptionId::PlanetLocalDetailHeight:
        return optional_float(config.planet.local_detail_height_strength_m);
    case RunConfigOptionId::PlanetLocalDetailScale:
        return optional_float(config.planet.local_detail_scale_m);
    case RunConfigOptionId::PlanetWireOverlay:
        return optional_bool(config.planet.wire_overlay);
    case RunConfigOptionId::PlanetSkirts:
        return optional_bool(config.planet.skirts_enabled);
    case RunConfigOptionId::PlanetSkirtDepthScale:
        return optional_float(config.planet.skirt_depth_scale);
    case RunConfigOptionId::PlanetTerrain:
        return optional_bool(config.planet.terrain_enabled);
    case RunConfigOptionId::PlanetTerrainHeightScale:
        return optional_float(config.planet.terrain_height_scale_m);
    case RunConfigOptionId::PlanetTerrainNoiseScale:
        return optional_float(config.planet.terrain_noise_scale);
    case RunConfigOptionId::PlanetTerrainMidDetailStrength:
        return optional_float(config.planet.terrain_mid_detail_strength);
    case RunConfigOptionId::PlanetTerrainFineDetailStrength:
        return optional_float(config.planet.terrain_fine_detail_strength);
    case RunConfigOptionId::PlanetTerrainFineDetailScale:
        return optional_float(config.planet.terrain_fine_detail_scale);
    case RunConfigOptionId::PlanetTerrainSeed:
        return config.planet.terrain_seed_set ? nlohmann::json(config.planet.terrain_seed)
                                              : nlohmann::json(nullptr);
    case RunConfigOptionId::PlanetSeaLevel:
        return optional_float(config.planet.sea_level_m);
    case RunConfigOptionId::PlanetBathymetryDepthScale:
        return optional_float(config.planet.bathymetry_depth_scale_m);
    case RunConfigOptionId::PlanetShorelineWidth:
        return optional_float(config.planet.shoreline_width_m);
    case RunConfigOptionId::PlanetAtmosphereHazeStrength:
        return optional_float(config.planet.atmosphere_haze_strength);
    case RunConfigOptionId::PlanetAtmosphereHazeStart:
        return optional_float(config.planet.atmosphere_haze_start);
    case RunConfigOptionId::PlanetAtmosphereHazeEnd:
        return optional_float(config.planet.atmosphere_haze_end);
    case RunConfigOptionId::PlanetAtmosphereAerialStrength:
        return optional_float(config.planet.atmosphere_aerial_strength);
    case RunConfigOptionId::PlanetDayOfYear:
        return optional_float(config.planet.day_of_year);
    case RunConfigOptionId::PlanetTimeHours:
        return optional_float(config.planet.time_hours);
    case RunConfigOptionId::PlanetTimeSpeed:
        return optional_float(config.planet.time_speed_hours_per_second);
    case RunConfigOptionId::PlanetTimePaused:
        return optional_bool(config.planet.time_paused);
    case RunConfigOptionId::PlanetCameraMode:
        return config.planet.camera_mode.empty() ? nlohmann::json(nullptr)
                                                 : nlohmann::json(config.planet.camera_mode);
    case RunConfigOptionId::PlanetAtmosphereMode:
        return config.planet.atmosphere_mode.empty()
                   ? nlohmann::json(nullptr)
                   : nlohmann::json(config.planet.atmosphere_mode);
    case RunConfigOptionId::TerrainSeed:
        return config.terrain.seed_set ? nlohmann::json(config.terrain.seed)
                                       : nlohmann::json(nullptr);
    case RunConfigOptionId::TerrainCellSize:
        return optional_float(config.terrain.cell_size);
    case RunConfigOptionId::TerrainSeaLevel:
        return optional_float(config.terrain.sea_level);
    case RunConfigOptionId::TerrainLandExtent:
        return optional_float(config.terrain.land_extent);
    case RunConfigOptionId::TerrainCoastNoise:
        return optional_float(config.terrain.coast_noise);
    case RunConfigOptionId::TerrainRelief:
        return optional_float(config.terrain.relief);
    case RunConfigOptionId::TerrainRidges:
        return optional_float(config.terrain.ridges);
    case RunConfigOptionId::TerrainValleys:
        return optional_float(config.terrain.valleys);
    case RunConfigOptionId::TerrainWaterSurface:
        return optional_bool(config.terrain.water_surface);
    case RunConfigOptionId::TerrainLabSlicePreset:
        return config.terrain_lab.slice_preset.empty()
                   ? nlohmann::json(nullptr)
                   : nlohmann::json(config.terrain_lab.slice_preset);
    case RunConfigOptionId::TerrainLabCameraPreset:
        return config.terrain_lab.camera_preset.empty()
                   ? nlohmann::json(nullptr)
                   : nlohmann::json(config.terrain_lab.camera_preset);
    case RunConfigOptionId::TerrainLabNoiseSource:
        return config.terrain_lab.noise_source.empty()
                   ? nlohmann::json(nullptr)
                   : nlohmann::json(config.terrain_lab.noise_source);
    case RunConfigOptionId::AtmospherePreset:
        return config.atmosphere.preset;
    case RunConfigOptionId::AtmosphereTimeOfDayMode:
        return config.atmosphere.time_of_day_mode;
    case RunConfigOptionId::AtmosphereNightSkyMode:
        return config.atmosphere.night_sky_mode;
    case RunConfigOptionId::AtmosphereMilkyWayLayer:
        return config.atmosphere.milky_way_layer;
    case RunConfigOptionId::AtmosphereSunElevation:
        return optional_float(config.atmosphere.sun_elevation_degrees);
    case RunConfigOptionId::AtmosphereSunAzimuth:
        return optional_float(config.atmosphere.sun_azimuth_degrees);
    case RunConfigOptionId::AtmosphereCameraAltitude:
        return optional_float(config.atmosphere.camera_altitude_km);
    case RunConfigOptionId::AtmosphereCameraYawOffset:
        return optional_float(config.atmosphere.camera_yaw_offset_degrees);
    case RunConfigOptionId::AtmosphereCameraPitchOffset:
        return optional_float(config.atmosphere.camera_pitch_offset_degrees);
    case RunConfigOptionId::AtmosphereRayleighScale:
        return optional_float(config.atmosphere.rayleigh_scale);
    case RunConfigOptionId::AtmosphereMieScale:
        return optional_float(config.atmosphere.mie_scale);
    case RunConfigOptionId::AtmosphereOzoneScale:
        return optional_float(config.atmosphere.ozone_scale);
    case RunConfigOptionId::AtmosphereTimeHours:
        return optional_float(config.atmosphere.time_hours);
    case RunConfigOptionId::AtmosphereDayOfYear:
        return optional_float(config.atmosphere.day_of_year);
    case RunConfigOptionId::AtmosphereLatitude:
        return optional_float(config.atmosphere.latitude_degrees);
    case RunConfigOptionId::AtmosphereSunAzimuthOffset:
        return optional_float(config.atmosphere.sun_azimuth_offset_degrees);
    case RunConfigOptionId::AtmosphereTimeSpeed:
        return optional_float(config.atmosphere.time_speed_hours_per_second);
    case RunConfigOptionId::AtmosphereTimePaused:
        return optional_bool(config.atmosphere.time_paused);
    case RunConfigOptionId::AtmosphereAutoExposure:
        return optional_bool(config.atmosphere.auto_exposure);
    case RunConfigOptionId::AtmosphereExposureBias:
        return optional_float(config.atmosphere.exposure_bias);
    case RunConfigOptionId::AtmosphereTwilightStrength:
        return optional_float(config.atmosphere.twilight_strength);
    case RunConfigOptionId::AtmosphereTwilightHorizonWarmth:
        return optional_float(config.atmosphere.twilight_horizon_warmth);
    case RunConfigOptionId::AtmosphereStarIntensity:
        return optional_float(config.atmosphere.star_intensity);
    case RunConfigOptionId::AtmosphereStarDensity:
        return optional_float(config.atmosphere.star_density);
    case RunConfigOptionId::AtmosphereMilkyWayIntensity:
        return optional_float(config.atmosphere.milky_way_intensity);
    case RunConfigOptionId::AtmosphereMilkyWayContrast:
        return optional_float(config.atmosphere.milky_way_contrast);
    case RunConfigOptionId::AtmosphereLightPollution:
        return optional_float(config.atmosphere.light_pollution);
    case RunConfigOptionId::AtmosphereMilkyWayVariation:
        return optional_float(config.atmosphere.milky_way_variation);
    case RunConfigOptionId::AtmosphereMoonIntensity:
        return optional_float(config.atmosphere.moon_intensity);
    case RunConfigOptionId::AtmosphereMoonlightIntensity:
        return optional_float(config.atmosphere.moonlight_intensity);
    case RunConfigOptionId::AtmosphereMoonPhaseOffset:
        return optional_float(config.atmosphere.moon_phase_offset_days);
    case RunConfigOptionId::AtmosphereMoonSizeScale:
        return optional_float(config.atmosphere.moon_size_scale);
    case RunConfigOptionId::AtmosphereMoon:
        return optional_bool(config.atmosphere.moon);
    case RunConfigOptionId::AtmosphereReferenceGeometry:
        return optional_bool(config.atmosphere.reference_geometry);
    case RunConfigOptionId::CloudEnabled:
        return optional_bool(config.clouds.enabled);
    case RunConfigOptionId::CloudDebugView:
        return config.clouds.debug_view.empty() ? nlohmann::json(nullptr)
                                                : nlohmann::json(config.clouds.debug_view);
    case RunConfigOptionId::CloudCameraMode:
        return config.clouds.camera_mode.empty() ? nlohmann::json(nullptr)
                                                 : nlohmann::json(config.clouds.camera_mode);
    case RunConfigOptionId::CloudQuality:
        return config.clouds.quality.empty() ? nlohmann::json(nullptr)
                                             : nlohmann::json(config.clouds.quality);
    case RunConfigOptionId::CloudWeatherPreset:
        return config.clouds.weather_preset.empty() ? nlohmann::json(nullptr)
                                                    : nlohmann::json(config.clouds.weather_preset);
    case RunConfigOptionId::CloudCacheFrames:
        return config.clouds.cache_frames.empty() ? nlohmann::json(nullptr)
                                                  : nlohmann::json(config.clouds.cache_frames);
    case RunConfigOptionId::CloudCacheTextureSize:
        return optional_uint32(config.clouds.cache_texture_size);
    case RunConfigOptionId::CloudRenderPath:
        return config.clouds.render_path.empty() ? nlohmann::json(nullptr)
                                                 : nlohmann::json(config.clouds.render_path);
    case RunConfigOptionId::CloudSamplingMode:
        return config.clouds.sampling_mode.empty() ? nlohmann::json(nullptr)
                                                   : nlohmann::json(config.clouds.sampling_mode);
    case RunConfigOptionId::CloudDensityModel:
        return config.clouds.density_model.empty() ? nlohmann::json(nullptr)
                                                   : nlohmann::json(config.clouds.density_model);
    case RunConfigOptionId::CloudResolveMode:
        return config.clouds.resolve_mode.empty() ? nlohmann::json(nullptr)
                                                  : nlohmann::json(config.clouds.resolve_mode);
    case RunConfigOptionId::CloudBackgroundMode:
        return config.clouds.background_mode.empty()
                   ? nlohmann::json(nullptr)
                   : nlohmann::json(config.clouds.background_mode);
    case RunConfigOptionId::CloudDistanceMode:
        return config.clouds.distance_mode.empty()
                   ? nlohmann::json(nullptr)
                   : nlohmann::json(config.clouds.distance_mode);
    case RunConfigOptionId::CloudOrbitRepresentation:
        return config.clouds.orbit_representation.empty()
                   ? nlohmann::json(nullptr)
                   : nlohmann::json(config.clouds.orbit_representation);
    case RunConfigOptionId::CloudPlanetRadius:
        return optional_float(config.clouds.planet_radius_m);
    case RunConfigOptionId::CloudCameraAltitude:
        return optional_float(config.clouds.camera_altitude_m);
    case RunConfigOptionId::CloudBottomAltitude:
        return optional_float(config.clouds.bottom_altitude_m);
    case RunConfigOptionId::CloudTopAltitude:
        return optional_float(config.clouds.top_altitude_m);
    case RunConfigOptionId::CloudCoverage:
        return optional_float(config.clouds.coverage);
    case RunConfigOptionId::CloudDensity:
        return optional_float(config.clouds.density);
    case RunConfigOptionId::CloudWeatherScale:
        return optional_float(config.clouds.weather_scale_km);
    case RunConfigOptionId::CloudShapeDomain:
        return optional_float(config.clouds.shape_domain_km);
    case RunConfigOptionId::CloudFootprintFilterStrength:
        return optional_float(config.clouds.footprint_filter_strength);
    case RunConfigOptionId::CloudEdgeSoftness:
        return optional_float(config.clouds.edge_softness);
    case RunConfigOptionId::CloudEdgeDetailFade:
        return optional_float(config.clouds.edge_detail_fade);
    case RunConfigOptionId::CloudEdgeResolveStrength:
        return optional_float(config.clouds.edge_resolve_strength);
    case RunConfigOptionId::CloudVerticalShearFraction:
        return optional_float(config.clouds.vertical_shear_fraction);
    case RunConfigOptionId::CloudWindSpeed:
        return optional_float(config.clouds.wind_speed_mps);
    case RunConfigOptionId::CloudShadowStrength:
        return optional_float(config.clouds.shadow_strength);
    case RunConfigOptionId::CloudHorizonStrength:
        return optional_float(config.clouds.horizon_strength);
    case RunConfigOptionId::CloudWeatherFronts:
        return optional_float(config.clouds.weather_fronts);
    case RunConfigOptionId::CloudWeatherCells:
        return optional_float(config.clouds.weather_cells);
    case RunConfigOptionId::CloudWeatherStreaks:
        return optional_float(config.clouds.weather_streaks);
    case RunConfigOptionId::CloudWeatherSoftness:
        return optional_float(config.clouds.weather_softness);
    case RunConfigOptionId::CloudWeatherInfluence:
        return optional_float(config.clouds.weather_influence);
    case RunConfigOptionId::CloudDetailErosion:
        return optional_float(config.clouds.detail_erosion);
    case RunConfigOptionId::CloudAmbientStrength:
        return optional_float(config.clouds.ambient_strength);
    case RunConfigOptionId::CloudDirectStrength:
        return optional_float(config.clouds.direct_strength);
    case RunConfigOptionId::CloudPhaseStrength:
        return optional_float(config.clouds.phase_strength);
    case RunConfigOptionId::CloudFinalContrast:
        return optional_float(config.clouds.final_contrast);
    case RunConfigOptionId::CloudFinalSaturation:
        return optional_float(config.clouds.final_saturation);
    case RunConfigOptionId::CloudResolveStrength:
        return optional_float(config.clouds.resolve_strength);
    case RunConfigOptionId::CloudResolveRadius:
        return optional_float(config.clouds.resolve_radius_px);
    case RunConfigOptionId::CloudHorizonGlowStrength:
        return optional_float(config.clouds.horizon_glow_strength);
    case RunConfigOptionId::CloudSunGlareStrength:
        return optional_float(config.clouds.sun_glare_strength);
    case RunConfigOptionId::CloudJitterStrength:
        return optional_float(config.clouds.jitter_strength);
    case RunConfigOptionId::CloudOrbitTransitionStart:
        return optional_float(config.clouds.orbit_transition_start_m);
    case RunConfigOptionId::CloudOrbitTransitionEnd:
        return optional_float(config.clouds.orbit_transition_end_m);
    case RunConfigOptionId::CloudFarShellStart:
        return optional_float(config.clouds.far_shell_start_m);
    case RunConfigOptionId::CloudFarShellEnd:
        return optional_float(config.clouds.far_shell_end_m);
    case RunConfigOptionId::CloudFarShellStrength:
        return optional_float(config.clouds.far_shell_strength);
    case RunConfigOptionId::CloudOrbitDetailStrength:
        return optional_float(config.clouds.orbit_detail_strength);
    case RunConfigOptionId::CloudOrbitDensityScale:
        return optional_float(config.clouds.orbit_density_scale);
    case RunConfigOptionId::CloudOrbitFill:
        return optional_float(config.clouds.orbit_fill);
    case RunConfigOptionId::CloudOrbitMotionStrength:
        return optional_float(config.clouds.orbit_motion_strength);
    case RunConfigOptionId::CloudOrbitShellExtinction:
        return optional_float(config.clouds.orbit_shell_extinction);
    case RunConfigOptionId::CloudTemporal:
        return optional_bool(config.clouds.temporal);
    case RunConfigOptionId::CloudLocalVolume:
        return optional_bool(config.clouds.local_volume);
    case RunConfigOptionId::CloudHorizonLayer:
        return optional_bool(config.clouds.horizon_layer);
    case RunConfigOptionId::SmokeInjectors:
        return optional_uint32(config.smoke.injectors);
    case RunConfigOptionId::SmokePressureIterations:
        return optional_uint32(config.smoke.pressure_iterations);
    case RunConfigOptionId::SmokePressureSolver:
        return config.smoke.pressure_solver.empty() ? nlohmann::json(nullptr)
                                                    : nlohmann::json(config.smoke.pressure_solver);
    case RunConfigOptionId::SmokeDyeDecay:
        return optional_float(config.smoke.dye_decay);
    case RunConfigOptionId::SmokeVelocityDecay:
        return optional_float(config.smoke.velocity_decay);
    case RunConfigOptionId::SmokeInjectorRadius:
        return optional_float(config.smoke.injector_radius);
    case RunConfigOptionId::SmokeInjectorForce:
        return optional_float(config.smoke.injector_force);
    case RunConfigOptionId::SmokeInjectorPropulsion:
        return optional_float(config.smoke.injector_propulsion);
    case RunConfigOptionId::SmokeInjectorOrbitRadius:
        return optional_float(config.smoke.injector_orbit_radius);
    case RunConfigOptionId::SmokeInjectorOrbitRadiusSpread:
        return optional_float(config.smoke.injector_orbit_radius_spread);
    case RunConfigOptionId::SmokeInjectorOrbitAngularSpeed:
        return optional_float(config.smoke.injector_orbit_angular_speed);
    case RunConfigOptionId::SmokeInjectorOrbitAngularSpeedSpread:
        return optional_float(config.smoke.injector_orbit_angular_speed_spread);
    case RunConfigOptionId::SmokeInjectorOrbitPhaseSpread:
        return optional_float(config.smoke.injector_orbit_phase_spread);
    case RunConfigOptionId::SmokeVorticity:
        return optional_float(config.smoke.vorticity);
    case RunConfigOptionId::PyroShadowGridWidth:
        return optional_uint32(config.pyro.shadow_grid.width);
    case RunConfigOptionId::PyroShadowGridHeight:
        return optional_uint32(config.pyro.shadow_grid.height);
    case RunConfigOptionId::PyroShadowGridDepth:
        return optional_uint32(config.pyro.shadow_grid.depth);
    case RunConfigOptionId::PyroShadowSteps:
        return optional_uint32(config.pyro.shadow_steps);
    case RunConfigOptionId::PyroShadowUpdateInterval:
        return optional_uint32(config.pyro.shadow_update_interval);
    case RunConfigOptionId::PyroSources:
        return optional_uint32(config.pyro.sources);
    case RunConfigOptionId::PyroSourceHeight:
        return optional_float(config.pyro.source_height);
    case RunConfigOptionId::PyroSourceRadius:
        return optional_float(config.pyro.source_radius);
    case RunConfigOptionId::PyroSourceForce:
        return optional_float(config.pyro.source_force);
    case RunConfigOptionId::PyroSoot:
        return optional_float(config.pyro.soot);
    case RunConfigOptionId::PyroTemperature:
        return optional_float(config.pyro.temperature);
    case RunConfigOptionId::PyroFuel:
        return optional_float(config.pyro.fuel);
    case RunConfigOptionId::PyroBuoyancy:
        return optional_float(config.pyro.buoyancy);
    case RunConfigOptionId::PyroIgnitionTemperature:
        return optional_float(config.pyro.ignition_temperature);
    case RunConfigOptionId::PyroBurnRate:
        return optional_float(config.pyro.burn_rate);
    case RunConfigOptionId::PyroHeatOutput:
        return optional_float(config.pyro.heat_output);
    case RunConfigOptionId::PyroSootYield:
        return optional_float(config.pyro.soot_yield);
    case RunConfigOptionId::PyroExpansion:
        return optional_float(config.pyro.expansion);
    case RunConfigOptionId::PyroFlameCooling:
        return optional_float(config.pyro.flame_cooling);
    case RunConfigOptionId::PyroShredding:
        return optional_float(config.pyro.shredding);
    case RunConfigOptionId::PyroTurbulence:
        return optional_float(config.pyro.turbulence);
    case RunConfigOptionId::PyroObstacleHeight:
        return optional_float(config.pyro.obstacle_height);
    case RunConfigOptionId::PyroObstacleRadius:
        return optional_float(config.pyro.obstacle_radius);
    case RunConfigOptionId::PyroExplosionInterval:
        return optional_float(config.pyro.explosion_interval_seconds);
    case RunConfigOptionId::PyroExplosionDuration:
        return optional_float(config.pyro.explosion_duration_seconds);
    case RunConfigOptionId::PyroExplosionBoost:
        return optional_float(config.pyro.explosion_boost);
    case RunConfigOptionId::Water2DTransfer:
        return config.water2d.transfer_mode.empty() ? nlohmann::json(nullptr)
                                                    : nlohmann::json(config.water2d.transfer_mode);
    case RunConfigOptionId::Water2DTransferLimit:
        return optional_uint32(config.water2d.transfer_limit);
    case RunConfigOptionId::Water2DHose:
        return optional_bool(config.water2d.hose);
    case RunConfigOptionId::Water2DDrain:
        return optional_bool(config.water2d.drain);
    case RunConfigOptionId::Water2DWave:
        return optional_bool(config.water2d.wave);
    case RunConfigOptionId::Water3DTransfer:
        return config.water3d.transfer_mode.empty() ? nlohmann::json(nullptr)
                                                    : nlohmann::json(config.water3d.transfer_mode);
    case RunConfigOptionId::Water3DTransferLimit:
        return optional_uint32(config.water3d.transfer_limit);
    case RunConfigOptionId::Water3DP2GMode:
        return config.water3d.p2g_mode.empty() ? nlohmann::json(nullptr)
                                               : nlohmann::json(config.water3d.p2g_mode);
    case RunConfigOptionId::Water3DHose:
        return optional_bool(config.water3d.hose);
    case RunConfigOptionId::Water3DDrain:
        return optional_bool(config.water3d.drain);
    case RunConfigOptionId::Water3DRain:
        return optional_bool(config.water3d.rain);
    case RunConfigOptionId::Water3DWave:
        return optional_bool(config.water3d.wave);
    case RunConfigOptionId::Water3DWhitewater:
        return optional_bool(config.water3d.whitewater);
    }
    return nullptr;
}

} // namespace

namespace lazy::serializable {

using JsonAdapter = NlohmannJsonAdapter;

inline void serialize(JsonAdapter& adapter, const RunConfig::OceanOptions& options) {
    adapter.writeField<std::uint32_t>("map_size", options.map_size);
    adapter.writeField<std::string>("field_precision", options.field_precision);
    adapter.writeField<std::string>("surface_mode", options.surface_mode);
    adapter.writeField<std::string>("camera_preset", options.camera_preset);
    adapter.writeField<int>("cascade", options.cascade);
    adapter.writeField<int>("spectral_domains", options.spectral_domains);
    adapter.writeField<int>("terrain_fields", options.terrain_fields);
    adapter.writeField<float>("planet_radius_scale", options.planet_radius_scale);
    adapter.writeField<float>("curvature_start_ratio", options.curvature_start_ratio);
    adapter.writeField<float>("curvature_end_ratio", options.curvature_end_ratio);
    adapter.writeField<float>("curvature_strength", options.curvature_strength);
    adapter.writeField<float>("wire_opacity", options.wire_opacity);
    adapter.writeField<bool>("wire_overlay", options.wire_overlay);
}

inline void deserialize(JsonAdapter& adapter, RunConfig::OceanOptions& options) {
    std::string cascade = options.cascade < 0 ? "all" : std::to_string(options.cascade);
    adapter.readField<std::uint32_t>("map_size", options.map_size);
    adapter.readField<std::string>("field_precision", options.field_precision);
    adapter.readField<std::string>("surface_mode", options.surface_mode);
    adapter.readField<std::string>("camera_preset", options.camera_preset);
    adapter.readField<std::string>("cascade", cascade);
    adapter.readField<int>("spectral_domains", options.spectral_domains);
    adapter.readField<int>("terrain_fields", options.terrain_fields);
    adapter.readField<float>("planet_radius_scale", options.planet_radius_scale);
    adapter.readField<float>("curvature_start_ratio", options.curvature_start_ratio);
    adapter.readField<float>("curvature_end_ratio", options.curvature_end_ratio);
    adapter.readField<float>("curvature_strength", options.curvature_strength);
    adapter.readField<float>("wire_opacity", options.wire_opacity);
    adapter.readField<bool>("wire_overlay", options.wire_overlay);
    if (const ConfigOptionDescriptor* option = cubey::find_run_config_option("ocean.cascade")) {
        options.cascade = parse_ocean_cascade(cascade, *option);
    }
}

inline void serialize(JsonAdapter& adapter, const RunConfig::PlanetOptions& options) {
    adapter.writeField<std::string>("scale_preset", options.scale_preset);
    adapter.writeField<float>("radius_m", options.radius_m);
    adapter.writeField<float>("atmosphere_height_m", options.atmosphere_height_m);
    adapter.writeField<float>("camera_altitude_m", options.camera_altitude_m);
    adapter.writeField<float>("camera_orbit_spin_degrees_per_second",
                              options.camera_orbit_spin_degrees_per_second);
    adapter.writeField<float>("camera_surface_pitch_degrees", options.camera_surface_pitch_degrees);
    adapter.writeField<float>("camera_surface_yaw_degrees", options.camera_surface_yaw_degrees);
    adapter.writeField<std::string>("camera_surface_look", options.camera_surface_look);
    adapter.writeField<std::uint32_t>("patches_per_face", options.patches_per_face);
    adapter.writeField<std::uint32_t>("patch_resolution", options.patch_resolution);
    adapter.writeField<std::uint32_t>("max_lod_level", options.max_lod_level);
    adapter.writeField<float>("lod_target_edge_px", options.lod_target_edge_px);
    adapter.writeField<float>("lod_hysteresis", options.lod_hysteresis);
    adapter.writeField<std::uint32_t>("local_detail_lod_levels", options.local_detail_lod_levels);
    adapter.writeField<std::uint32_t>("local_detail_cells_per_axis",
                                      options.local_detail_cells_per_axis);
    adapter.writeField<float>("local_detail_outer_half_extent_m",
                              options.local_detail_outer_half_extent_m);
    adapter.writeField<int>("local_detail_enabled", options.local_detail_enabled);
    adapter.writeField<float>("local_detail_height_strength_m",
                              options.local_detail_height_strength_m);
    adapter.writeField<float>("local_detail_scale_m", options.local_detail_scale_m);
    adapter.writeField<int>("wire_overlay", options.wire_overlay);
    adapter.writeField<int>("skirts_enabled", options.skirts_enabled);
    adapter.writeField<float>("skirt_depth_scale", options.skirt_depth_scale);
    adapter.writeField<int>("terrain_enabled", options.terrain_enabled);
    adapter.writeField<float>("terrain_height_scale_m", options.terrain_height_scale_m);
    adapter.writeField<float>("terrain_noise_scale", options.terrain_noise_scale);
    adapter.writeField<std::uint32_t>("terrain_seed", options.terrain_seed);
    adapter.writeField<float>("sea_level_m", options.sea_level_m);
    adapter.writeField<float>("bathymetry_depth_scale_m", options.bathymetry_depth_scale_m);
    adapter.writeField<float>("shoreline_width_m", options.shoreline_width_m);
    adapter.writeField<float>("atmosphere_haze_strength", options.atmosphere_haze_strength);
    adapter.writeField<float>("atmosphere_haze_start", options.atmosphere_haze_start);
    adapter.writeField<float>("atmosphere_haze_end", options.atmosphere_haze_end);
    adapter.writeField<float>("atmosphere_aerial_strength", options.atmosphere_aerial_strength);
    adapter.writeField<float>("day_of_year", options.day_of_year);
    adapter.writeField<float>("time_hours", options.time_hours);
    adapter.writeField<float>("time_speed_hours_per_second", options.time_speed_hours_per_second);
    adapter.writeField<int>("time_paused", options.time_paused);
    adapter.writeField<std::string>("camera_mode", options.camera_mode);
    adapter.writeField<std::string>("atmosphere_mode", options.atmosphere_mode);
}

inline void deserialize(JsonAdapter& adapter, RunConfig::PlanetOptions& options) {
    adapter.readField<std::string>("scale_preset", options.scale_preset);
    adapter.readField<float>("radius_m", options.radius_m);
    adapter.readField<float>("atmosphere_height_m", options.atmosphere_height_m);
    adapter.readField<float>("camera_altitude_m", options.camera_altitude_m);
    adapter.readField<float>("camera_orbit_spin_degrees_per_second",
                             options.camera_orbit_spin_degrees_per_second);
    adapter.readField<float>("camera_surface_pitch_degrees", options.camera_surface_pitch_degrees);
    adapter.readField<float>("camera_surface_yaw_degrees", options.camera_surface_yaw_degrees);
    adapter.readField<std::string>("camera_surface_look", options.camera_surface_look);
    adapter.readField<std::uint32_t>("patches_per_face", options.patches_per_face);
    adapter.readField<std::uint32_t>("patch_resolution", options.patch_resolution);
    adapter.readField<std::uint32_t>("max_lod_level", options.max_lod_level);
    adapter.readField<float>("lod_target_edge_px", options.lod_target_edge_px);
    adapter.readField<float>("lod_hysteresis", options.lod_hysteresis);
    adapter.readField<std::uint32_t>("local_detail_lod_levels", options.local_detail_lod_levels);
    adapter.readField<std::uint32_t>("local_detail_cells_per_axis",
                                     options.local_detail_cells_per_axis);
    adapter.readField<float>("local_detail_outer_half_extent_m",
                             options.local_detail_outer_half_extent_m);
    adapter.readField<int>("local_detail_enabled", options.local_detail_enabled);
    adapter.readField<float>("local_detail_height_strength_m",
                             options.local_detail_height_strength_m);
    adapter.readField<float>("local_detail_scale_m", options.local_detail_scale_m);
    adapter.readField<int>("wire_overlay", options.wire_overlay);
    adapter.readField<int>("skirts_enabled", options.skirts_enabled);
    adapter.readField<float>("skirt_depth_scale", options.skirt_depth_scale);
    adapter.readField<int>("terrain_enabled", options.terrain_enabled);
    adapter.readField<float>("terrain_height_scale_m", options.terrain_height_scale_m);
    adapter.readField<float>("terrain_noise_scale", options.terrain_noise_scale);
    adapter.readField<std::uint32_t>("terrain_seed", options.terrain_seed);
    adapter.readField<float>("sea_level_m", options.sea_level_m);
    adapter.readField<float>("bathymetry_depth_scale_m", options.bathymetry_depth_scale_m);
    adapter.readField<float>("shoreline_width_m", options.shoreline_width_m);
    adapter.readField<float>("atmosphere_haze_strength", options.atmosphere_haze_strength);
    adapter.readField<float>("atmosphere_haze_start", options.atmosphere_haze_start);
    adapter.readField<float>("atmosphere_haze_end", options.atmosphere_haze_end);
    adapter.readField<float>("atmosphere_aerial_strength", options.atmosphere_aerial_strength);
    adapter.readField<float>("day_of_year", options.day_of_year);
    adapter.readField<float>("time_hours", options.time_hours);
    adapter.readField<float>("time_speed_hours_per_second", options.time_speed_hours_per_second);
    adapter.readField<int>("time_paused", options.time_paused);
    adapter.readField<std::string>("camera_mode", options.camera_mode);
    adapter.readField<std::string>("atmosphere_mode", options.atmosphere_mode);
}

inline void serialize(JsonAdapter& adapter, const RunConfig::PbrOptions& options) {
    const std::string environment = options.environment_path.string();
    adapter.writeField<std::string>("environment", environment);
    adapter.writeField<std::string>("environment_source", options.environment_source);
    adapter.writeField<float>("ibl_intensity", options.ibl_intensity);
    adapter.writeField<float>("environment_rotation_degrees", options.environment_rotation_degrees);
    adapter.writeField<float>("exposure", options.exposure);
}

inline void deserialize(JsonAdapter& adapter, RunConfig::PbrOptions& options) {
    std::string environment = options.environment_path.string();
    adapter.readField<std::string>("environment", environment);
    adapter.readField<std::string>("environment_source", options.environment_source);
    adapter.readField<float>("ibl_intensity", options.ibl_intensity);
    adapter.readField<float>("environment_rotation_degrees", options.environment_rotation_degrees);
    adapter.readField<float>("exposure", options.exposure);
    if (!environment.empty()) {
        options.environment_path = environment;
    }
}

inline void serialize(JsonAdapter& adapter, const RunConfig::GltfOptions& options) {
    const std::string input = options.input_path.string();
    adapter.writeField<std::string>("input", input);
    adapter.writeField<std::uint32_t>("animation_index", options.animation_index);
    adapter.writeField<float>("animation_speed", options.animation_speed);
    adapter.writeField<bool>("animation_paused", options.animation_paused);
}

inline void deserialize(JsonAdapter& adapter, RunConfig::GltfOptions& options) {
    std::string input = options.input_path.string();
    adapter.readField<std::string>("input", input);
    adapter.readField<std::uint32_t>("animation_index", options.animation_index);
    adapter.readField<float>("animation_speed", options.animation_speed);
    adapter.readField<bool>("animation_paused", options.animation_paused);
    if (!input.empty()) {
        options.input_path = input;
    }
}

inline void serialize(JsonAdapter& adapter, const RunConfig::TerrainOptions& options) {
    adapter.writeField<std::uint64_t>("seed", options.seed);
    adapter.writeField<float>("cell_size", options.cell_size);
    adapter.writeField<float>("sea_level", options.sea_level);
    adapter.writeField<float>("land_extent", options.land_extent);
    adapter.writeField<float>("coast_noise", options.coast_noise);
    adapter.writeField<float>("relief", options.relief);
    adapter.writeField<float>("ridges", options.ridges);
    adapter.writeField<float>("valleys", options.valleys);
    adapter.writeField<int>("water_surface", options.water_surface);
}

inline void deserialize(JsonAdapter& adapter, RunConfig::TerrainOptions& options) {
    adapter.readField<std::uint64_t>("seed", options.seed);
    adapter.readField<float>("cell_size", options.cell_size);
    adapter.readField<float>("sea_level", options.sea_level);
    adapter.readField<float>("land_extent", options.land_extent);
    adapter.readField<float>("coast_noise", options.coast_noise);
    adapter.readField<float>("relief", options.relief);
    adapter.readField<float>("ridges", options.ridges);
    adapter.readField<float>("valleys", options.valleys);
    adapter.readField<int>("water_surface", options.water_surface);
}

inline void serialize(JsonAdapter& adapter, const RunConfig::TerrainLabOptions& options) {
    adapter.writeField<std::string>("slice_preset", options.slice_preset);
    adapter.writeField<std::string>("camera_preset", options.camera_preset);
    adapter.writeField<std::string>("noise_source", options.noise_source);
}

inline void deserialize(JsonAdapter& adapter, RunConfig::TerrainLabOptions& options) {
    adapter.readField<std::string>("slice_preset", options.slice_preset);
    adapter.readField<std::string>("camera_preset", options.camera_preset);
    adapter.readField<std::string>("noise_source", options.noise_source);
}

inline void serialize(JsonAdapter& adapter, const RunConfig::AtmosphereOptions& options) {
    adapter.writeField<std::string>("preset", options.preset);
    adapter.writeField<std::string>("time_of_day_mode", options.time_of_day_mode);
    adapter.writeField<std::string>("night_sky_mode", options.night_sky_mode);
    adapter.writeField<std::string>("milky_way_layer", options.milky_way_layer);
    adapter.writeField<float>("sun_elevation_degrees", options.sun_elevation_degrees);
    adapter.writeField<float>("sun_azimuth_degrees", options.sun_azimuth_degrees);
    adapter.writeField<float>("camera_altitude_km", options.camera_altitude_km);
    adapter.writeField<float>("camera_yaw_offset_degrees", options.camera_yaw_offset_degrees);
    adapter.writeField<float>("camera_pitch_offset_degrees", options.camera_pitch_offset_degrees);
    adapter.writeField<float>("rayleigh_scale", options.rayleigh_scale);
    adapter.writeField<float>("mie_scale", options.mie_scale);
    adapter.writeField<float>("ozone_scale", options.ozone_scale);
    adapter.writeField<float>("time_hours", options.time_hours);
    adapter.writeField<float>("day_of_year", options.day_of_year);
    adapter.writeField<float>("latitude_degrees", options.latitude_degrees);
    adapter.writeField<float>("sun_azimuth_offset_degrees", options.sun_azimuth_offset_degrees);
    adapter.writeField<float>("time_speed_hours_per_second", options.time_speed_hours_per_second);
    adapter.writeField<float>("exposure_bias", options.exposure_bias);
    adapter.writeField<float>("twilight_strength", options.twilight_strength);
    adapter.writeField<float>("twilight_horizon_warmth", options.twilight_horizon_warmth);
    adapter.writeField<float>("star_intensity", options.star_intensity);
    adapter.writeField<float>("star_density", options.star_density);
    adapter.writeField<float>("milky_way_intensity", options.milky_way_intensity);
    adapter.writeField<float>("milky_way_contrast", options.milky_way_contrast);
    adapter.writeField<float>("light_pollution", options.light_pollution);
    adapter.writeField<float>("milky_way_variation", options.milky_way_variation);
    adapter.writeField<float>("moon_intensity", options.moon_intensity);
    adapter.writeField<float>("moonlight_intensity", options.moonlight_intensity);
    adapter.writeField<float>("moon_phase_offset_days", options.moon_phase_offset_days);
    adapter.writeField<float>("moon_size_scale", options.moon_size_scale);
    adapter.writeField<int>("time_paused", options.time_paused);
    adapter.writeField<int>("auto_exposure", options.auto_exposure);
    adapter.writeField<int>("moon", options.moon);
    adapter.writeField<int>("reference_geometry", options.reference_geometry);
}

inline void deserialize(JsonAdapter& adapter, RunConfig::AtmosphereOptions& options) {
    adapter.readField<std::string>("preset", options.preset);
    adapter.readField<std::string>("time_of_day_mode", options.time_of_day_mode);
    adapter.readField<std::string>("night_sky_mode", options.night_sky_mode);
    adapter.readField<std::string>("milky_way_layer", options.milky_way_layer);
    adapter.readField<float>("sun_elevation_degrees", options.sun_elevation_degrees);
    adapter.readField<float>("sun_azimuth_degrees", options.sun_azimuth_degrees);
    adapter.readField<float>("camera_altitude_km", options.camera_altitude_km);
    adapter.readField<float>("camera_yaw_offset_degrees", options.camera_yaw_offset_degrees);
    adapter.readField<float>("camera_pitch_offset_degrees", options.camera_pitch_offset_degrees);
    adapter.readField<float>("rayleigh_scale", options.rayleigh_scale);
    adapter.readField<float>("mie_scale", options.mie_scale);
    adapter.readField<float>("ozone_scale", options.ozone_scale);
    adapter.readField<float>("time_hours", options.time_hours);
    adapter.readField<float>("day_of_year", options.day_of_year);
    adapter.readField<float>("latitude_degrees", options.latitude_degrees);
    adapter.readField<float>("sun_azimuth_offset_degrees", options.sun_azimuth_offset_degrees);
    adapter.readField<float>("time_speed_hours_per_second", options.time_speed_hours_per_second);
    adapter.readField<float>("exposure_bias", options.exposure_bias);
    adapter.readField<float>("twilight_strength", options.twilight_strength);
    adapter.readField<float>("twilight_horizon_warmth", options.twilight_horizon_warmth);
    adapter.readField<float>("star_intensity", options.star_intensity);
    adapter.readField<float>("star_density", options.star_density);
    adapter.readField<float>("milky_way_intensity", options.milky_way_intensity);
    adapter.readField<float>("milky_way_contrast", options.milky_way_contrast);
    adapter.readField<float>("light_pollution", options.light_pollution);
    adapter.readField<float>("milky_way_variation", options.milky_way_variation);
    adapter.readField<float>("moon_intensity", options.moon_intensity);
    adapter.readField<float>("moonlight_intensity", options.moonlight_intensity);
    adapter.readField<float>("moon_phase_offset_days", options.moon_phase_offset_days);
    adapter.readField<float>("moon_size_scale", options.moon_size_scale);
    adapter.readField<int>("time_paused", options.time_paused);
    adapter.readField<int>("auto_exposure", options.auto_exposure);
    adapter.readField<int>("moon", options.moon);
    adapter.readField<int>("reference_geometry", options.reference_geometry);
}

inline void serialize(JsonAdapter& adapter, const RunConfig::CloudOptions& options) {
    adapter.writeField<std::string>("debug_view", options.debug_view);
    adapter.writeField<std::string>("camera_mode", options.camera_mode);
    adapter.writeField<std::string>("quality", options.quality);
    adapter.writeField<std::string>("weather_preset", options.weather_preset);
    adapter.writeField<std::string>("cache_frames", options.cache_frames);
    adapter.writeField<std::uint32_t>("cache_texture_size", options.cache_texture_size);
    adapter.writeField<std::string>("render_path", options.render_path);
    adapter.writeField<std::string>("sampling_mode", options.sampling_mode);
    adapter.writeField<std::string>("density_model", options.density_model);
    adapter.writeField<std::string>("resolve_mode", options.resolve_mode);
    adapter.writeField<std::string>("background_mode", options.background_mode);
    adapter.writeField<std::string>("distance_mode", options.distance_mode);
    adapter.writeField<std::string>("orbit_representation", options.orbit_representation);
    adapter.writeField<float>("planet_radius_m", options.planet_radius_m);
    adapter.writeField<float>("camera_altitude_m", options.camera_altitude_m);
    adapter.writeField<float>("bottom_altitude_m", options.bottom_altitude_m);
    adapter.writeField<float>("top_altitude_m", options.top_altitude_m);
    adapter.writeField<float>("coverage", options.coverage);
    adapter.writeField<float>("density", options.density);
    adapter.writeField<float>("weather_scale_km", options.weather_scale_km);
    adapter.writeField<float>("shape_domain_km", options.shape_domain_km);
    adapter.writeField<float>("footprint_filter_strength", options.footprint_filter_strength);
    adapter.writeField<float>("edge_softness", options.edge_softness);
    adapter.writeField<float>("edge_detail_fade", options.edge_detail_fade);
    adapter.writeField<float>("edge_resolve_strength", options.edge_resolve_strength);
    adapter.writeField<float>("vertical_shear_fraction", options.vertical_shear_fraction);
    adapter.writeField<float>("wind_speed_mps", options.wind_speed_mps);
    adapter.writeField<float>("shadow_strength", options.shadow_strength);
    adapter.writeField<float>("horizon_strength", options.horizon_strength);
    adapter.writeField<float>("weather_fronts", options.weather_fronts);
    adapter.writeField<float>("weather_cells", options.weather_cells);
    adapter.writeField<float>("weather_streaks", options.weather_streaks);
    adapter.writeField<float>("weather_softness", options.weather_softness);
    adapter.writeField<float>("weather_influence", options.weather_influence);
    adapter.writeField<float>("detail_erosion", options.detail_erosion);
    adapter.writeField<float>("ambient_strength", options.ambient_strength);
    adapter.writeField<float>("direct_strength", options.direct_strength);
    adapter.writeField<float>("phase_strength", options.phase_strength);
    adapter.writeField<float>("final_contrast", options.final_contrast);
    adapter.writeField<float>("final_saturation", options.final_saturation);
    adapter.writeField<float>("resolve_strength", options.resolve_strength);
    adapter.writeField<float>("resolve_radius_px", options.resolve_radius_px);
    adapter.writeField<float>("horizon_glow_strength", options.horizon_glow_strength);
    adapter.writeField<float>("sun_glare_strength", options.sun_glare_strength);
    adapter.writeField<float>("jitter_strength", options.jitter_strength);
    adapter.writeField<float>("orbit_transition_start_m", options.orbit_transition_start_m);
    adapter.writeField<float>("orbit_transition_end_m", options.orbit_transition_end_m);
    adapter.writeField<float>("far_shell_start_m", options.far_shell_start_m);
    adapter.writeField<float>("far_shell_end_m", options.far_shell_end_m);
    adapter.writeField<float>("far_shell_strength", options.far_shell_strength);
    adapter.writeField<float>("orbit_detail_strength", options.orbit_detail_strength);
    adapter.writeField<float>("orbit_density_scale", options.orbit_density_scale);
    adapter.writeField<float>("orbit_fill", options.orbit_fill);
    adapter.writeField<float>("orbit_motion_strength", options.orbit_motion_strength);
    adapter.writeField<float>("orbit_shell_extinction", options.orbit_shell_extinction);
    adapter.writeField<int>("enabled", options.enabled);
    adapter.writeField<int>("temporal", options.temporal);
    adapter.writeField<int>("local_volume", options.local_volume);
    adapter.writeField<int>("horizon_layer", options.horizon_layer);
}

inline void deserialize(JsonAdapter& adapter, RunConfig::CloudOptions& options) {
    adapter.readField<std::string>("debug_view", options.debug_view);
    adapter.readField<std::string>("camera_mode", options.camera_mode);
    adapter.readField<std::string>("quality", options.quality);
    adapter.readField<std::string>("weather_preset", options.weather_preset);
    adapter.readField<std::string>("cache_frames", options.cache_frames);
    adapter.readField<std::uint32_t>("cache_texture_size", options.cache_texture_size);
    adapter.readField<std::string>("render_path", options.render_path);
    adapter.readField<std::string>("sampling_mode", options.sampling_mode);
    adapter.readField<std::string>("density_model", options.density_model);
    adapter.readField<std::string>("resolve_mode", options.resolve_mode);
    adapter.readField<std::string>("background_mode", options.background_mode);
    adapter.readField<std::string>("distance_mode", options.distance_mode);
    adapter.readField<std::string>("orbit_representation", options.orbit_representation);
    adapter.readField<float>("planet_radius_m", options.planet_radius_m);
    adapter.readField<float>("camera_altitude_m", options.camera_altitude_m);
    adapter.readField<float>("bottom_altitude_m", options.bottom_altitude_m);
    adapter.readField<float>("top_altitude_m", options.top_altitude_m);
    adapter.readField<float>("coverage", options.coverage);
    adapter.readField<float>("density", options.density);
    adapter.readField<float>("weather_scale_km", options.weather_scale_km);
    adapter.readField<float>("shape_domain_km", options.shape_domain_km);
    adapter.readField<float>("footprint_filter_strength", options.footprint_filter_strength);
    adapter.readField<float>("edge_softness", options.edge_softness);
    adapter.readField<float>("edge_detail_fade", options.edge_detail_fade);
    adapter.readField<float>("edge_resolve_strength", options.edge_resolve_strength);
    adapter.readField<float>("vertical_shear_fraction", options.vertical_shear_fraction);
    adapter.readField<float>("wind_speed_mps", options.wind_speed_mps);
    adapter.readField<float>("shadow_strength", options.shadow_strength);
    adapter.readField<float>("horizon_strength", options.horizon_strength);
    adapter.readField<float>("weather_fronts", options.weather_fronts);
    adapter.readField<float>("weather_cells", options.weather_cells);
    adapter.readField<float>("weather_streaks", options.weather_streaks);
    adapter.readField<float>("weather_softness", options.weather_softness);
    adapter.readField<float>("weather_influence", options.weather_influence);
    adapter.readField<float>("detail_erosion", options.detail_erosion);
    adapter.readField<float>("ambient_strength", options.ambient_strength);
    adapter.readField<float>("direct_strength", options.direct_strength);
    adapter.readField<float>("phase_strength", options.phase_strength);
    adapter.readField<float>("final_contrast", options.final_contrast);
    adapter.readField<float>("final_saturation", options.final_saturation);
    adapter.readField<float>("resolve_strength", options.resolve_strength);
    adapter.readField<float>("resolve_radius_px", options.resolve_radius_px);
    adapter.readField<float>("horizon_glow_strength", options.horizon_glow_strength);
    adapter.readField<float>("sun_glare_strength", options.sun_glare_strength);
    adapter.readField<float>("jitter_strength", options.jitter_strength);
    adapter.readField<float>("orbit_transition_start_m", options.orbit_transition_start_m);
    adapter.readField<float>("orbit_transition_end_m", options.orbit_transition_end_m);
    adapter.readField<float>("far_shell_start_m", options.far_shell_start_m);
    adapter.readField<float>("far_shell_end_m", options.far_shell_end_m);
    adapter.readField<float>("far_shell_strength", options.far_shell_strength);
    adapter.readField<float>("orbit_detail_strength", options.orbit_detail_strength);
    adapter.readField<float>("orbit_density_scale", options.orbit_density_scale);
    adapter.readField<float>("orbit_fill", options.orbit_fill);
    adapter.readField<float>("orbit_motion_strength", options.orbit_motion_strength);
    adapter.readField<float>("orbit_shell_extinction", options.orbit_shell_extinction);
    adapter.readField<int>("enabled", options.enabled);
    adapter.readField<int>("temporal", options.temporal);
    adapter.readField<int>("local_volume", options.local_volume);
    adapter.readField<int>("horizon_layer", options.horizon_layer);
}

inline void serialize(JsonAdapter& adapter, const RunConfig& config) {
    adapter.writeField<std::string>("title", config.title);
    adapter.writeField<std::uint32_t>("width", config.width);
    adapter.writeField<std::uint32_t>("height", config.height);
    adapter.writeField<std::uint32_t>("frames", config.frames);
    adapter.writeField<std::uint32_t>("fps", config.fps);
    const std::string output = config.output_path.string();
    adapter.writeField<std::string>("output", output);
    adapter.writeField<std::string>("debug_view", config.debug_view);
    adapter.writeField<bool>("headless", config.headless);
    adapter.writeField<bool>("validation", config.validation);
    adapter.writeField<bool>("require_validation", config.require_validation);
    adapter.writeField<bool>("print_frame_stats", config.print_frame_stats);
    const std::string capture = config.capture_mode == cubey::CaptureMode::Video ? "video" : "png";
    adapter.writeField<std::string>("capture", capture);
    adapter.writeField<RunConfig::GltfOptions>("gltf", config.gltf);
    adapter.writeField<RunConfig::PbrOptions>("pbr", config.pbr);
    adapter.writeField<RunConfig::OceanOptions>("ocean", config.ocean);
    adapter.writeField<RunConfig::PlanetOptions>("planet", config.planet);
    adapter.writeField<RunConfig::TerrainOptions>("terrain", config.terrain);
    adapter.writeField<RunConfig::TerrainLabOptions>("terrain_lab", config.terrain_lab);
    adapter.writeField<RunConfig::AtmosphereOptions>("atmosphere", config.atmosphere);
    adapter.writeField<RunConfig::CloudOptions>("clouds", config.clouds);
}

inline void deserialize(JsonAdapter& adapter, RunConfig& config) {
    std::string output = config.output_path.string();
    std::string capture = config.capture_mode == cubey::CaptureMode::Video ? "video" : "png";
    adapter.readField<std::string>("title", config.title);
    adapter.readField<std::uint32_t>("width", config.width);
    adapter.readField<std::uint32_t>("height", config.height);
    adapter.readField<std::uint32_t>("frames", config.frames);
    adapter.readField<std::uint32_t>("fps", config.fps);
    adapter.readField<std::string>("output", output);
    adapter.readField<std::string>("debug_view", config.debug_view);
    adapter.readField<bool>("headless", config.headless);
    adapter.readField<bool>("validation", config.validation);
    adapter.readField<bool>("require_validation", config.require_validation);
    adapter.readField<bool>("print_frame_stats", config.print_frame_stats);
    adapter.readField<std::string>("capture", capture);
    adapter.readField<RunConfig::GltfOptions>("gltf", config.gltf);
    adapter.readField<RunConfig::PbrOptions>("pbr", config.pbr);
    adapter.readField<RunConfig::OceanOptions>("ocean", config.ocean);
    adapter.readField<RunConfig::PlanetOptions>("planet", config.planet);
    adapter.readField<RunConfig::TerrainOptions>("terrain", config.terrain);
    adapter.readField<RunConfig::TerrainLabOptions>("terrain_lab", config.terrain_lab);
    adapter.readField<RunConfig::AtmosphereOptions>("atmosphere", config.atmosphere);
    adapter.readField<RunConfig::CloudOptions>("clouds", config.clouds);

    if (!output.empty()) {
        config.output_path = output;
    }
    if (capture == "png") {
        config.capture_mode = cubey::CaptureMode::Png;
    } else if (capture == "video") {
        config.capture_mode = cubey::CaptureMode::Video;
    }
}

} // namespace lazy::serializable

namespace cubey {

std::span<const ConfigOptionDescriptor> run_config_option_descriptors() {
    return kRunConfigOptions;
}

const ConfigOptionDescriptor* find_run_config_option(std::string_view path) {
    const auto it =
        std::find_if(kRunConfigOptions.begin(), kRunConfigOptions.end(),
                     [path](const ConfigOptionDescriptor& option) { return option.path == path; });
    return it == kRunConfigOptions.end() ? nullptr : &*it;
}

const ConfigOptionDescriptor* find_run_config_option_by_cli_name(std::string_view cli_name) {
    const auto it = std::find_if(kRunConfigOptions.begin(), kRunConfigOptions.end(),
                                 [cli_name](const ConfigOptionDescriptor& option) {
                                     return option.cli_name == cli_name ||
                                            (!option.negative_cli_name.empty() &&
                                             option.negative_cli_name == cli_name);
                                 });
    return it == kRunConfigOptions.end() ? nullptr : &*it;
}

bool config_option_has_choice(const ConfigOptionDescriptor& option, std::string_view value) {
    if (option.enum_choices.values == nullptr || option.enum_choices.count == 0U) {
        return true;
    }
    return std::find(option.enum_choices.values,
                     option.enum_choices.values + option.enum_choices.count,
                     value) != option.enum_choices.values + option.enum_choices.count;
}

void set_run_config_option_from_string(RunConfig& config, const ConfigOptionDescriptor& option,
                                       std::string_view value) {
    if (option.type == ConfigOptionType::Enum && !config_option_has_choice(option, value)) {
        throw std::runtime_error("invalid enum value for " + std::string(option.path));
    }

    switch (option.id) {
    case RunConfigOptionId::Title:
        config.title = std::string(value);
        break;
    case RunConfigOptionId::Width:
        config.width = parse_number<std::uint32_t>(value, option, "unsigned integer");
        validate_range(config.width, option);
        break;
    case RunConfigOptionId::Height:
        config.height = parse_number<std::uint32_t>(value, option, "unsigned integer");
        validate_range(config.height, option);
        break;
    case RunConfigOptionId::Frames:
        config.frames = parse_number<std::uint32_t>(value, option, "unsigned integer");
        break;
    case RunConfigOptionId::Fps:
        config.fps = parse_number<std::uint32_t>(value, option, "unsigned integer");
        validate_range(config.fps, option);
        break;
    case RunConfigOptionId::OutputPath:
        config.output_path = std::string(value);
        break;
    case RunConfigOptionId::DebugView:
        config.debug_view = std::string(value);
        break;
    case RunConfigOptionId::Headless:
        config.headless = parse_config_bool(value, option);
        break;
    case RunConfigOptionId::Validation:
        config.validation = parse_config_bool(value, option);
        if (!config.validation) {
            config.require_validation = false;
        }
        break;
    case RunConfigOptionId::RequireValidation:
        config.require_validation = parse_config_bool(value, option);
        if (config.require_validation) {
            config.validation = true;
        }
        break;
    case RunConfigOptionId::PrintFrameStats:
        config.print_frame_stats = parse_config_bool(value, option);
        break;
    case RunConfigOptionId::CaptureMode:
        config.capture_mode = value == "video" ? CaptureMode::Video : CaptureMode::Png;
        break;
    case RunConfigOptionId::GridWidth:
        config.grid.width = parse_number<std::uint32_t>(value, option, "unsigned integer");
        validate_range(config.grid.width, option);
        break;
    case RunConfigOptionId::GridHeight:
        config.grid.height = parse_number<std::uint32_t>(value, option, "unsigned integer");
        validate_range(config.grid.height, option);
        break;
    case RunConfigOptionId::GridDepth:
        config.grid.depth = parse_number<std::uint32_t>(value, option, "unsigned integer");
        validate_range(config.grid.depth, option);
        break;
    case RunConfigOptionId::ProfileOutput:
        config.profile_output_prefix = profile_output_prefix(value, option);
        break;
    case RunConfigOptionId::ProfileWarmupFrames:
        config.profile_warmup_frames =
            parse_number<std::uint32_t>(value, option, "unsigned integer");
        break;
    case RunConfigOptionId::ProfileDiagnostics:
        config.profile_diagnostics = parse_config_bool(value, option);
        break;
    case RunConfigOptionId::ProfileDiagnosticInterval:
        config.profile_diagnostic_interval =
            parse_number<std::uint32_t>(value, option, "unsigned integer");
        validate_range(config.profile_diagnostic_interval, option);
        break;
    case RunConfigOptionId::GltfInput:
        config.gltf.input_path = std::string(value);
        break;
    case RunConfigOptionId::GltfAnimationIndex:
        config.gltf.animation_index =
            parse_number<std::uint32_t>(value, option, "unsigned integer");
        break;
    case RunConfigOptionId::GltfAnimationSpeed:
        config.gltf.animation_speed = parse_config_float(value, option);
        break;
    case RunConfigOptionId::GltfAnimationPaused:
        config.gltf.animation_paused = parse_config_bool(value, option);
        break;
    case RunConfigOptionId::PbrEnvironment:
        config.pbr.environment_path = std::string(value);
        break;
    case RunConfigOptionId::PbrEnvironmentSource:
        config.pbr.environment_source = std::string(value);
        break;
    case RunConfigOptionId::PbrIblIntensity:
        config.pbr.ibl_intensity = parse_config_float(value, option);
        validate_range(config.pbr.ibl_intensity, option);
        break;
    case RunConfigOptionId::PbrEnvironmentRotation:
        config.pbr.environment_rotation_degrees = parse_config_float(value, option);
        break;
    case RunConfigOptionId::PbrExposure:
        config.pbr.exposure = parse_config_float(value, option);
        validate_range(config.pbr.exposure, option);
        config.pbr.exposure_explicit = true;
        break;
    case RunConfigOptionId::OceanMapSize:
        config.ocean.map_size = parse_number<std::uint32_t>(value, option, "unsigned integer");
        break;
    case RunConfigOptionId::OceanFieldPrecision:
        config.ocean.field_precision = std::string(value);
        break;
    case RunConfigOptionId::OceanSurfaceMode:
        config.ocean.surface_mode = std::string(value);
        break;
    case RunConfigOptionId::OceanCameraPreset:
        config.ocean.camera_preset = std::string(value);
        break;
    case RunConfigOptionId::OceanPlanetRadiusScale:
        config.ocean.planet_radius_scale = parse_config_float(value, option);
        validate_range(config.ocean.planet_radius_scale, option);
        break;
    case RunConfigOptionId::OceanCurvatureStartRatio:
        config.ocean.curvature_start_ratio = parse_config_float(value, option);
        validate_range(config.ocean.curvature_start_ratio, option);
        break;
    case RunConfigOptionId::OceanCurvatureEndRatio:
        config.ocean.curvature_end_ratio = parse_config_float(value, option);
        validate_range(config.ocean.curvature_end_ratio, option);
        break;
    case RunConfigOptionId::OceanCurvatureStrength:
        config.ocean.curvature_strength = parse_config_float(value, option);
        validate_range(config.ocean.curvature_strength, option);
        break;
    case RunConfigOptionId::OceanCascade:
        config.ocean.cascade = parse_ocean_cascade(value, option);
        break;
    case RunConfigOptionId::OceanSpectralDomains:
        config.ocean.spectral_domains = parse_config_bool(value, option) ? 1 : 0;
        break;
    case RunConfigOptionId::OceanTerrainFields:
        config.ocean.terrain_fields = parse_config_bool(value, option) ? 1 : 0;
        break;
    case RunConfigOptionId::OceanWireOverlay:
        config.ocean.wire_overlay = parse_config_bool(value, option);
        break;
    case RunConfigOptionId::OceanWireOpacity:
        config.ocean.wire_opacity = parse_config_float(value, option);
        validate_range(config.ocean.wire_opacity, option);
        break;
    case RunConfigOptionId::PlanetScalePreset:
        config.planet.scale_preset = std::string(value);
        break;
    case RunConfigOptionId::PlanetRadius:
        config.planet.radius_m = parse_config_float(value, option);
        validate_range(config.planet.radius_m, option);
        break;
    case RunConfigOptionId::PlanetAtmosphereHeight:
        config.planet.atmosphere_height_m = parse_config_float(value, option);
        validate_range(config.planet.atmosphere_height_m, option);
        break;
    case RunConfigOptionId::PlanetCameraAltitude:
        config.planet.camera_altitude_m = parse_config_float(value, option);
        validate_range(config.planet.camera_altitude_m, option);
        break;
    case RunConfigOptionId::PlanetCameraOrbitSpin:
        config.planet.camera_orbit_spin_degrees_per_second = parse_config_float(value, option);
        validate_range(config.planet.camera_orbit_spin_degrees_per_second, option);
        break;
    case RunConfigOptionId::PlanetCameraSurfacePitch:
        config.planet.camera_surface_pitch_degrees = parse_config_float(value, option);
        validate_range(config.planet.camera_surface_pitch_degrees, option);
        break;
    case RunConfigOptionId::PlanetCameraSurfaceYaw:
        config.planet.camera_surface_yaw_degrees = parse_config_float(value, option);
        validate_range(config.planet.camera_surface_yaw_degrees, option);
        break;
    case RunConfigOptionId::PlanetCameraSurfaceLook:
        config.planet.camera_surface_look = std::string(value);
        break;
    case RunConfigOptionId::PlanetPatchesPerFace:
        config.planet.patches_per_face =
            parse_number<std::uint32_t>(value, option, "unsigned integer");
        validate_range(config.planet.patches_per_face, option);
        break;
    case RunConfigOptionId::PlanetPatchResolution:
        config.planet.patch_resolution =
            parse_number<std::uint32_t>(value, option, "unsigned integer");
        validate_range(config.planet.patch_resolution, option);
        break;
    case RunConfigOptionId::PlanetMaxLodLevel:
        config.planet.max_lod_level =
            parse_number<std::uint32_t>(value, option, "unsigned integer");
        validate_range(config.planet.max_lod_level, option);
        config.planet.max_lod_level_set = true;
        break;
    case RunConfigOptionId::PlanetLodTargetEdge:
        config.planet.lod_target_edge_px = parse_config_float(value, option);
        validate_range(config.planet.lod_target_edge_px, option);
        break;
    case RunConfigOptionId::PlanetLodHysteresis:
        config.planet.lod_hysteresis = parse_config_float(value, option);
        validate_range(config.planet.lod_hysteresis, option);
        break;
    case RunConfigOptionId::PlanetLocalDetailLodLevels:
        config.planet.local_detail_lod_levels =
            parse_number<std::uint32_t>(value, option, "unsigned integer");
        validate_range(config.planet.local_detail_lod_levels, option);
        break;
    case RunConfigOptionId::PlanetLocalDetailCells:
        config.planet.local_detail_cells_per_axis =
            parse_number<std::uint32_t>(value, option, "unsigned integer");
        validate_range(config.planet.local_detail_cells_per_axis, option);
        break;
    case RunConfigOptionId::PlanetLocalDetailOuterExtent:
        config.planet.local_detail_outer_half_extent_m = parse_config_float(value, option);
        validate_range(config.planet.local_detail_outer_half_extent_m, option);
        break;
    case RunConfigOptionId::PlanetLocalDetail:
        config.planet.local_detail_enabled = parse_config_bool(value, option) ? 1 : 0;
        break;
    case RunConfigOptionId::PlanetLocalDetailHeight:
        config.planet.local_detail_height_strength_m = parse_config_float(value, option);
        validate_range(config.planet.local_detail_height_strength_m, option);
        break;
    case RunConfigOptionId::PlanetLocalDetailScale:
        config.planet.local_detail_scale_m = parse_config_float(value, option);
        validate_range(config.planet.local_detail_scale_m, option);
        break;
    case RunConfigOptionId::PlanetWireOverlay:
        config.planet.wire_overlay = parse_config_bool(value, option) ? 1 : 0;
        break;
    case RunConfigOptionId::PlanetSkirts:
        config.planet.skirts_enabled = parse_config_bool(value, option) ? 1 : 0;
        break;
    case RunConfigOptionId::PlanetSkirtDepthScale:
        config.planet.skirt_depth_scale = parse_config_float(value, option);
        validate_range(config.planet.skirt_depth_scale, option);
        break;
    case RunConfigOptionId::PlanetTerrain:
        config.planet.terrain_enabled = parse_config_bool(value, option) ? 1 : 0;
        break;
    case RunConfigOptionId::PlanetTerrainHeightScale:
        config.planet.terrain_height_scale_m = parse_config_float(value, option);
        validate_range(config.planet.terrain_height_scale_m, option);
        break;
    case RunConfigOptionId::PlanetTerrainNoiseScale:
        config.planet.terrain_noise_scale = parse_config_float(value, option);
        validate_range(config.planet.terrain_noise_scale, option);
        break;
    case RunConfigOptionId::PlanetTerrainMidDetailStrength:
        config.planet.terrain_mid_detail_strength = parse_config_float(value, option);
        validate_range(config.planet.terrain_mid_detail_strength, option);
        break;
    case RunConfigOptionId::PlanetTerrainFineDetailStrength:
        config.planet.terrain_fine_detail_strength = parse_config_float(value, option);
        validate_range(config.planet.terrain_fine_detail_strength, option);
        break;
    case RunConfigOptionId::PlanetTerrainFineDetailScale:
        config.planet.terrain_fine_detail_scale = parse_config_float(value, option);
        validate_range(config.planet.terrain_fine_detail_scale, option);
        break;
    case RunConfigOptionId::PlanetTerrainSeed:
        config.planet.terrain_seed = parse_number<std::uint32_t>(value, option, "unsigned integer");
        config.planet.terrain_seed_set = true;
        break;
    case RunConfigOptionId::PlanetSeaLevel:
        config.planet.sea_level_m = parse_config_float(value, option);
        validate_range(config.planet.sea_level_m, option);
        break;
    case RunConfigOptionId::PlanetBathymetryDepthScale:
        config.planet.bathymetry_depth_scale_m = parse_config_float(value, option);
        validate_range(config.planet.bathymetry_depth_scale_m, option);
        break;
    case RunConfigOptionId::PlanetShorelineWidth:
        config.planet.shoreline_width_m = parse_config_float(value, option);
        validate_range(config.planet.shoreline_width_m, option);
        break;
    case RunConfigOptionId::PlanetAtmosphereHazeStrength:
        config.planet.atmosphere_haze_strength = parse_config_float(value, option);
        validate_range(config.planet.atmosphere_haze_strength, option);
        break;
    case RunConfigOptionId::PlanetAtmosphereHazeStart:
        config.planet.atmosphere_haze_start = parse_config_float(value, option);
        validate_range(config.planet.atmosphere_haze_start, option);
        break;
    case RunConfigOptionId::PlanetAtmosphereHazeEnd:
        config.planet.atmosphere_haze_end = parse_config_float(value, option);
        validate_range(config.planet.atmosphere_haze_end, option);
        break;
    case RunConfigOptionId::PlanetAtmosphereAerialStrength:
        config.planet.atmosphere_aerial_strength = parse_config_float(value, option);
        validate_range(config.planet.atmosphere_aerial_strength, option);
        break;
    case RunConfigOptionId::PlanetDayOfYear:
        config.planet.day_of_year = parse_config_float(value, option);
        validate_range(config.planet.day_of_year, option);
        break;
    case RunConfigOptionId::PlanetTimeHours:
        config.planet.time_hours = parse_config_float(value, option);
        validate_range(config.planet.time_hours, option);
        break;
    case RunConfigOptionId::PlanetTimeSpeed:
        config.planet.time_speed_hours_per_second = parse_config_float(value, option);
        validate_range(config.planet.time_speed_hours_per_second, option);
        break;
    case RunConfigOptionId::PlanetTimePaused:
        config.planet.time_paused = parse_config_bool(value, option) ? 1 : 0;
        break;
    case RunConfigOptionId::PlanetCameraMode:
        config.planet.camera_mode = std::string(value);
        break;
    case RunConfigOptionId::PlanetAtmosphereMode:
        config.planet.atmosphere_mode = std::string(value);
        break;
    case RunConfigOptionId::TerrainSeed:
        config.terrain.seed = parse_number<std::uint64_t>(value, option, "unsigned integer");
        config.terrain.seed_set = true;
        break;
    case RunConfigOptionId::TerrainCellSize:
        config.terrain.cell_size = parse_config_float(value, option);
        validate_range(config.terrain.cell_size, option);
        break;
    case RunConfigOptionId::TerrainSeaLevel:
        config.terrain.sea_level = parse_config_float(value, option);
        break;
    case RunConfigOptionId::TerrainLandExtent:
        config.terrain.land_extent = parse_config_float(value, option);
        break;
    case RunConfigOptionId::TerrainCoastNoise:
        config.terrain.coast_noise = parse_config_float(value, option);
        validate_range(config.terrain.coast_noise, option);
        break;
    case RunConfigOptionId::TerrainRelief:
        config.terrain.relief = parse_config_float(value, option);
        validate_range(config.terrain.relief, option);
        break;
    case RunConfigOptionId::TerrainRidges:
        config.terrain.ridges = parse_config_float(value, option);
        validate_range(config.terrain.ridges, option);
        break;
    case RunConfigOptionId::TerrainValleys:
        config.terrain.valleys = parse_config_float(value, option);
        validate_range(config.terrain.valleys, option);
        break;
    case RunConfigOptionId::TerrainWaterSurface:
        config.terrain.water_surface = parse_config_bool(value, option) ? 1 : 0;
        break;
    case RunConfigOptionId::TerrainLabSlicePreset:
        config.terrain_lab.slice_preset = std::string(value);
        break;
    case RunConfigOptionId::TerrainLabCameraPreset:
        config.terrain_lab.camera_preset = std::string(value);
        break;
    case RunConfigOptionId::TerrainLabNoiseSource:
        config.terrain_lab.noise_source = std::string(value);
        break;
    case RunConfigOptionId::AtmospherePreset:
        config.atmosphere.preset = std::string(value);
        break;
    case RunConfigOptionId::AtmosphereTimeOfDayMode:
        config.atmosphere.time_of_day_mode = std::string(value);
        break;
    case RunConfigOptionId::AtmosphereNightSkyMode:
        config.atmosphere.night_sky_mode = std::string(value);
        break;
    case RunConfigOptionId::AtmosphereMilkyWayLayer:
        config.atmosphere.milky_way_layer = std::string(value);
        break;
    case RunConfigOptionId::AtmosphereSunElevation:
        config.atmosphere.sun_elevation_degrees = parse_config_float(value, option);
        validate_range(config.atmosphere.sun_elevation_degrees, option);
        break;
    case RunConfigOptionId::AtmosphereSunAzimuth:
        config.atmosphere.sun_azimuth_degrees = parse_config_float(value, option);
        validate_range(config.atmosphere.sun_azimuth_degrees, option);
        break;
    case RunConfigOptionId::AtmosphereCameraAltitude:
        config.atmosphere.camera_altitude_km = parse_config_float(value, option);
        validate_range(config.atmosphere.camera_altitude_km, option);
        break;
    case RunConfigOptionId::AtmosphereCameraYawOffset:
        config.atmosphere.camera_yaw_offset_degrees = parse_config_float(value, option);
        validate_range(config.atmosphere.camera_yaw_offset_degrees, option);
        break;
    case RunConfigOptionId::AtmosphereCameraPitchOffset:
        config.atmosphere.camera_pitch_offset_degrees = parse_config_float(value, option);
        validate_range(config.atmosphere.camera_pitch_offset_degrees, option);
        break;
    case RunConfigOptionId::AtmosphereRayleighScale:
        config.atmosphere.rayleigh_scale = parse_config_float(value, option);
        validate_range(config.atmosphere.rayleigh_scale, option);
        break;
    case RunConfigOptionId::AtmosphereMieScale:
        config.atmosphere.mie_scale = parse_config_float(value, option);
        validate_range(config.atmosphere.mie_scale, option);
        break;
    case RunConfigOptionId::AtmosphereOzoneScale:
        config.atmosphere.ozone_scale = parse_config_float(value, option);
        validate_range(config.atmosphere.ozone_scale, option);
        break;
    case RunConfigOptionId::AtmosphereTimeHours:
        config.atmosphere.time_hours = parse_config_float(value, option);
        validate_range(config.atmosphere.time_hours, option);
        break;
    case RunConfigOptionId::AtmosphereDayOfYear:
        config.atmosphere.day_of_year = parse_config_float(value, option);
        validate_range(config.atmosphere.day_of_year, option);
        break;
    case RunConfigOptionId::AtmosphereLatitude:
        config.atmosphere.latitude_degrees = parse_config_float(value, option);
        validate_range(config.atmosphere.latitude_degrees, option);
        break;
    case RunConfigOptionId::AtmosphereSunAzimuthOffset:
        config.atmosphere.sun_azimuth_offset_degrees = parse_config_float(value, option);
        validate_range(config.atmosphere.sun_azimuth_offset_degrees, option);
        break;
    case RunConfigOptionId::AtmosphereTimeSpeed:
        config.atmosphere.time_speed_hours_per_second = parse_config_float(value, option);
        validate_range(config.atmosphere.time_speed_hours_per_second, option);
        break;
    case RunConfigOptionId::AtmosphereTimePaused:
        config.atmosphere.time_paused = parse_config_bool(value, option) ? 1 : 0;
        break;
    case RunConfigOptionId::AtmosphereAutoExposure:
        config.atmosphere.auto_exposure = parse_config_bool(value, option) ? 1 : 0;
        break;
    case RunConfigOptionId::AtmosphereExposureBias:
        config.atmosphere.exposure_bias = parse_config_float(value, option);
        validate_range(config.atmosphere.exposure_bias, option);
        break;
    case RunConfigOptionId::AtmosphereTwilightStrength:
        config.atmosphere.twilight_strength = parse_config_float(value, option);
        validate_range(config.atmosphere.twilight_strength, option);
        break;
    case RunConfigOptionId::AtmosphereTwilightHorizonWarmth:
        config.atmosphere.twilight_horizon_warmth = parse_config_float(value, option);
        validate_range(config.atmosphere.twilight_horizon_warmth, option);
        break;
    case RunConfigOptionId::AtmosphereStarIntensity:
        config.atmosphere.star_intensity = parse_config_float(value, option);
        validate_range(config.atmosphere.star_intensity, option);
        break;
    case RunConfigOptionId::AtmosphereStarDensity:
        config.atmosphere.star_density = parse_config_float(value, option);
        validate_range(config.atmosphere.star_density, option);
        break;
    case RunConfigOptionId::AtmosphereMilkyWayIntensity:
        config.atmosphere.milky_way_intensity = parse_config_float(value, option);
        validate_range(config.atmosphere.milky_way_intensity, option);
        break;
    case RunConfigOptionId::AtmosphereMilkyWayContrast:
        config.atmosphere.milky_way_contrast = parse_config_float(value, option);
        validate_range(config.atmosphere.milky_way_contrast, option);
        break;
    case RunConfigOptionId::AtmosphereLightPollution:
        config.atmosphere.light_pollution = parse_config_float(value, option);
        validate_range(config.atmosphere.light_pollution, option);
        break;
    case RunConfigOptionId::AtmosphereMilkyWayVariation:
        config.atmosphere.milky_way_variation = parse_config_float(value, option);
        validate_range(config.atmosphere.milky_way_variation, option);
        break;
    case RunConfigOptionId::AtmosphereMoonIntensity:
        config.atmosphere.moon_intensity = parse_config_float(value, option);
        validate_range(config.atmosphere.moon_intensity, option);
        break;
    case RunConfigOptionId::AtmosphereMoonlightIntensity:
        config.atmosphere.moonlight_intensity = parse_config_float(value, option);
        validate_range(config.atmosphere.moonlight_intensity, option);
        break;
    case RunConfigOptionId::AtmosphereMoonPhaseOffset:
        config.atmosphere.moon_phase_offset_days = parse_config_float(value, option);
        validate_range(config.atmosphere.moon_phase_offset_days, option);
        break;
    case RunConfigOptionId::AtmosphereMoonSizeScale:
        config.atmosphere.moon_size_scale = parse_config_float(value, option);
        validate_range(config.atmosphere.moon_size_scale, option);
        break;
    case RunConfigOptionId::AtmosphereMoon:
        config.atmosphere.moon = parse_config_bool(value, option) ? 1 : 0;
        break;
    case RunConfigOptionId::AtmosphereReferenceGeometry:
        config.atmosphere.reference_geometry = parse_config_bool(value, option) ? 1 : 0;
        break;
    case RunConfigOptionId::CloudEnabled:
        config.clouds.enabled = parse_config_bool(value, option) ? 1 : 0;
        break;
    case RunConfigOptionId::CloudDebugView:
        config.clouds.debug_view = std::string(value);
        break;
    case RunConfigOptionId::CloudCameraMode:
        config.clouds.camera_mode = std::string(value);
        break;
    case RunConfigOptionId::CloudQuality:
        config.clouds.quality = std::string(value);
        break;
    case RunConfigOptionId::CloudWeatherPreset:
        config.clouds.weather_preset = std::string(value);
        break;
    case RunConfigOptionId::CloudCacheFrames:
        config.clouds.cache_frames = std::string(value);
        break;
    case RunConfigOptionId::CloudCacheTextureSize:
        config.clouds.cache_texture_size =
            parse_number<std::uint32_t>(value, option, "unsigned integer");
        validate_range(config.clouds.cache_texture_size, option);
        break;
    case RunConfigOptionId::CloudRenderPath:
        config.clouds.render_path = std::string(value);
        break;
    case RunConfigOptionId::CloudSamplingMode:
        config.clouds.sampling_mode = std::string(value);
        break;
    case RunConfigOptionId::CloudDensityModel:
        config.clouds.density_model = std::string(value);
        break;
    case RunConfigOptionId::CloudResolveMode:
        config.clouds.resolve_mode = std::string(value);
        break;
    case RunConfigOptionId::CloudBackgroundMode:
        config.clouds.background_mode = std::string(value);
        break;
    case RunConfigOptionId::CloudDistanceMode:
        config.clouds.distance_mode = std::string(value);
        break;
    case RunConfigOptionId::CloudOrbitRepresentation:
        config.clouds.orbit_representation = std::string(value);
        break;
    case RunConfigOptionId::CloudPlanetRadius:
        config.clouds.planet_radius_m = parse_config_float(value, option);
        validate_range(config.clouds.planet_radius_m, option);
        break;
    case RunConfigOptionId::CloudCameraAltitude:
        config.clouds.camera_altitude_m = parse_config_float(value, option);
        validate_range(config.clouds.camera_altitude_m, option);
        break;
    case RunConfigOptionId::CloudBottomAltitude:
        config.clouds.bottom_altitude_m = parse_config_float(value, option);
        validate_range(config.clouds.bottom_altitude_m, option);
        break;
    case RunConfigOptionId::CloudTopAltitude:
        config.clouds.top_altitude_m = parse_config_float(value, option);
        validate_range(config.clouds.top_altitude_m, option);
        break;
    case RunConfigOptionId::CloudCoverage:
        config.clouds.coverage = parse_config_float(value, option);
        validate_range(config.clouds.coverage, option);
        break;
    case RunConfigOptionId::CloudDensity:
        config.clouds.density = parse_config_float(value, option);
        validate_range(config.clouds.density, option);
        break;
    case RunConfigOptionId::CloudWeatherScale:
        config.clouds.weather_scale_km = parse_config_float(value, option);
        validate_range(config.clouds.weather_scale_km, option);
        break;
    case RunConfigOptionId::CloudShapeDomain:
        config.clouds.shape_domain_km = parse_config_float(value, option);
        validate_range(config.clouds.shape_domain_km, option);
        break;
    case RunConfigOptionId::CloudFootprintFilterStrength:
        config.clouds.footprint_filter_strength = parse_config_float(value, option);
        validate_range(config.clouds.footprint_filter_strength, option);
        break;
    case RunConfigOptionId::CloudEdgeSoftness:
        config.clouds.edge_softness = parse_config_float(value, option);
        validate_range(config.clouds.edge_softness, option);
        break;
    case RunConfigOptionId::CloudEdgeDetailFade:
        config.clouds.edge_detail_fade = parse_config_float(value, option);
        validate_range(config.clouds.edge_detail_fade, option);
        break;
    case RunConfigOptionId::CloudEdgeResolveStrength:
        config.clouds.edge_resolve_strength = parse_config_float(value, option);
        validate_range(config.clouds.edge_resolve_strength, option);
        break;
    case RunConfigOptionId::CloudVerticalShearFraction:
        config.clouds.vertical_shear_fraction = parse_config_float(value, option);
        validate_range(config.clouds.vertical_shear_fraction, option);
        break;
    case RunConfigOptionId::CloudWindSpeed:
        config.clouds.wind_speed_mps = parse_config_float(value, option);
        validate_range(config.clouds.wind_speed_mps, option);
        break;
    case RunConfigOptionId::CloudShadowStrength:
        config.clouds.shadow_strength = parse_config_float(value, option);
        validate_range(config.clouds.shadow_strength, option);
        break;
    case RunConfigOptionId::CloudHorizonStrength:
        config.clouds.horizon_strength = parse_config_float(value, option);
        validate_range(config.clouds.horizon_strength, option);
        break;
    case RunConfigOptionId::CloudWeatherFronts:
        config.clouds.weather_fronts = parse_config_float(value, option);
        validate_range(config.clouds.weather_fronts, option);
        break;
    case RunConfigOptionId::CloudWeatherCells:
        config.clouds.weather_cells = parse_config_float(value, option);
        validate_range(config.clouds.weather_cells, option);
        break;
    case RunConfigOptionId::CloudWeatherStreaks:
        config.clouds.weather_streaks = parse_config_float(value, option);
        validate_range(config.clouds.weather_streaks, option);
        break;
    case RunConfigOptionId::CloudWeatherSoftness:
        config.clouds.weather_softness = parse_config_float(value, option);
        validate_range(config.clouds.weather_softness, option);
        break;
    case RunConfigOptionId::CloudWeatherInfluence:
        config.clouds.weather_influence = parse_config_float(value, option);
        validate_range(config.clouds.weather_influence, option);
        break;
    case RunConfigOptionId::CloudDetailErosion:
        config.clouds.detail_erosion = parse_config_float(value, option);
        validate_range(config.clouds.detail_erosion, option);
        break;
    case RunConfigOptionId::CloudAmbientStrength:
        config.clouds.ambient_strength = parse_config_float(value, option);
        validate_range(config.clouds.ambient_strength, option);
        break;
    case RunConfigOptionId::CloudDirectStrength:
        config.clouds.direct_strength = parse_config_float(value, option);
        validate_range(config.clouds.direct_strength, option);
        break;
    case RunConfigOptionId::CloudPhaseStrength:
        config.clouds.phase_strength = parse_config_float(value, option);
        validate_range(config.clouds.phase_strength, option);
        break;
    case RunConfigOptionId::CloudFinalContrast:
        config.clouds.final_contrast = parse_config_float(value, option);
        validate_range(config.clouds.final_contrast, option);
        break;
    case RunConfigOptionId::CloudFinalSaturation:
        config.clouds.final_saturation = parse_config_float(value, option);
        validate_range(config.clouds.final_saturation, option);
        break;
    case RunConfigOptionId::CloudResolveStrength:
        config.clouds.resolve_strength = parse_config_float(value, option);
        validate_range(config.clouds.resolve_strength, option);
        break;
    case RunConfigOptionId::CloudResolveRadius:
        config.clouds.resolve_radius_px = parse_config_float(value, option);
        validate_range(config.clouds.resolve_radius_px, option);
        break;
    case RunConfigOptionId::CloudHorizonGlowStrength:
        config.clouds.horizon_glow_strength = parse_config_float(value, option);
        validate_range(config.clouds.horizon_glow_strength, option);
        break;
    case RunConfigOptionId::CloudSunGlareStrength:
        config.clouds.sun_glare_strength = parse_config_float(value, option);
        validate_range(config.clouds.sun_glare_strength, option);
        break;
    case RunConfigOptionId::CloudJitterStrength:
        config.clouds.jitter_strength = parse_config_float(value, option);
        validate_range(config.clouds.jitter_strength, option);
        break;
    case RunConfigOptionId::CloudOrbitTransitionStart:
        config.clouds.orbit_transition_start_m = parse_config_float(value, option);
        validate_range(config.clouds.orbit_transition_start_m, option);
        break;
    case RunConfigOptionId::CloudOrbitTransitionEnd:
        config.clouds.orbit_transition_end_m = parse_config_float(value, option);
        validate_range(config.clouds.orbit_transition_end_m, option);
        break;
    case RunConfigOptionId::CloudFarShellStart:
        config.clouds.far_shell_start_m = parse_config_float(value, option);
        validate_range(config.clouds.far_shell_start_m, option);
        break;
    case RunConfigOptionId::CloudFarShellEnd:
        config.clouds.far_shell_end_m = parse_config_float(value, option);
        validate_range(config.clouds.far_shell_end_m, option);
        break;
    case RunConfigOptionId::CloudFarShellStrength:
        config.clouds.far_shell_strength = parse_config_float(value, option);
        validate_range(config.clouds.far_shell_strength, option);
        break;
    case RunConfigOptionId::CloudOrbitDetailStrength:
        config.clouds.orbit_detail_strength = parse_config_float(value, option);
        validate_range(config.clouds.orbit_detail_strength, option);
        break;
    case RunConfigOptionId::CloudOrbitDensityScale:
        config.clouds.orbit_density_scale = parse_config_float(value, option);
        validate_range(config.clouds.orbit_density_scale, option);
        break;
    case RunConfigOptionId::CloudOrbitFill:
        config.clouds.orbit_fill = parse_config_float(value, option);
        validate_range(config.clouds.orbit_fill, option);
        break;
    case RunConfigOptionId::CloudOrbitMotionStrength:
        config.clouds.orbit_motion_strength = parse_config_float(value, option);
        validate_range(config.clouds.orbit_motion_strength, option);
        break;
    case RunConfigOptionId::CloudOrbitShellExtinction:
        config.clouds.orbit_shell_extinction = parse_config_float(value, option);
        validate_range(config.clouds.orbit_shell_extinction, option);
        break;
    case RunConfigOptionId::CloudTemporal:
        config.clouds.temporal = parse_config_bool(value, option) ? 1 : 0;
        break;
    case RunConfigOptionId::CloudLocalVolume:
        config.clouds.local_volume = parse_config_bool(value, option) ? 1 : 0;
        break;
    case RunConfigOptionId::CloudHorizonLayer:
        config.clouds.horizon_layer = parse_config_bool(value, option) ? 1 : 0;
        break;
    case RunConfigOptionId::SmokeInjectors:
        config.smoke.injectors = parse_number<std::uint32_t>(value, option, "unsigned integer");
        validate_range(config.smoke.injectors, option);
        break;
    case RunConfigOptionId::SmokePressureIterations:
        config.smoke.pressure_iterations =
            parse_number<std::uint32_t>(value, option, "unsigned integer");
        validate_range(config.smoke.pressure_iterations, option);
        break;
    case RunConfigOptionId::SmokePressureSolver:
        config.smoke.pressure_solver = std::string(value);
        break;
    case RunConfigOptionId::SmokeDyeDecay:
        config.smoke.dye_decay = parse_config_float(value, option);
        validate_range(config.smoke.dye_decay, option);
        break;
    case RunConfigOptionId::SmokeVelocityDecay:
        config.smoke.velocity_decay = parse_config_float(value, option);
        validate_range(config.smoke.velocity_decay, option);
        break;
    case RunConfigOptionId::SmokeInjectorRadius:
        config.smoke.injector_radius = parse_config_float(value, option);
        validate_range(config.smoke.injector_radius, option);
        break;
    case RunConfigOptionId::SmokeInjectorForce:
        config.smoke.injector_force = parse_config_float(value, option);
        validate_range(config.smoke.injector_force, option);
        break;
    case RunConfigOptionId::SmokeInjectorPropulsion:
        config.smoke.injector_propulsion = parse_config_float(value, option);
        validate_range(config.smoke.injector_propulsion, option);
        break;
    case RunConfigOptionId::SmokeInjectorOrbitRadius:
        config.smoke.injector_orbit_radius = parse_config_float(value, option);
        validate_range(config.smoke.injector_orbit_radius, option);
        break;
    case RunConfigOptionId::SmokeInjectorOrbitRadiusSpread:
        config.smoke.injector_orbit_radius_spread = parse_config_float(value, option);
        validate_range(config.smoke.injector_orbit_radius_spread, option);
        break;
    case RunConfigOptionId::SmokeInjectorOrbitAngularSpeed:
        config.smoke.injector_orbit_angular_speed = parse_config_float(value, option);
        break;
    case RunConfigOptionId::SmokeInjectorOrbitAngularSpeedSpread:
        config.smoke.injector_orbit_angular_speed_spread = parse_config_float(value, option);
        validate_range(config.smoke.injector_orbit_angular_speed_spread, option);
        break;
    case RunConfigOptionId::SmokeInjectorOrbitPhaseSpread:
        config.smoke.injector_orbit_phase_spread = parse_config_float(value, option);
        validate_range(config.smoke.injector_orbit_phase_spread, option);
        break;
    case RunConfigOptionId::SmokeVorticity:
        config.smoke.vorticity = parse_config_float(value, option);
        validate_range(config.smoke.vorticity, option);
        break;
    case RunConfigOptionId::PyroShadowGridWidth:
        config.pyro.shadow_grid.width =
            parse_number<std::uint32_t>(value, option, "unsigned integer");
        validate_range(config.pyro.shadow_grid.width, option);
        break;
    case RunConfigOptionId::PyroShadowGridHeight:
        config.pyro.shadow_grid.height =
            parse_number<std::uint32_t>(value, option, "unsigned integer");
        validate_range(config.pyro.shadow_grid.height, option);
        break;
    case RunConfigOptionId::PyroShadowGridDepth:
        config.pyro.shadow_grid.depth =
            parse_number<std::uint32_t>(value, option, "unsigned integer");
        validate_range(config.pyro.shadow_grid.depth, option);
        break;
    case RunConfigOptionId::PyroShadowSteps:
        config.pyro.shadow_steps = parse_number<std::uint32_t>(value, option, "unsigned integer");
        validate_range(config.pyro.shadow_steps, option);
        break;
    case RunConfigOptionId::PyroShadowUpdateInterval:
        config.pyro.shadow_update_interval =
            parse_number<std::uint32_t>(value, option, "unsigned integer");
        validate_range(config.pyro.shadow_update_interval, option);
        break;
    case RunConfigOptionId::PyroSources:
        config.pyro.sources = parse_number<std::uint32_t>(value, option, "unsigned integer");
        validate_range(config.pyro.sources, option);
        break;
    case RunConfigOptionId::PyroSourceHeight:
        config.pyro.source_height = parse_config_float(value, option);
        validate_range(config.pyro.source_height, option);
        break;
    case RunConfigOptionId::PyroSourceRadius:
        config.pyro.source_radius = parse_config_float(value, option);
        validate_range(config.pyro.source_radius, option);
        break;
    case RunConfigOptionId::PyroSourceForce:
        config.pyro.source_force = parse_config_float(value, option);
        validate_range(config.pyro.source_force, option);
        break;
    case RunConfigOptionId::PyroSoot:
        config.pyro.soot = parse_config_float(value, option);
        validate_range(config.pyro.soot, option);
        break;
    case RunConfigOptionId::PyroTemperature:
        config.pyro.temperature = parse_config_float(value, option);
        validate_range(config.pyro.temperature, option);
        break;
    case RunConfigOptionId::PyroFuel:
        config.pyro.fuel = parse_config_float(value, option);
        validate_range(config.pyro.fuel, option);
        break;
    case RunConfigOptionId::PyroBuoyancy:
        config.pyro.buoyancy = parse_config_float(value, option);
        validate_range(config.pyro.buoyancy, option);
        break;
    case RunConfigOptionId::PyroIgnitionTemperature:
        config.pyro.ignition_temperature = parse_config_float(value, option);
        validate_range(config.pyro.ignition_temperature, option);
        break;
    case RunConfigOptionId::PyroBurnRate:
        config.pyro.burn_rate = parse_config_float(value, option);
        validate_range(config.pyro.burn_rate, option);
        break;
    case RunConfigOptionId::PyroHeatOutput:
        config.pyro.heat_output = parse_config_float(value, option);
        validate_range(config.pyro.heat_output, option);
        break;
    case RunConfigOptionId::PyroSootYield:
        config.pyro.soot_yield = parse_config_float(value, option);
        validate_range(config.pyro.soot_yield, option);
        break;
    case RunConfigOptionId::PyroExpansion:
        config.pyro.expansion = parse_config_float(value, option);
        validate_range(config.pyro.expansion, option);
        break;
    case RunConfigOptionId::PyroFlameCooling:
        config.pyro.flame_cooling = parse_config_float(value, option);
        validate_range(config.pyro.flame_cooling, option);
        break;
    case RunConfigOptionId::PyroShredding:
        config.pyro.shredding = parse_config_float(value, option);
        validate_range(config.pyro.shredding, option);
        break;
    case RunConfigOptionId::PyroTurbulence:
        config.pyro.turbulence = parse_config_float(value, option);
        validate_range(config.pyro.turbulence, option);
        break;
    case RunConfigOptionId::PyroObstacleHeight:
        config.pyro.obstacle_height = parse_config_float(value, option);
        validate_range(config.pyro.obstacle_height, option);
        break;
    case RunConfigOptionId::PyroObstacleRadius:
        config.pyro.obstacle_radius = parse_config_float(value, option);
        validate_range(config.pyro.obstacle_radius, option);
        break;
    case RunConfigOptionId::PyroExplosionInterval:
        config.pyro.explosion_interval_seconds = parse_config_float(value, option);
        validate_range(config.pyro.explosion_interval_seconds, option);
        break;
    case RunConfigOptionId::PyroExplosionDuration:
        config.pyro.explosion_duration_seconds = parse_config_float(value, option);
        validate_range(config.pyro.explosion_duration_seconds, option);
        break;
    case RunConfigOptionId::PyroExplosionBoost:
        config.pyro.explosion_boost = parse_config_float(value, option);
        validate_range(config.pyro.explosion_boost, option);
        break;
    case RunConfigOptionId::Water2DTransfer:
        config.water2d.transfer_mode = std::string(value);
        break;
    case RunConfigOptionId::Water2DTransferLimit:
        config.water2d.transfer_limit =
            parse_number<std::uint32_t>(value, option, "unsigned integer");
        validate_range(config.water2d.transfer_limit, option);
        break;
    case RunConfigOptionId::Water2DHose:
        config.water2d.hose = parse_config_bool(value, option) ? 1 : 0;
        break;
    case RunConfigOptionId::Water2DDrain:
        config.water2d.drain = parse_config_bool(value, option) ? 1 : 0;
        break;
    case RunConfigOptionId::Water2DWave:
        config.water2d.wave = parse_config_bool(value, option) ? 1 : 0;
        break;
    case RunConfigOptionId::Water3DTransfer:
        config.water3d.transfer_mode = std::string(value);
        break;
    case RunConfigOptionId::Water3DTransferLimit:
        config.water3d.transfer_limit =
            parse_number<std::uint32_t>(value, option, "unsigned integer");
        validate_range(config.water3d.transfer_limit, option);
        break;
    case RunConfigOptionId::Water3DP2GMode:
        config.water3d.p2g_mode = std::string(value);
        break;
    case RunConfigOptionId::Water3DHose:
        config.water3d.hose = parse_config_bool(value, option) ? 1 : 0;
        break;
    case RunConfigOptionId::Water3DDrain:
        config.water3d.drain = parse_config_bool(value, option) ? 1 : 0;
        break;
    case RunConfigOptionId::Water3DRain:
        config.water3d.rain = parse_config_bool(value, option) ? 1 : 0;
        break;
    case RunConfigOptionId::Water3DWave:
        config.water3d.wave = parse_config_bool(value, option) ? 1 : 0;
        break;
    case RunConfigOptionId::Water3DWhitewater:
        config.water3d.whitewater = parse_config_bool(value, option) ? 1 : 0;
        break;
    }
}

void set_run_config_option_from_string(RunConfig& config, std::string_view path,
                                       std::string_view value) {
    const ConfigOptionDescriptor* option = find_run_config_option(path);
    if (option == nullptr) {
        throw std::runtime_error("unknown config option: " + std::string(path));
    }
    set_run_config_option_from_string(config, *option, value);
}

RunConfigFileApplyResult apply_run_config_file(RunConfig& config,
                                               const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("could not open config file: " + path.string());
    }

    nlohmann::json document;
    try {
        input >> document;
    } catch (const std::exception& error) {
        throw std::runtime_error("could not parse config file " + path.string() + ": " +
                                 error.what());
    }

    RunConfigFileApplyResult result{};
    validate_json_options(document, {}, result.output_path_set);

    std::stringstream serialized;
    serialized << document.dump();
    lazy::serializable::NlohmannJsonAdapter adapter =
        lazy::serializable::NlohmannJsonAdapter::fromStream(serialized);
    lazy::serializable::deserialize(adapter, config);
    apply_json_options(document, {}, config);

    config.config_path = path;
    return result;
}

void write_run_config_template(const RunConfig& config, const std::filesystem::path& path) {
    nlohmann::json document = nlohmann::json::object();
    for (const ConfigOptionDescriptor& option : run_config_option_descriptors()) {
        set_json_path(document, option.path, option_to_json(config, option));
    }

    std::ofstream output(path);
    if (!output) {
        throw std::runtime_error("could not write config template: " + path.string());
    }
    output << document.dump(2) << '\n';
}

} // namespace cubey
