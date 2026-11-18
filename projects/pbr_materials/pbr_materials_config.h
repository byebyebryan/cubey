#pragma once

#include <cubey/engine/pbr_environment_schema.h>
#include <cubey/host/configured_app.h>

#include <filesystem>
#include <string>
#include <utility>

namespace cubey::projects::pbr_materials {

using PbrMaterialsPbrConfig = cubey::PbrStaticIblOptions;

struct PbrMaterialsConfig {
    cubey::host::CommonRunConfig common;
    std::string debug_view;
    PbrMaterialsPbrConfig pbr = [] {
        PbrMaterialsPbrConfig c;
        c.exposure = .6F;
        return c;
    }();
    std::string material = "gallery", specimen = "sphere", environment = "studio";
    std::string sphere_density = "standard";
    std::string transmission_source = "screen";
    std::filesystem::path hdr_output, view_output;
    std::filesystem::path recipe_input, recipe_output;
    float yaw = 0, pitch = .15F, distance = 1.05F, roughness = -1, pattern_scale = -1;
    float orbit_degrees = 0;
    std::string presentation;
    std::string lighting_cycle = "frozen";
    double presentation_time = -1;
    bool pattern = true, bump = true;
    bool taa_moving_fixture = false;
    double taa_fixture_time = -1;
    std::uint32_t stress_edits = 0;
};

inline cubey::config::OptionSpec pbr_materials_option(std::string path, std::string cli_name,
                                                      std::string label, std::string group_path,
                                                      std::string help,
                                                      cubey::config::ValueType type,
                                                      cubey::config::Range range = {}) {
    return {
        .path = std::move(path),
        .cli_name = cli_name,
        .negative_cli_name =
            type == cubey::config::ValueType::Bool ? "--no-" + cli_name.substr(2) : "",
        .label = std::move(label),
        .group_path = std::move(group_path),
        .help = std::move(help),
        .type = type,
        .range = range,
        .enum_values = {},
    };
}

inline cubey::config::Schema pbr_materials_config_schema(PbrMaterialsConfig& config) {
    cubey::config::Schema::Builder builder = cubey::config::Schema::builder().compose(
        cubey::host::common_run_config_schema(config.common));
    builder.bind(pbr_materials_option("debug_view", "--debug-view", "Debug View", "Debug",
                                      "Project-specific debug view name.",
                                      cubey::config::ValueType::String),
                 config.debug_view);

    auto bind = [&](auto& value, const char* name, const char* cli, cubey::config::ValueType type) {
        builder.bind(
            pbr_materials_option(name, cli, name, "Workshop", "Material workshop setting.", type),
            value);
    };
    bind(config.material, "material", "--material", cubey::config::ValueType::String);
    bind(config.specimen, "specimen", "--specimen", cubey::config::ValueType::String);
    auto density =
        pbr_materials_option("sphere_density", "--sphere-density", "Sphere density", "Workshop",
                             "Opt-in comparison mesh resolution.", cubey::config::ValueType::Enum);
    density.enum_values = {"standard", "dense"};
    builder.bind(density, config.sphere_density);
    auto source = pbr_materials_option(
        "transmission_source", "--transmission-source", "Transmission source", "Workshop",
        "Projected screen radiance with edge fallback, or distant environment along the exit ray.",
        cubey::config::ValueType::Enum);
    source.enum_values = {"screen", "environment"};
    builder.bind(source, config.transmission_source);
    bind(config.taa_moving_fixture, "taa_moving_fixture", "--taa-moving-fixture",
         cubey::config::ValueType::Bool);
    bind(config.taa_fixture_time, "taa_fixture_time", "--taa-fixture-time",
         cubey::config::ValueType::Float);
    bind(config.hdr_output, "hdr_output", "--hdr-output", cubey::config::ValueType::Path);
    bind(config.view_output, "view_output", "--view-output", cubey::config::ValueType::Path);
    bind(config.environment, "environment", "--lighting", cubey::config::ValueType::String);
    bind(config.recipe_input, "recipe_input", "--recipe-input", cubey::config::ValueType::Path);
    bind(config.recipe_output, "recipe_output", "--recipe-output", cubey::config::ValueType::Path);
    bind(config.yaw, "yaw", "--yaw", cubey::config::ValueType::Float);
    bind(config.pitch, "pitch", "--pitch", cubey::config::ValueType::Float);
    bind(config.distance, "distance", "--distance", cubey::config::ValueType::Float);
    bind(config.roughness, "roughness", "--roughness", cubey::config::ValueType::Float);
    bind(config.pattern_scale, "pattern_scale", "--pattern-scale", cubey::config::ValueType::Float);
    bind(config.orbit_degrees, "orbit_degrees", "--orbit-degrees", cubey::config::ValueType::Float);
    bind(config.presentation, "presentation", "--presentation", cubey::config::ValueType::String);
    bind(config.presentation_time, "presentation_time", "--presentation-time",
         cubey::config::ValueType::Float);
    auto cycle =
        pbr_materials_option("lighting_cycle", "--lighting-cycle", "Lighting cycle", "Presentation",
                             "Frozen, dusk or dawn lighting.", cubey::config::ValueType::Enum);
    cycle.enum_values = {"frozen", "dusk", "dawn"};
    builder.bind(cycle, config.lighting_cycle);
    bind(config.pattern, "pattern", "--pattern", cubey::config::ValueType::Bool);
    bind(config.bump, "bump", "--bump", cubey::config::ValueType::Bool);
    bind(config.stress_edits, "stress_edits", "--stress-edits", cubey::config::ValueType::UInt32);
    builder.compose(cubey::pbr_static_ibl_schema(config.pbr));
    return std::move(builder).build();
}

inline PbrMaterialsConfig parse_pbr_materials_config(int argc, char** argv,
                                                     cubey::config::ParseResult* result = nullptr) {
    cubey::config::ParseResult parsed;
    auto c = cubey::host::parse_configured_app<PbrMaterialsConfig>(
        argc, argv, pbr_materials_config_schema, &parsed);
    if (c.material == "gallery" && c.presentation.empty() && !parsed.path_was_assigned("distance"))
        c.distance = 3.3F;
    if (!c.presentation.empty()) {
        if (!parsed.path_was_assigned("distance"))
            c.distance = 2;
        if (!parsed.path_was_assigned("pitch"))
            c.pitch = -.32F;
        if (!parsed.path_was_assigned("yaw"))
            c.yaw = -.12F;
        if (!parsed.path_was_assigned("environment"))
            c.environment = "clouds";
        if (c.presentation == "library") {
            if (!parsed.path_was_assigned("distance"))
                c.distance = .855F;
            if (!parsed.path_was_assigned("pitch"))
                c.pitch = 0;
            if (!parsed.path_was_assigned("yaw"))
                c.yaw = 0;
            if (!parsed.path_was_assigned("lighting_cycle"))
                c.lighting_cycle = "dusk";
        }
    }
    if (c.taa_moving_fixture) {
        if (!parsed.path_was_assigned("anti_aliasing"))
            c.common.anti_aliasing = "taa";
        if (!parsed.path_was_assigned("material"))
            c.material = "clay";
        if (!parsed.path_was_assigned("specimen"))
            c.specimen = "sphere";
        if (!parsed.path_was_assigned("environment"))
            c.environment = "studio";
        if (!parsed.path_was_assigned("lighting_cycle"))
            c.lighting_cycle = "frozen";
        if (!parsed.path_was_assigned("distance"))
            c.distance = 2.2F;
        if (!parsed.path_was_assigned("yaw"))
            c.yaw = 0.0F;
        if (!parsed.path_was_assigned("pitch"))
            c.pitch = 0.0F;
    }
    if (result)
        *result = std::move(parsed);
    return c;
}

} // namespace cubey::projects::pbr_materials
