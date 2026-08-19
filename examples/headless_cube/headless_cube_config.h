#pragma once

#include <cubey/host/configured_app.h>

namespace cubey::examples::headless_cube {

struct HeadlessCubeConfig {
    cubey::host::CommonRunConfig common;
};

inline cubey::config::Schema headless_cube_config_schema(HeadlessCubeConfig& config) {
    return cubey::config::Schema::builder()
        .compose(cubey::host::common_run_config_schema(config.common))
        .build();
}

inline HeadlessCubeConfig parse_headless_cube_config(int argc, char** argv,
                                                     cubey::config::ParseResult* result = nullptr) {
    return cubey::host::parse_configured_app<HeadlessCubeConfig>(
        argc, argv, headless_cube_config_schema, result);
}

} // namespace cubey::examples::headless_cube
