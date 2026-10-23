#pragma once
#include "sdf_timeline.h"
#include <algorithm>
#include <cmath>
#include <cubey/host/configured_app.h>
#include <limits>

namespace cubey::projects::sdf {
struct SdfConfig {
    host::CommonRunConfig common;
    TimelineConfig timeline;
    double snapshot_time = -1;
    std::filesystem::path recipe_path, export_recipe_path;
    std::filesystem::path authored_path, export_authored_path;
    std::string workshop, workshop_material = "marble";
    std::filesystem::path material_recipe_path, export_material_recipe_path;
    bool workshop_cut_material = false, workshop_blend_materials = true;
    std::string scene = "sticks";
    std::string lighting = "atmosphere";
    std::string cutter_mode = "original";
    std::uint32_t seed = 42, count = 512, debug = 0, shadows = 1;
    std::uint32_t atlas_axis = 64;
    std::uint32_t supersample = 1;
    std::uint32_t glass_crossings = 4;
    std::uint32_t environment_extent = 128;
    bool clouds = false;
    bool cloud_backdrop = true;
    bool cloud_sky_only = true;
    std::string cloud_distance = "auto", cloud_quality = "half", cloud_resolve = "terrain-post";
    std::string cloud_debug = "final";
    std::uint32_t cloud_view_samples = 2;
    float cloud_coverage = .45F;
    double cloud_time = 0;
    float cutter_width = 0.003F;
    float sun_elevation = 35, sun_azimuth = -55, exposure = 0, roughness = 0.36F,
          shadow_softness = 0.04F;
    bool ao = false;
    bool clay = false;
    bool cutter_cue = true;
    bool procedural = true, bump = true, material_fast_path = true;
    bool ground = false, validate = false, validate_batches = false, occluder = false;
    bool validate_subtractions = false;
    bool validate_history = false;
    float yaw = 0.6F, pitch = 0.3F, distance = 1.35F;
    float target_y = 0;
};
inline config::Schema sdf_config_schema(SdfConfig& c) {
    const auto option = [](std::string name, std::string label, config::ValueType type) {
        config::OptionSpec spec;
        spec.path = name;
        spec.cli_name = "--" + name;
        spec.label = std::move(label);
        spec.group_path = "SDF";
        spec.help = spec.label;
        spec.type = type;
        return spec;
    };
    auto scene = option("scene", "Deterministic box recipe", config::ValueType::Enum);
    scene.enum_values = {"box",
                         "rotated",
                         "cut",
                         "sticks",
                         "negative-sticks",
                         "negative-sphere",
                         "negative-stick-ball"};
    auto cutter_mode =
        option("cutter-mode", "Cutter treatment for negative-stick-ball", config::ValueType::Enum);
    cutter_mode.enum_values = {"original", "intro-bores", "through"};
    auto lighting = option("lighting", "Lighting route", config::ValueType::Enum);
    lighting.enum_values = {"atmosphere", "material-study", "material-furnace",
                            "material-atmosphere"};
    auto motion =
        option("motion", "Deterministic capture/presentation motion", config::ValueType::Enum);
    motion.enum_values = {"static", "orbit", "construction", "presentation", "carve"};
    auto ground = option("ground", "Ground", config::ValueType::Bool);
    ground.negative_cli_name = "--no-ground";
    auto clouds =
        option("clouds", "Frozen shared broken-cumulus environment", config::ValueType::Bool);
    clouds.negative_cli_name = "--no-clouds";
    auto cloud_backdrop =
        option("cloud-backdrop", "Visible cloud layer (off: inspect clouded IBL without composite)",
               config::ValueType::Bool);
    cloud_backdrop.negative_cli_name = "--no-cloud-backdrop";
    auto cloud_sky_only = option("cloud-sky-only", "Fade clouds into the sky-only backdrop horizon",
                                 config::ValueType::Bool);
    cloud_sky_only.negative_cli_name = "--no-cloud-sky-only";
    auto cloud_distance =
        option("cloud-distance-mode", "Cloud horizon route", config::ValueType::Enum);
    cloud_distance.enum_values = {"auto", "local"};
    auto cloud_quality =
        option("cloud-quality", "Visible cloud resolution", config::ValueType::Enum);
    cloud_quality.enum_values = {"half", "full"};
    auto cloud_resolve =
        option("cloud-resolve-mode", "Shared cloud edge filter", config::ValueType::Enum);
    cloud_resolve.enum_values = {"terrain-post", "metadata-bilateral"};
    auto cloud_debug =
        option("cloud-debug-view", "Shared cloud diagnostics", config::ValueType::Enum);
    cloud_debug.enum_values = {"final",     "raw-final",       "cloud-alpha",
                               "edge-mask", "horizon-handoff", "integrated-horizon-alpha"};
    auto validate =
        option("validate-field", "Compare GPU rays with the CPU recipe", config::ValueType::Bool);
    validate.negative_cli_name = "--no-validate-field";
    auto batches =
        option("validate-batches", "Check field reset and 128/129/256/257 edit promotion",
               config::ValueType::Bool);
    batches.negative_cli_name = "--no-validate-batches";
    auto subtractions = option("validate-subtractions",
                               "Check intersecting cavities and subtractive batch boundaries",
                               config::ValueType::Bool);
    subtractions.negative_cli_name = "--no-validate-subtractions";
    auto ao = option("ao", "Local environment occlusion", config::ValueType::Bool);
    ao.negative_cli_name = "--no-ao";
    auto occluder = option("occluder", "Raster plane intersecting the SDF for depth checks",
                           config::ValueType::Bool);
    occluder.negative_cli_name = "--no-occluder";
    auto cue = option("cutter-cue", "Show cutter bounds during the slow carving introduction",
                      config::ValueType::Bool);
    cue.negative_cli_name = "--no-cutter-cue";
    auto procedural =
        option("procedural", "Procedural surface appearance", config::ValueType::Bool);
    procedural.negative_cli_name = "--no-procedural";
    auto bump = option("bump", "Procedural surface normal detail", config::ValueType::Bool);
    bump.negative_cli_name = "--no-bump";
    auto fast_path =
        option("material-fast-path", "Uniform material shading shortcut", config::ValueType::Bool);
    fast_path.negative_cli_name = "--no-material-fast-path";
    return config::Schema::builder()
        .compose(host::common_run_config_schema(c.common))
        .bind(option("workshop", "Material workshop specimen: sphere, block, csg",
                     config::ValueType::String),
              c.workshop)
        .bind(option("material", "Shared workshop material", config::ValueType::String),
              c.workshop_material)
        .bind(option("material-recipe", "Standalone recipe override", config::ValueType::Path),
              c.material_recipe_path)
        .bind(option("export-material-recipe", "Export resolved workshop material",
                     config::ValueType::Path),
              c.export_material_recipe_path)
        .bind(option("workshop-cut-material", "Use contrasting cut material",
                     config::ValueType::Bool),
              c.workshop_cut_material)
        .bind(option("workshop-blend-materials", "Blend fused surface materials",
                     config::ValueType::Bool),
              c.workshop_blend_materials)
        .bind(scene, c.scene)
        .bind(lighting, c.lighting)
        .bind(clouds, c.clouds)
        .bind(cloud_backdrop, c.cloud_backdrop)
        .bind(cloud_sky_only, c.cloud_sky_only)
        .bind(cloud_distance, c.cloud_distance)
        .bind(cloud_quality, c.cloud_quality)
        .bind(cloud_resolve, c.cloud_resolve)
        .bind(cloud_debug, c.cloud_debug)
        .bind(option("cloud-view-samples", "Single-frame cloud samples (1, 2 or 4)",
                     config::ValueType::UInt32),
              c.cloud_view_samples)
        .bind(option("cloud-coverage", "Frozen cloud coverage", config::ValueType::Float),
              c.cloud_coverage)
        .bind(option("cloud-time", "Frozen weather time in seconds", config::ValueType::Float),
              c.cloud_time)
        .bind(option("environment-extent", "Atmosphere reflection face extent (64 or 128)",
                     config::ValueType::UInt32),
              c.environment_extent)
        .bind(option("glass-crossings", "Glass interfaces including primary hit (4, 8 or 16)",
                     config::ValueType::UInt32),
              c.glass_crossings)
        .bind(cutter_mode, c.cutter_mode)
        .bind(motion, c.timeline.motion)
        .bind(option("duration", "Logical clip duration in seconds", config::ValueType::Float),
              c.timeline.duration)
        .bind(option("construction-seconds", "Construction duration in seconds",
                     config::ValueType::Float),
              c.timeline.construction_seconds)
        .bind(option("orbit-turns", "Turns during the orbit phase", config::ValueType::Float),
              c.timeline.orbit_turns)
        .bind(option("time", "Fixed logical time for a headless still (-1 uses frame progress)",
                     config::ValueType::Float),
              c.snapshot_time)
        .bind(option("recipe", "Load an explicit versioned box recipe", config::ValueType::Path),
              c.recipe_path)
        .bind(option("export-recipe", "Save the complete ordered recipe", config::ValueType::Path),
              c.export_recipe_path)
        .bind(option("authored-scene", "Load a named version-2 authored scene",
                     config::ValueType::Path),
              c.authored_path)
        .bind(
            option("export-authored", "Save the explicit authored scene", config::ValueType::Path),
            c.export_authored_path)
        .bind(option("seed", "Seed", config::ValueType::UInt32), c.seed)
        .bind(option("count", "Sticks (negative scenes: cutters, excluding the base)",
                     config::ValueType::UInt32),
              c.count)
        .bind(option("cutter-width", "Full cutter width for negative-stick-ball (0.003–0.06)",
                     config::ValueType::Float),
              c.cutter_width)
        .bind(option("debug",
                     "Debug (0 shaded, 1 normals, 2 distance, 3 steps, 4 bricks, 5 parts, 6 base "
                     "color, 7 roughness, 8 surface normals, 9 glass transport diagnostics, "
                     "10 opaque diffuse, 11 opaque reflections)",
                     config::ValueType::UInt32),
              c.debug)
        .bind(option("shadows", "Shadows (0 off, 1 hard, 2 soft)", config::ValueType::UInt32),
              c.shadows)
        .bind(ground, c.ground)
        .bind(
            option("atlas-axis", "Atlas bricks per axis (64, 80 or 92)", config::ValueType::UInt32),
            c.atlas_axis)
        .bind(validate, c.validate)
        .bind(batches, c.validate_batches)
        .bind(subtractions, c.validate_subtractions)
        .bind(option("validate-history", "Check every authored history state and backward seeks",
                     config::ValueType::Bool),
              c.validate_history)
        .bind(ao, c.ao)
        .bind(option("clay", "Neutral inspection material (legacy clay)", config::ValueType::Bool),
              c.clay)
        .bind(option("supersample", "Internal resolution multiplier (1 or 2)",
                     config::ValueType::UInt32),
              c.supersample)
        .bind(option("sun-elevation", "Sun elevation in degrees", config::ValueType::Float),
              c.sun_elevation)
        .bind(option("sun-azimuth", "Sun azimuth in degrees", config::ValueType::Float),
              c.sun_azimuth)
        .bind(option("exposure", "Exposure in stops", config::ValueType::Float), c.exposure)
        .bind(option("roughness", "Object roughness", config::ValueType::Float), c.roughness)
        .bind(option("shadow-softness", "Soft shadow angular radius", config::ValueType::Float),
              c.shadow_softness)
        .bind(occluder, c.occluder)
        .bind(cue, c.cutter_cue)
        .bind(procedural, c.procedural)
        .bind(bump, c.bump)
        .bind(fast_path, c.material_fast_path)
        .bind(option("yaw", "Yaw", config::ValueType::Float), c.yaw)
        .bind(option("pitch", "Pitch", config::ValueType::Float), c.pitch)
        .bind(option("distance", "Distance", config::ValueType::Float), c.distance)
        .bind(option("target-y", "Orbit target height", config::ValueType::Float), c.target_y)
        .build();
}
inline SdfConfig parse_sdf_config(int argc, char** argv, config::ParseResult* result = nullptr) {
    config::ParseResult local;
    auto& parsed = result ? *result : local;
    auto config = host::parse_configured_app<SdfConfig>(argc, argv, sdf_config_schema, &parsed);
    if (!config.workshop.empty()) {
        if (!parsed.path_was_assigned("lighting"))
            config.lighting = "material-study";
        if (!parsed.path_was_assigned("exposure"))
            config.exposure = .6F;
        if (!parsed.path_was_assigned("distance"))
            config.distance = 1.05F;
        if (!parsed.path_was_assigned("yaw"))
            config.yaw = 0;
        if (!parsed.path_was_assigned("pitch"))
            config.pitch = .15F;
        if (!parsed.path_was_assigned("target-y"))
            config.target_y = 0;
        if (!parsed.path_was_assigned("glass-crossings"))
            config.glass_crossings = 8;
        if (!parsed.path_was_assigned("sun-elevation"))
            config.sun_elevation = 8;
        if (!parsed.path_was_assigned("sun-azimuth"))
            config.sun_azimuth = -155;
        if (!parsed.path_was_assigned("cloud-time"))
            config.cloud_time = 300;
        if (!parsed.path_was_assigned("cloud-coverage"))
            config.cloud_coverage = .28F;
    }
    validate_timeline(config.timeline);
    if (config.common.headless && config.common.capture_mode == CaptureMode::Video &&
        config.timeline.motion != "static" && !parsed.path_was_assigned("frames")) {
        const double count = std::round(config.timeline.duration * config.common.fps);
        if (count > std::numeric_limits<std::uint32_t>::max())
            throw std::invalid_argument("SDF capture duration/fps exceeds the frame limit");
        config.common.frames = static_cast<std::uint32_t>(std::max(1.0, count));
    }
    return config;
}
int run_sdf(const SdfConfig& config);
} // namespace cubey::projects::sdf
