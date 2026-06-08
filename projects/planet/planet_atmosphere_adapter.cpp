#include "planet_atmosphere_adapter.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace cubey::projects::planet {
namespace {

[[nodiscard]] cubey::math::Vec3 normalized_or_up(cubey::math::Vec3 direction) {
    if (glm::dot(direction, direction) <= 0.0F) {
        return {0.0F, 1.0F, 0.0F};
    }
    return glm::normalize(direction);
}

} // namespace

cubey::render::AtmosphereEnvironmentConfig
planet_atmosphere_environment_config(const PlanetAtmosphereInputs& inputs) {
    constexpr float kMetersToKm = 0.001F;
    constexpr float kRadiansToDegrees = 180.0F / std::numbers::pi_v<float>;
    const cubey::math::Vec3 sun_direction = normalized_or_up(inputs.sun_direction);
    const float elevation_degrees =
        std::asin(std::clamp(sun_direction.y, -1.0F, 1.0F)) * kRadiansToDegrees;
    const float azimuth_degrees = std::atan2(sun_direction.x, -sun_direction.z) * kRadiansToDegrees;

    cubey::render::AtmosphereEnvironmentConfig config{};
    config.bottom_radius_km = std::max(inputs.planet_radius_m * kMetersToKm, 0.001F);
    config.top_radius_km =
        std::max(inputs.atmosphere_outer_radius_m * kMetersToKm, config.bottom_radius_km);
    config.camera_altitude_km = std::max(inputs.camera_altitude_m * kMetersToKm, 0.0F);
    config.sun_elevation_degrees = elevation_degrees;
    config.sun_azimuth_degrees =
        cubey::render::atmosphere_environment_wrap_signed_degrees(azimuth_degrees);
    config.sun_angular_radius = inputs.sun_angular_radius_rad;
    config.render_celestial_content = false;
    config.reference_geometry_enabled = false;
    config.moon.enabled = false;
    return config;
}

} // namespace cubey::projects::planet
