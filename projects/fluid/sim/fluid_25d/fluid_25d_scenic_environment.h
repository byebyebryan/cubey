#pragma once

#include <cmath>
#include <cubey/render/atmosphere_environment.h>

namespace cubey::projects::fluid::fluid_25d {
// Fixed shared-runtime daylight, oriented to Scenic's retained directional sun.
// Cubey azimuth uses -Z as zero. No time-of-day/cloud/atlas-generation subsystem.
inline render::AtmosphereEnvironmentConfig fluid_25d_fixed_daylight() {
    render::AtmosphereEnvironmentConfig result;
    const auto sun = glm::normalize(math::Vec3{-0.35F, 0.42F, 0.84F});
    result.sun_elevation_degrees = glm::degrees(std::asin(sun.y));
    result.sun_azimuth_degrees = glm::degrees(std::atan2(sun.x, -sun.z));
    result.reference_geometry_enabled = false;
    result.render_celestial_content = false;
    result.render_night_sky = false;
    result.render_moon_disk = false;
    result.moon.enabled = false;
    return result;
}
} // namespace cubey::projects::fluid::fluid_25d
