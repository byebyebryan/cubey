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
    PbrMaterialsPbrConfig pbr;
};

inline cubey::config::OptionSpec pbr_materials_option(std::string path, std::string cli_name,
                                                       std::string label, std::string group_path,
                                                       std::string help,
                                                       cubey::config::ValueType type,
                                                       cubey::config::Range range = {}) {
    return {
        .path = std::move(path),
        .cli_name = std::move(cli_name),
        .negative_cli_name = {},
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
    builder
        .bind(pbr_materials_option("debug_view", "--debug-view", "Debug View", "Debug",
                                    "Project-specific debug view name.",
                                    cubey::config::ValueType::String),
              config.debug_view);
    builder.compose(cubey::pbr_static_ibl_schema(config.pbr));
    return std::move(builder).build();
}

inline PbrMaterialsConfig
parse_pbr_materials_config(int argc, char** argv, cubey::config::ParseResult* result = nullptr) {
    return cubey::host::parse_configured_app<PbrMaterialsConfig>(
        argc, argv, pbr_materials_config_schema, result);
}

} // namespace cubey::projects::pbr_materials
