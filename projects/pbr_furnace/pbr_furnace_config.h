#pragma once

#include <cubey/host/configured_app.h>

#include <string>

namespace cubey::projects::pbr_furnace {

struct PbrFurnaceConfig {
    cubey::host::CommonRunConfig common;
    std::string conformance_case = "none";
};

inline cubey::config::Schema pbr_furnace_config_schema(PbrFurnaceConfig& config) {
    return cubey::config::Schema::builder()
        .compose(cubey::host::common_run_config_schema(config.common))
        .bind({.path = "conformance_case",
               .cli_name = "--conformance-case",
               .negative_cli_name = {},
               .label = "Conformance Case",
               .group_path = "Conformance",
               .help = "Opt-in deterministic material conformance layout.",
               .type = cubey::config::ValueType::Enum,
               .range = {},
               .enum_values = {"none", "ior", "specular", "clearcoat"}},
              config.conformance_case)
        .build();
}

inline PbrFurnaceConfig parse_pbr_furnace_config(int argc, char** argv,
                                                 cubey::config::ParseResult* result = nullptr) {
    return cubey::host::parse_configured_app<PbrFurnaceConfig>(argc, argv,
                                                               pbr_furnace_config_schema, result);
}

} // namespace cubey::projects::pbr_furnace
