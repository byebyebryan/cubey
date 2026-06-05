#pragma once

#include <cubey/core/math.h>
#include <cubey/render/atmosphere_environment.h>

namespace cubey::projects::planet {

struct PlanetCelestialSun {
    bool visible = true;
    cubey::math::Vec3 direction{0.0F, 1.0F, 0.0F};
    cubey::math::Vec3 color{1.0F, 0.94F, 0.82F};
    float intensity = 1.0F;
    float angular_radius_rad = 0.004675F;
};

struct PlanetCelestialSystem {
    PlanetCelestialSun sun{};
};

[[nodiscard]] PlanetCelestialSystem planet_celestial_system_from_atmosphere(
    const cubey::render::AtmosphereEnvironmentConfig& atmosphere);
[[nodiscard]] cubey::render::AtmosphereEnvironmentConfig planet_atmosphere_inputs_from_celestial(
    cubey::render::AtmosphereEnvironmentConfig atmosphere,
    const PlanetCelestialSystem& celestial);
[[nodiscard]] cubey::render::AtmosphereEnvironmentLighting planet_celestial_lighting(
    const cubey::render::AtmosphereEnvironmentConfig& atmosphere,
    const PlanetCelestialSystem& celestial);

} // namespace cubey::projects::planet
