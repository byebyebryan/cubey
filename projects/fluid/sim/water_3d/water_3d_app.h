#pragma once

#include "../../water_3d/water_3d_project_config.h"

namespace cubey::projects::fluid::water_3d {

int run_water_3d(const Water3DProjectConfig& config, Water3DAppInfo app_info = {});

} // namespace cubey::projects::fluid::water_3d
