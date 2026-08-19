#pragma once

#include <cubey/host/configured_app.h>

namespace cubey::examples::particle_cubes {

struct ParticleCubesConfig {
    cubey::host::CommonRunConfig common;
};

inline cubey::config::Schema particle_cubes_config_schema(ParticleCubesConfig& config) {
    return cubey::config::Schema::builder()
        .compose(cubey::host::common_run_config_schema(config.common))
        .build();
}

inline ParticleCubesConfig
parse_particle_cubes_config(int argc, char** argv, cubey::config::ParseResult* result = nullptr) {
    return cubey::host::parse_configured_app<ParticleCubesConfig>(
        argc, argv, particle_cubes_config_schema, result);
}

} // namespace cubey::examples::particle_cubes
