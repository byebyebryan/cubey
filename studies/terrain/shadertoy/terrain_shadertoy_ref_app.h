#pragma once

#include "terrain_shadertoy_ref_config.h"

#include <cubey/core/run_config.h>

namespace cubey::projects::terrain_shadertoy_ref {

int run_terrain_shadertoy_ref(const RunConfig& run_config,
                              const TerrainShadertoyRefConfig& reference_config);

} // namespace cubey::projects::terrain_shadertoy_ref
