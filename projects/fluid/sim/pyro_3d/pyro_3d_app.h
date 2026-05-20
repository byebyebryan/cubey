#pragma once

#include "pyro_3d_config.h"

#include <cubey/core/run_config.h>

namespace cubey::projects::fluid::pyro_3d {

struct Pyro3DAppInfo {
    Pyro3DMode mode = Pyro3DMode::Fire;
    const char* app_name = "pyro_3d";
    const char* ready_status = "rendering 3D pyro project";
    const char* ui_title = "Pyro 3D";
};

int run_pyro_3d(const RunConfig& config, Pyro3DAppInfo app_info);

} // namespace cubey::projects::fluid::pyro_3d
