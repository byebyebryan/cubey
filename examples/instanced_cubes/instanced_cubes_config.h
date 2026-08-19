#pragma once

#include <cubey/host/configured_app.h>

namespace cubey::examples::instanced_cubes {

struct InstancedCubesConfig {
    cubey::host::CommonRunConfig common;
};

inline cubey::config::Schema instanced_cubes_config_schema(InstancedCubesConfig& config) {
    return cubey::config::Schema::builder()
        .compose(cubey::host::common_run_config_schema(config.common))
        .build();
}

inline InstancedCubesConfig
parse_instanced_cubes_config(int argc, char** argv, cubey::config::ParseResult* result = nullptr) {
    return cubey::host::parse_configured_app<InstancedCubesConfig>(
        argc, argv, instanced_cubes_config_schema, result);
}

} // namespace cubey::examples::instanced_cubes
