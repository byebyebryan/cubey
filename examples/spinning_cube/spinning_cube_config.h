#pragma once

#include <cubey/host/configured_app.h>

namespace cubey::examples::spinning_cube {

struct SpinningCubeConfig {
    cubey::host::CommonRunConfig common;
};

inline cubey::config::Schema spinning_cube_config_schema(SpinningCubeConfig& config) {
    return cubey::config::Schema::builder()
        .compose(cubey::host::common_run_config_schema(config.common))
        .build();
}

inline SpinningCubeConfig parse_spinning_cube_config(int argc, char** argv,
                                                     cubey::config::ParseResult* result = nullptr) {
    return cubey::host::parse_configured_app<SpinningCubeConfig>(
        argc, argv, spinning_cube_config_schema, result);
}

} // namespace cubey::examples::spinning_cube
