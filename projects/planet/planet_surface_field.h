#pragma once

#include "planet_config.h"
#include "planet_surface.h"

#include <cubey/core/math.h>

#include <cstdint>

namespace cubey::projects::planet {

struct PlanetSurfaceSample {
    cubey::math::Vec3 sphere_normal{0.0F, 1.0F, 0.0F};
    cubey::math::Vec3 normal{0.0F, 1.0F, 0.0F};
    cubey::math::DVec3 world_position_m{0.0, 0.0, 0.0};
    float height_m = 0.0F;
    float normalized_elevation = 0.0F;
    float normalized_slope = 0.0F;
};

[[nodiscard]] cubey::math::Vec3 planet_surface_cube_face_point(std::uint32_t face, float u,
                                                               float v);
[[nodiscard]] cubey::math::DVec3 planet_surface_sphere_world_position_m(const PlanetConfig& config,
                                                                        std::uint32_t face, float u,
                                                                        float v);
[[nodiscard]] float planet_surface_terrain_height_m(const PlanetConfig& config,
                                                    cubey::math::Vec3 sphere_normal);
[[nodiscard]] PlanetSurfaceSample
planet_surface_sample_field(const PlanetConfig& config, PlanetSurfacePatchId id, float u, float v);

} // namespace cubey::projects::planet
