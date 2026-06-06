#pragma once

#include "planet_config.h"
#include "planet_surface.h"

#include <cubey/core/math.h>

#include <cstdint>
#include <limits>

namespace cubey::projects::planet {

enum class PlanetSurfaceMaterial : std::uint8_t {
    Water,
    Lowland,
    Highland,
    Snow,
};

struct PlanetSurfaceSample {
    cubey::math::Vec3 sphere_normal{0.0F, 1.0F, 0.0F};
    cubey::math::Vec3 normal{0.0F, 1.0F, 0.0F};
    cubey::math::DVec3 world_position_m{0.0, 0.0, 0.0};
    float height_m = 0.0F;
    float height_above_sea_m = 0.0F;
    float water_depth_m = 0.0F;
    float normalized_bathymetry = 0.0F;
    float shoreline_mask = 0.0F;
    float normalized_elevation = 0.0F;
    float normalized_slope = 0.0F;
    PlanetSurfaceMaterial material = PlanetSurfaceMaterial::Lowland;
};

enum class PlanetSurfaceTileSource : std::uint8_t {
    Procedural,
};

struct PlanetSurfaceTileKey {
    std::uint32_t face = 0;
    std::uint32_t level = 0;
    std::uint32_t x = 0;
    std::uint32_t y = 0;

    friend bool operator==(const PlanetSurfaceTileKey&, const PlanetSurfaceTileKey&) = default;
};

struct PlanetSurfaceTileSummary {
    float min_height_m = std::numeric_limits<float>::max();
    float max_height_m = std::numeric_limits<float>::lowest();
    float min_height_above_sea_m = std::numeric_limits<float>::max();
    float max_height_above_sea_m = std::numeric_limits<float>::lowest();
    float max_water_depth_m = 0.0F;
    float max_shoreline_mask = 0.0F;
    float max_normalized_slope = 0.0F;
    std::uint32_t sample_count = 0;
    std::uint32_t material_mask = 0;
};

struct PlanetSurfaceTilePayload {
    PlanetSurfaceTileKey key{};
    PlanetSurfacePatchBounds bounds{};
    PlanetSurfaceTileSource source = PlanetSurfaceTileSource::Procedural;
    std::uint32_t generator_revision = 0;
    PlanetSurfaceTileSummary summary{};
};

[[nodiscard]] cubey::math::Vec3 planet_surface_cube_face_point(std::uint32_t face, float u,
                                                               float v);
[[nodiscard]] cubey::math::DVec3 planet_surface_sphere_world_position_m(const PlanetConfig& config,
                                                                        std::uint32_t face, float u,
                                                                        float v);
[[nodiscard]] float planet_surface_terrain_height_m(const PlanetConfig& config,
                                                    cubey::math::Vec3 sphere_normal);
[[nodiscard]] float planet_surface_height_above_sea_m(const PlanetConfig& config, float height_m);
[[nodiscard]] float planet_surface_water_depth_m(const PlanetConfig& config, float height_m);
[[nodiscard]] float planet_surface_normalized_bathymetry(const PlanetConfig& config,
                                                         float height_m);
[[nodiscard]] float planet_surface_shoreline_mask(const PlanetConfig& config, float height_m);
[[nodiscard]] PlanetSurfaceSample
planet_surface_sample_field(const PlanetConfig& config, PlanetSurfacePatchId id, float u, float v);
[[nodiscard]] PlanetSurfaceTileKey
planet_surface_tile_key_from_patch_id(PlanetSurfacePatchId id);
[[nodiscard]] PlanetSurfacePatchId
planet_surface_patch_id_from_tile_key(PlanetSurfaceTileKey key);
[[nodiscard]] PlanetSurfaceTilePayload
make_planet_surface_tile_payload(const PlanetConfig& config, PlanetSurfaceTileKey key,
                                 std::uint32_t sample_resolution = 4);
[[nodiscard]] PlanetSurfaceMaterial planet_surface_material(float height_above_sea_m,
                                                            float normalized_elevation,
                                                            float normalized_slope);
[[nodiscard]] cubey::math::Vec3 planet_surface_material_color(PlanetSurfaceMaterial material,
                                                              float normalized_elevation,
                                                              float normalized_slope);

} // namespace cubey::projects::planet
