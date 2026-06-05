#include "planet_celestial.h"

#include <algorithm>
#include <cmath>

namespace cubey::projects::planet {
namespace {

[[nodiscard]] cubey::math::Vec3 normalized_or_up(cubey::math::Vec3 direction) {
    if (glm::dot(direction, direction) <= 0.0F) {
        return {0.0F, 1.0F, 0.0F};
    }
    return glm::normalize(direction);
}

[[nodiscard]] float direction_elevation_degrees(cubey::math::Vec3 direction) {
    const cubey::math::Vec3 normal = normalized_or_up(direction);
    return cubey::render::atmosphere_environment_radians_to_degrees(
        std::asin(std::clamp(normal.y, -1.0F, 1.0F)));
}

[[nodiscard]] float direction_azimuth_degrees(cubey::math::Vec3 direction) {
    const cubey::math::Vec3 normal = normalized_or_up(direction);
    return cubey::render::atmosphere_environment_wrap_signed_degrees(
        cubey::render::atmosphere_environment_radians_to_degrees(std::atan2(normal.x, -normal.z)));
}

} // namespace

PlanetCelestialSystem planet_celestial_system_from_atmosphere(
    const cubey::render::AtmosphereEnvironmentConfig& atmosphere) {
    const cubey::render::AtmosphereEnvironmentLighting lighting =
        cubey::render::atmosphere_environment_lighting(atmosphere);
    return {
        .sun =
            {
                .visible = true,
                .direction = normalized_or_up(lighting.sun_direction),
                .color = lighting.sun_color,
                .intensity = lighting.sun_intensity,
                .angular_radius_rad = atmosphere.sun_angular_radius,
            },
    };
}

cubey::render::AtmosphereEnvironmentConfig planet_atmosphere_inputs_from_celestial(
    cubey::render::AtmosphereEnvironmentConfig atmosphere,
    const PlanetCelestialSystem& celestial) {
    atmosphere.sun_elevation_degrees = direction_elevation_degrees(celestial.sun.direction);
    atmosphere.sun_azimuth_degrees = direction_azimuth_degrees(celestial.sun.direction);
    atmosphere.sun_angular_radius = celestial.sun.angular_radius_rad;
    return atmosphere;
}

cubey::render::AtmosphereEnvironmentLighting planet_celestial_lighting(
    const cubey::render::AtmosphereEnvironmentConfig& atmosphere,
    const PlanetCelestialSystem& celestial) {
    return cubey::render::atmosphere_environment_lighting(
        planet_atmosphere_inputs_from_celestial(atmosphere, celestial));
}

} // namespace cubey::projects::planet
