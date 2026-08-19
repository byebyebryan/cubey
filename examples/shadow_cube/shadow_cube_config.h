#pragma once

#include <cubey/host/configured_app.h>

namespace cubey::examples::shadow_cube {

struct ShadowCubeConfig {
    cubey::host::CommonRunConfig common;
};

inline cubey::config::Schema shadow_cube_config_schema(ShadowCubeConfig& config) {
    return cubey::config::Schema::builder()
        .compose(cubey::host::common_run_config_schema(config.common))
        .build();
}

inline ShadowCubeConfig parse_shadow_cube_config(int argc, char** argv,
                                                 cubey::config::ParseResult* result = nullptr) {
    return cubey::host::parse_configured_app<ShadowCubeConfig>(argc, argv,
                                                               shadow_cube_config_schema, result);
}

} // namespace cubey::examples::shadow_cube
