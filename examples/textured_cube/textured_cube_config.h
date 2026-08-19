#pragma once

#include <cubey/host/configured_app.h>

namespace cubey::examples::textured_cube {

struct TexturedCubeConfig {
    cubey::host::CommonRunConfig common;
};

inline cubey::config::Schema textured_cube_config_schema(TexturedCubeConfig& config) {
    return cubey::config::Schema::builder()
        .compose(cubey::host::common_run_config_schema(config.common))
        .build();
}

inline TexturedCubeConfig parse_textured_cube_config(int argc, char** argv,
                                                     cubey::config::ParseResult* result = nullptr) {
    return cubey::host::parse_configured_app<TexturedCubeConfig>(
        argc, argv, textured_cube_config_schema, result);
}

} // namespace cubey::examples::textured_cube
