#pragma once

#include <cmath>
#include <cubey/render/atmosphere_environment.h>
#include <cubey/render/terrain_surface_patch_scene.h>

namespace cubey::projects::fluid::fluid_25d {
// Fixed shared-runtime daylight, oriented to Scenic's retained directional sun.
// Cubey azimuth uses -Z as zero. No time-of-day/cloud/atlas-generation subsystem.
inline render::AtmosphereEnvironmentConfig fluid_25d_fixed_daylight() {
    return render::terrain_surface_patch_fixed_daylight();
}
} // namespace cubey::projects::fluid::fluid_25d
