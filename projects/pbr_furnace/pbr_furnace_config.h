#pragma once

#include <cubey/host/configured_app.h>

namespace cubey::projects::pbr_furnace {

struct PbrFurnaceConfig {
    cubey::host::CommonRunConfig common;
};

inline cubey::config::Schema pbr_furnace_config_schema(PbrFurnaceConfig& config) {
    return cubey::config::Schema::builder()
        .compose(cubey::host::common_run_config_schema(config.common))
        .build();
}

inline PbrFurnaceConfig parse_pbr_furnace_config(int argc, char** argv,
                                                 cubey::config::ParseResult* result = nullptr) {
    return cubey::host::parse_configured_app<PbrFurnaceConfig>(argc, argv,
                                                               pbr_furnace_config_schema, result);
}

} // namespace cubey::projects::pbr_furnace
