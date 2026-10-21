#pragma once
#include <cubey/host/configured_app.h>

namespace cubey::projects::sdf {
struct SdfConfig {
    host::CommonRunConfig common;
    std::string scene = "sticks";
    std::uint32_t seed = 42, count = 512, debug = 0, shadows = 0;
    std::uint32_t atlas_axis = 64;
    std::uint32_t supersample = 1;
    float sun_elevation = 35, sun_azimuth = -55, exposure = 0, roughness = 0.36F,
          shadow_softness = 0.04F;
    bool ao = false;
    bool ground = true, validate = false, validate_batches = false, occluder = false;
    float yaw = 0.6F, pitch = 0.3F, distance = 1.35F;
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
    scene.enum_values = {"box", "rotated", "cut", "sticks"};
    auto ground = option("ground", "Ground", config::ValueType::Bool);
    ground.negative_cli_name = "--no-ground";
    auto validate =
        option("validate-field", "Compare GPU rays with the CPU recipe", config::ValueType::Bool);
    validate.negative_cli_name = "--no-validate-field";
    auto batches =
        option("validate-batches", "Check field reset and 128/129/256/257 edit promotion",
               config::ValueType::Bool);
    batches.negative_cli_name = "--no-validate-batches";
    auto ao = option("ao", "Local environment occlusion", config::ValueType::Bool);
    ao.negative_cli_name = "--no-ao";
    auto occluder = option("occluder", "Raster plane intersecting the SDF for depth checks",
                           config::ValueType::Bool);
    occluder.negative_cli_name = "--no-occluder";
    return config::Schema::builder()
        .compose(host::common_run_config_schema(c.common))
        .bind(scene, c.scene)
        .bind(option("seed", "Seed", config::ValueType::UInt32), c.seed)
        .bind(option("count", "Sticks", config::ValueType::UInt32), c.count)
        .bind(option("debug", "Debug (0 shaded, 1 normals, 2 distance, 3 steps)",
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
        .bind(option("yaw", "Yaw", config::ValueType::Float), c.yaw)
        .bind(option("pitch", "Pitch", config::ValueType::Float), c.pitch)
        .bind(option("distance", "Distance", config::ValueType::Float), c.distance)
        .build();
}
inline SdfConfig parse_sdf_config(int argc, char** argv, config::ParseResult* result = nullptr) {
    return host::parse_configured_app<SdfConfig>(argc, argv, sdf_config_schema, result);
}
int run_sdf(const SdfConfig& config);
} // namespace cubey::projects::sdf
