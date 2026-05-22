#pragma once

#include "water_3d_config.h"

#include <cubey/core/run_config.h>

namespace cubey::projects::fluid::water_3d {

int run_water_3d(const RunConfig& config, Water3DAppInfo app_info = {});

} // namespace cubey::projects::fluid::water_3d
