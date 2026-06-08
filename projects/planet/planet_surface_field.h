#pragma once

#include "planet_config.h"
#include "planet_surface.h"

#include <cubey/core/math.h>

#include <array>
#include <cstdint>
#include <limits>

namespace cubey::projects::planet {

enum class PlanetSurfaceMaterial : std::uint8_t {
    DeepWater,
    ShallowWater,
    Beach,
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
    float land_mask = 0.0F;
    float moisture = 0.0F;
    float temperature = 0.0F;
    float roughness = 0.5F;
    PlanetSurfaceMaterial material = PlanetSurfaceMaterial::Lowland;
};

struct PlanetTerrainFeatureContext {
    cubey::math::Vec3 domain_point{0.0F, 0.0F, 0.0F};
    float continent_mask = 0.0F;
    float mountain_belt = 0.0F;
    float valley_network = 0.0F;
    float relief_gate = 0.0F;
    float plain_gate = 0.0F;
    float land_mask = 0.0F;
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
    float min_moisture = std::numeric_limits<float>::max();
    float max_moisture = std::numeric_limits<float>::lowest();
    float min_temperature = std::numeric_limits<float>::max();
    float max_temperature = std::numeric_limits<float>::lowest();
    float min_roughness = std::numeric_limits<float>::max();
    float max_roughness = std::numeric_limits<float>::lowest();
    float average_height_m = 0.0F;
    float average_height_above_sea_m = 0.0F;
    float average_moisture = 0.0F;
    float average_temperature = 0.0F;
    float average_roughness = 0.0F;
    float average_normalized_slope = 0.0F;
    float max_water_depth_m = 0.0F;
    float max_shoreline_mask = 0.0F;
    float land_coverage = 0.0F;
    float water_coverage = 0.0F;
    float shoreline_coverage = 0.0F;
    float max_normalized_slope = 0.0F;
    std::uint32_t sample_count = 0;
    std::uint32_t material_mask = 0;
    PlanetSurfaceMaterial dominant_material = PlanetSurfaceMaterial::Lowland;
    std::array<std::uint32_t, 6> material_counts{};
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
[[nodiscard]] PlanetTerrainFeatureContext
planet_surface_terrain_feature_context(const PlanetConfig& config,
                                       cubey::math::Vec3 sphere_normal);
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
                                                            float water_depth_m,
                                                            float shoreline_mask,
                                                            float normalized_elevation,
                                                            float normalized_slope,
                                                            float moisture, float temperature);
[[nodiscard]] cubey::math::Vec3 planet_surface_material_color(PlanetSurfaceMaterial material,
                                                              float normalized_elevation,
                                                              float normalized_slope,
                                                              float moisture, float temperature);

} // namespace cubey::projects::planet
