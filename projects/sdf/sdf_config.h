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
    std::string scene = "sticks";
    std::uint32_t seed = 42, count = 512, debug = 0, shadows = 1;
    std::uint32_t atlas_axis = 64;
    std::uint32_t supersample = 1;
    float sun_elevation = 35, sun_azimuth = -55, exposure = 0, roughness = 0.36F,
          shadow_softness = 0.04F;
    bool ao = false;
    bool cutter_cue = true;
    bool ground = false, validate = false, validate_batches = false, occluder = false;
    bool validate_subtractions = false;
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
    scene.enum_values = {"box", "rotated", "cut", "sticks", "negative-sticks"};
    auto motion =
        option("motion", "Deterministic capture/presentation motion", config::ValueType::Enum);
    motion.enum_values = {"static", "orbit", "construction", "presentation", "carve"};
    auto ground = option("ground", "Ground", config::ValueType::Bool);
    ground.negative_cli_name = "--no-ground";
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
    return config::Schema::builder()
        .compose(host::common_run_config_schema(c.common))
        .bind(scene, c.scene)
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
        .bind(option("seed", "Seed", config::ValueType::UInt32), c.seed)
        .bind(option("count", "Sticks (negative-sticks: cutters, excluding the base)",
                     config::ValueType::UInt32),
              c.count)
        .bind(option("debug", "Debug (0 shaded, 1 normals, 2 distance, 3 steps, 4 bricks)",
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
        .bind(ao, c.ao)
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
