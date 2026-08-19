#pragma once

#include <cubey/host/configured_app.h>

namespace cubey::projects::fractal_2d {

struct Fractal2dConfig {
    cubey::host::CommonRunConfig common;
};

inline cubey::config::Schema fractal_2d_config_schema(Fractal2dConfig& config) {
    return cubey::config::Schema::builder()
        .compose(cubey::host::common_run_config_schema(config.common))
        .build();
}

inline Fractal2dConfig parse_fractal_2d_config(int argc, char** argv,
                                               cubey::config::ParseResult* result = nullptr) {
    return cubey::host::parse_configured_app<Fractal2dConfig>(argc, argv, fractal_2d_config_schema,
                                                              result);
}

} // namespace cubey::projects::fractal_2d
