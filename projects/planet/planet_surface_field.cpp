#include "planet_surface_field.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>

namespace cubey::projects::planet {
namespace {

[[nodiscard]] float lerp(float a, float b, float t) {
    return a + (b - a) * t;
}

[[nodiscard]] float smootherstep(float value) {
    const float x = std::clamp(value, 0.0F, 1.0F);
    return x * x * x * (x * (x * 6.0F - 15.0F) + 10.0F);
}

[[nodiscard]] std::uint32_t hash_u32(std::int32_t x, std::int32_t y, std::int32_t z,
                                     std::uint32_t seed) {
    std::uint32_t value = seed;
    value ^= static_cast<std::uint32_t>(x) * 0x9e3779b9U;
    value ^= static_cast<std::uint32_t>(y) * 0x85ebca6bU;
    value ^= static_cast<std::uint32_t>(z) * 0xc2b2ae35U;
    value ^= value >> 16U;
    value *= 0x7feb352dU;
    value ^= value >> 15U;
    value *= 0x846ca68bU;
    value ^= value >> 16U;
    return value;
}

[[nodiscard]] float hash01(std::int32_t x, std::int32_t y, std::int32_t z, std::uint32_t seed) {
    constexpr float kInv24Bit = 1.0F / 16777215.0F;
    return static_cast<float>(hash_u32(x, y, z, seed) >> 8U) * kInv24Bit;
}

[[nodiscard]] float value_noise(cubey::math::Vec3 p, std::uint32_t seed) {
    const auto x0 = static_cast<std::int32_t>(std::floor(p.x));
    const auto y0 = static_cast<std::int32_t>(std::floor(p.y));
    const auto z0 = static_cast<std::int32_t>(std::floor(p.z));
    const float tx = smootherstep(p.x - static_cast<float>(x0));
    const float ty = smootherstep(p.y - static_cast<float>(y0));
    const float tz = smootherstep(p.z - static_cast<float>(z0));

    const auto lattice = [seed](std::int32_t x, std::int32_t y, std::int32_t z) {
        return hash01(x, y, z, seed) * 2.0F - 1.0F;
    };
    const float x00 = lerp(lattice(x0, y0, z0), lattice(x0 + 1, y0, z0), tx);
    const float x10 = lerp(lattice(x0, y0 + 1, z0), lattice(x0 + 1, y0 + 1, z0), tx);
    const float x01 = lerp(lattice(x0, y0, z0 + 1), lattice(x0 + 1, y0, z0 + 1), tx);
    const float x11 = lerp(lattice(x0, y0 + 1, z0 + 1), lattice(x0 + 1, y0 + 1, z0 + 1), tx);
    return lerp(lerp(x00, x10, ty), lerp(x01, x11, ty), tz);
}

[[nodiscard]] float fbm(cubey::math::Vec3 p, std::uint32_t seed, std::uint32_t octaves) {
    float amplitude = 0.5F;
    float frequency = 1.0F;
    float sum = 0.0F;
    float weight = 0.0F;
    for (std::uint32_t octave = 0; octave < octaves; ++octave) {
        sum += value_noise(p * frequency, seed + octave * 1013U) * amplitude;
        weight += amplitude;
        frequency *= 2.03F;
        amplitude *= 0.5F;
    }
    return weight > 0.0F ? sum / weight : 0.0F;
}

[[nodiscard]] double surface_radius_m(const PlanetConfig& config, float height_m) {
    return static_cast<double>(config.radius_m) + static_cast<double>(height_m);
}

[[nodiscard]] cubey::math::DVec3 terrain_world_position(const PlanetConfig& config,
                                                        std::uint32_t face, float u, float v) {
    const cubey::math::Vec3 sphere_normal =
        glm::normalize(planet_surface_cube_face_point(face, u, v));
    const double radius =
        surface_radius_m(config, planet_surface_terrain_height_m(config, sphere_normal));
    return {
        static_cast<double>(sphere_normal.x) * radius,
        static_cast<double>(sphere_normal.y) * radius,
        static_cast<double>(sphere_normal.z) * radius,
    };
}

[[nodiscard]] float terrain_normal_step_uv(const PlanetConfig& config,
                                           const PlanetSurfacePatchId& id) {
    const float divisions =
        static_cast<float>(config.patches_per_face) * std::exp2(static_cast<float>(id.level));
    const float patch_width_uv = 2.0F / std::max(divisions, 1.0F);
    const float cell_width_uv = patch_width_uv / static_cast<float>(config.patch_resolution);
    return std::clamp(cell_width_uv * 0.5F, 0.00005F, 0.02F);
}

[[nodiscard]] cubey::math::Vec3 terrain_normal(const PlanetConfig& config,
                                               const PlanetSurfacePatchId& id, float u, float v,
                                               cubey::math::Vec3 sphere_normal) {
    if (!config.terrain_enabled || config.terrain_height_scale_m <= 0.0F) {
        return sphere_normal;
    }

    const float normal_step = terrain_normal_step_uv(config, id);
    const float u0 = std::clamp(u - normal_step, -1.0F, 1.0F);
    const float u1 = std::clamp(u + normal_step, -1.0F, 1.0F);
    const float v0 = std::clamp(v - normal_step, -1.0F, 1.0F);
    const float v1 = std::clamp(v + normal_step, -1.0F, 1.0F);
    const cubey::math::DVec3 tangent_u = terrain_world_position(config, id.face, u1, v) -
                                         terrain_world_position(config, id.face, u0, v);
    const cubey::math::DVec3 tangent_v = terrain_world_position(config, id.face, u, v1) -
                                         terrain_world_position(config, id.face, u, v0);
    cubey::math::DVec3 normal_d = glm::cross(tangent_u, tangent_v);
    if (glm::length(normal_d) <= 0.0000001) {
        return sphere_normal;
    }
    normal_d = glm::normalize(normal_d);
    cubey::math::Vec3 normal{
        static_cast<float>(normal_d.x),
        static_cast<float>(normal_d.y),
        static_cast<float>(normal_d.z),
    };
    if (glm::dot(normal, sphere_normal) < 0.0F) {
        normal = -normal;
    }
    return glm::normalize(normal);
}

[[nodiscard]] float normalized_slope(cubey::math::Vec3 sphere_normal, cubey::math::Vec3 normal) {
    constexpr float kInvHalfPi = 0.6366197723675813F;
    const float dot_up =
        std::clamp(glm::dot(glm::normalize(sphere_normal), glm::normalize(normal)), 0.0F, 1.0F);
    return std::clamp(std::acos(dot_up) * kInvHalfPi, 0.0F, 1.0F);
}

[[nodiscard]] float clamp01(float value) {
    return std::clamp(value, 0.0F, 1.0F);
}

[[nodiscard]] cubey::math::Vec3 terrain_domain_point(const PlanetConfig& config,
                                                     cubey::math::Vec3 sphere_normal) {
    const cubey::math::Vec3 base = sphere_normal * std::max(config.terrain_noise_scale, 0.0001F);
    const cubey::math::Vec3 warp{
        fbm(base * 0.71F + cubey::math::Vec3{-2.8F, 5.2F, 1.1F}, config.terrain_seed + 271U, 3U),
        fbm(base * 0.67F + cubey::math::Vec3{4.6F, -1.7F, 3.5F}, config.terrain_seed + 283U, 3U),
        fbm(base * 0.74F + cubey::math::Vec3{1.9F, 2.4F, -6.3F}, config.terrain_seed + 307U, 3U),
    };
    return base + warp * 0.22F;
}

[[nodiscard]] float terrain_ridge_profile(float value, float sharpness) {
    return std::pow(std::max(1.0F - std::abs(value), 0.0F), sharpness);
}

[[nodiscard]] float terrain_continent_mask(const PlanetConfig& config,
                                           cubey::math::Vec3 sphere_normal) {
    const cubey::math::Vec3 p = terrain_domain_point(config, sphere_normal);
    const float continent =
        fbm(p * 0.52F + cubey::math::Vec3{2.3F, -1.7F, 4.1F}, config.terrain_seed + 211U, 5U);
    const float breakup =
        fbm(p * 1.48F + cubey::math::Vec3{-3.8F, 5.0F, 0.9F}, config.terrain_seed + 547U, 4U);
    const float shelf =
        fbm(p * 3.05F + cubey::math::Vec3{5.1F, 2.8F, -1.6F}, config.terrain_seed + 659U, 3U);
    const float shape = continent * 0.78F + breakup * 0.25F + shelf * 0.08F;
    return smootherstep((shape + 0.18F) / 0.46F);
}

[[nodiscard]] float terrain_mountain_belt(const PlanetConfig& config,
                                          cubey::math::Vec3 sphere_normal) {
    const cubey::math::Vec3 p = terrain_domain_point(config, sphere_normal);
    const float belt =
        fbm(p * 1.08F + cubey::math::Vec3{-6.5F, 1.2F, 3.7F}, config.terrain_seed + 811U, 5U);
    const float fold =
        fbm(p * 2.35F + cubey::math::Vec3{3.2F, 6.4F, -5.7F}, config.terrain_seed + 919U, 4U);
    return smootherstep((belt * 0.72F + fold * 0.24F + 0.08F) / 0.44F);
}

[[nodiscard]] float terrain_valley_network(const PlanetConfig& config,
                                           cubey::math::Vec3 sphere_normal) {
    const cubey::math::Vec3 p = terrain_domain_point(config, sphere_normal);
    const float primary =
        fbm(p * 2.55F + cubey::math::Vec3{6.8F, -4.1F, 2.3F}, config.terrain_seed + 1223U, 4U);
    const float secondary =
        fbm(p * 5.10F + cubey::math::Vec3{-1.2F, 8.4F, -5.6F}, config.terrain_seed + 1291U, 3U);
    const float channel = primary * 0.78F + secondary * 0.22F;
    return terrain_ridge_profile(channel * 1.35F, 2.9F);
}

[[nodiscard]] float terrain_land_mask(float height_above_sea_m, float height_scale_m) {
    return smootherstep((height_above_sea_m / std::max(height_scale_m, 1.0F) + 0.04F) / 0.13F);
}

[[nodiscard]] float terrain_temperature(const PlanetConfig& config,
                                        cubey::math::Vec3 sphere_normal,
                                        float normalized_elevation) {
    const cubey::math::Vec3 p = terrain_domain_point(config, sphere_normal);
    const float latitude = std::abs(sphere_normal.y);
    const float weather =
        fbm(p * 1.35F + cubey::math::Vec3{8.1F, -2.2F, 1.4F}, config.terrain_seed + 1409U, 3U);
    const float highland_cooling = std::max(normalized_elevation, 0.0F) * 0.48F;
    return clamp01(1.0F - latitude * 1.12F - highland_cooling + weather * 0.12F);
}

[[nodiscard]] float terrain_moisture(const PlanetConfig& config,
                                     cubey::math::Vec3 sphere_normal, float shoreline_mask,
                                     float normalized_elevation) {
    const cubey::math::Vec3 p = terrain_domain_point(config, sphere_normal);
    const float weather =
        fbm(p * 2.05F + cubey::math::Vec3{-1.5F, 7.6F, -4.2F}, config.terrain_seed + 1613U, 4U);
    const float latitude_rain = 1.0F - std::abs(sphere_normal.y) * 0.35F;
    return clamp01((weather * 0.5F + 0.5F) * 0.76F + shoreline_mask * 0.24F +
                   latitude_rain * 0.08F - std::max(normalized_elevation, 0.0F) * 0.18F);
}

[[nodiscard]] float terrain_roughness(PlanetSurfaceMaterial material, float normalized_slope,
                                      float moisture) {
    switch (material) {
    case PlanetSurfaceMaterial::DeepWater:
        return 0.25F;
    case PlanetSurfaceMaterial::ShallowWater:
        return 0.34F;
    case PlanetSurfaceMaterial::Beach:
        return 0.78F;
    case PlanetSurfaceMaterial::Lowland:
        return std::clamp(0.74F - moisture * 0.18F + normalized_slope * 0.10F, 0.45F, 0.85F);
    case PlanetSurfaceMaterial::Highland:
        return std::clamp(0.76F + normalized_slope * 0.16F, 0.62F, 0.95F);
    case PlanetSurfaceMaterial::Snow:
        return 0.58F;
    }
    return 0.7F;
}

[[nodiscard]] std::uint32_t hash_combine_u32(std::uint32_t hash, std::uint32_t value) {
    hash ^= value + 0x9e3779b9U + (hash << 6U) + (hash >> 2U);
    hash ^= hash >> 16U;
    hash *= 0x7feb352dU;
    hash ^= hash >> 15U;
    hash *= 0x846ca68bU;
    hash ^= hash >> 16U;
    return hash;
}

[[nodiscard]] std::uint32_t hash_float(std::uint32_t hash, float value) {
    return hash_combine_u32(hash, std::bit_cast<std::uint32_t>(value));
}

[[nodiscard]] std::uint32_t terrain_generator_revision(const PlanetConfig& config) {
    std::uint32_t hash = 0x62706c6dU;
    hash = hash_combine_u32(hash, config.terrain_enabled ? 1U : 0U);
    hash = hash_float(hash, config.terrain_height_scale_m);
    hash = hash_float(hash, config.terrain_noise_scale);
    hash = hash_combine_u32(hash, config.terrain_seed);
    hash = hash_float(hash, config.terrain_mid_detail_strength);
    hash = hash_float(hash, config.terrain_fine_detail_strength);
    hash = hash_float(hash, config.terrain_fine_detail_scale);
    hash = hash_float(hash, config.sea_level_m);
    hash = hash_float(hash, config.bathymetry_depth_scale_m);
    hash = hash_float(hash, config.shoreline_width_m);
    return hash != 0U ? hash : 1U;
}

void finalize_summary_coverage(PlanetSurfaceTileSummary& summary) {
    if (summary.sample_count == 0U) {
        summary.min_height_m = 0.0F;
        summary.max_height_m = 0.0F;
        summary.min_height_above_sea_m = 0.0F;
        summary.max_height_above_sea_m = 0.0F;
        summary.min_moisture = 0.0F;
        summary.max_moisture = 0.0F;
        summary.min_temperature = 0.0F;
        summary.max_temperature = 0.0F;
        summary.min_roughness = 0.0F;
        summary.max_roughness = 0.0F;
        summary.average_height_m = 0.0F;
        summary.average_height_above_sea_m = 0.0F;
        summary.average_moisture = 0.0F;
        summary.average_temperature = 0.0F;
        summary.average_roughness = 0.0F;
        summary.average_normalized_slope = 0.0F;
        summary.dominant_material = PlanetSurfaceMaterial::Lowland;
        return;
    }
    const float inv_sample_count = 1.0F / static_cast<float>(summary.sample_count);
    summary.average_height_m *= inv_sample_count;
    summary.average_height_above_sea_m *= inv_sample_count;
    summary.average_moisture *= inv_sample_count;
    summary.average_temperature *= inv_sample_count;
    summary.average_roughness *= inv_sample_count;
    summary.average_normalized_slope *= inv_sample_count;
    summary.land_coverage = std::clamp(summary.land_coverage * inv_sample_count, 0.0F, 1.0F);
    summary.water_coverage = std::clamp(summary.water_coverage * inv_sample_count, 0.0F, 1.0F);
    summary.shoreline_coverage =
        std::clamp(summary.shoreline_coverage * inv_sample_count, 0.0F, 1.0F);
    std::uint32_t dominant_count = 0U;
    for (std::uint32_t index = 0U; index < summary.material_counts.size(); ++index) {
        if (summary.material_counts[index] > dominant_count) {
            dominant_count = summary.material_counts[index];
            summary.dominant_material = static_cast<PlanetSurfaceMaterial>(index);
        }
    }
}

} // namespace

cubey::math::Vec3 planet_surface_cube_face_point(std::uint32_t face, float u, float v) {
    switch (face) {
    case 0:
        return {1.0F, v, -u};
    case 1:
        return {-1.0F, v, u};
    case 2:
        return {u, 1.0F, -v};
    case 3:
        return {u, -1.0F, v};
    case 4:
        return {u, v, 1.0F};
    case 5:
        return {-u, v, -1.0F};
    default:
        return {0.0F, 1.0F, 0.0F};
    }
}

cubey::math::DVec3 planet_surface_sphere_world_position_m(const PlanetConfig& config,
                                                          std::uint32_t face, float u, float v) {
    const cubey::math::Vec3 sphere_normal =
        glm::normalize(planet_surface_cube_face_point(face, u, v));
    return {
        static_cast<double>(sphere_normal.x) * static_cast<double>(config.radius_m),
        static_cast<double>(sphere_normal.y) * static_cast<double>(config.radius_m),
        static_cast<double>(sphere_normal.z) * static_cast<double>(config.radius_m),
    };
}

PlanetTerrainFeatureContext
planet_surface_terrain_feature_context(const PlanetConfig& config,
                                       cubey::math::Vec3 sphere_normal) {
    if (!config.terrain_enabled || config.terrain_height_scale_m <= 0.0F) {
        return {};
    }
    const float continent_mask = terrain_continent_mask(config, sphere_normal);
    const float mountain_belt = terrain_mountain_belt(config, sphere_normal);
    const float relief_gate = smootherstep((continent_mask - 0.24F) / 0.54F);
    const float plain_gate = smootherstep((continent_mask - 0.36F) / 0.42F) *
                             (1.0F - smootherstep((mountain_belt - 0.30F) / 0.42F));
    return {
        .domain_point = terrain_domain_point(config, sphere_normal),
        .continent_mask = continent_mask,
        .mountain_belt = mountain_belt,
        .valley_network = terrain_valley_network(config, sphere_normal),
        .relief_gate = relief_gate,
        .plain_gate = plain_gate,
        .land_mask = smootherstep((continent_mask - 0.30F) / 0.42F),
    };
}

float planet_surface_terrain_height_m(const PlanetConfig& config, cubey::math::Vec3 sphere_normal) {
    if (!config.terrain_enabled || config.terrain_height_scale_m <= 0.0F) {
        return 0.0F;
    }
    const PlanetTerrainFeatureContext features =
        planet_surface_terrain_feature_context(config, sphere_normal);
    const cubey::math::Vec3 p = features.domain_point;
    const float broad =
        fbm(p + cubey::math::Vec3{1.7F, -3.2F, 5.1F}, config.terrain_seed, 4U);
    const float lowland =
        fbm(p * 2.15F + cubey::math::Vec3{0.4F, 3.2F, -2.0F}, config.terrain_seed + 19U, 4U);
    const float ridge_source =
        fbm(p * 4.10F + cubey::math::Vec3{-4.0F, 2.4F, 8.5F}, config.terrain_seed + 37U, 5U);
    const float ridge_source_secondary =
        fbm(p * 7.20F + cubey::math::Vec3{2.1F, -8.2F, 4.7F}, config.terrain_seed + 41U, 4U);
    const float ridges = std::max(terrain_ridge_profile(ridge_source, 2.7F),
                                  terrain_ridge_profile(ridge_source_secondary, 2.3F) * 0.52F);
    const float basin =
        fbm(p * 1.18F + cubey::math::Vec3{5.7F, 0.3F, -6.1F}, config.terrain_seed + 73U, 4U);
    const float continent_mask = features.continent_mask;
    const float mountain_belt = features.mountain_belt;
    const float valleys = features.valley_network;
    const float shelf = smootherstep((continent_mask - 0.05F) / 0.46F);
    const float ocean_floor = lerp(-0.72F + broad * 0.08F + basin * 0.07F,
                                   -0.18F + broad * 0.10F + basin * 0.04F, shelf);
    const float land_base =
        (continent_mask - 0.38F) * 0.72F + broad * 0.11F + lowland * 0.16F;
    const float relief_gate = features.relief_gate;
    const float plain_gate = features.plain_gate;
    const float mountains =
        ridges * mountain_belt * relief_gate * config.terrain_mid_detail_strength * 1.22F;
    const float valley_cut =
        valleys * relief_gate * config.terrain_mid_detail_strength *
        (0.08F + mountain_belt * 0.22F + plain_gate * 0.06F);
    const float fine =
        fbm(p * config.terrain_fine_detail_scale + cubey::math::Vec3{6.3F, 1.1F, -7.4F},
            config.terrain_seed + 113U, 3U) *
        config.terrain_fine_detail_strength * (0.12F + relief_gate * 0.88F) *
        (0.45F + mountain_belt * 0.55F);
    const float height =
        (lerp(ocean_floor, land_base, continent_mask) + mountains - valley_cut + fine * 0.26F) *
        config.terrain_height_scale_m;
    return std::clamp(height, -config.terrain_height_scale_m, config.terrain_height_scale_m);
}

float planet_surface_height_above_sea_m(const PlanetConfig& config, float height_m) {
    return height_m - config.sea_level_m;
}

float planet_surface_water_depth_m(const PlanetConfig& config, float height_m) {
    return std::max(-planet_surface_height_above_sea_m(config, height_m), 0.0F);
}

float planet_surface_normalized_bathymetry(const PlanetConfig& config, float height_m) {
    return std::clamp(planet_surface_water_depth_m(config, height_m) /
                          std::max(config.bathymetry_depth_scale_m, 0.0001F),
                      0.0F, 1.0F);
}

float planet_surface_shoreline_mask(const PlanetConfig& config, float height_m) {
    const float distance_m = std::abs(planet_surface_height_above_sea_m(config, height_m));
    return 1.0F - std::clamp(distance_m / std::max(config.shoreline_width_m, 0.0001F), 0.0F,
                             1.0F);
}

PlanetSurfaceMaterial planet_surface_material(float height_above_sea_m,
                                              float water_depth_m, float shoreline_mask,
                                              float normalized_elevation,
                                              float normalized_slope, float moisture,
                                              float temperature) {
    if (height_above_sea_m <= 0.0F) {
        return water_depth_m > 1200.0F ? PlanetSurfaceMaterial::DeepWater
                                       : PlanetSurfaceMaterial::ShallowWater;
    }
    if (shoreline_mask > 0.32F && normalized_slope < 0.24F) {
        return PlanetSurfaceMaterial::Beach;
    }
    if (normalized_elevation > 0.68F ||
        (normalized_elevation > 0.42F && temperature < 0.30F + moisture * 0.08F &&
         normalized_slope < 0.30F)) {
        return PlanetSurfaceMaterial::Snow;
    }
    if (normalized_elevation > 0.22F || normalized_slope > 0.30F) {
        return PlanetSurfaceMaterial::Highland;
    }
    return PlanetSurfaceMaterial::Lowland;
}

cubey::math::Vec3 planet_surface_material_color(PlanetSurfaceMaterial material,
                                                float normalized_elevation,
                                                float normalized_slope, float moisture,
                                                float temperature) {
    const float elevation = std::clamp(normalized_elevation, -1.0F, 1.0F);
    const float slope = std::clamp(normalized_slope, 0.0F, 1.0F);
    const float wet = std::clamp(moisture, 0.0F, 1.0F);
    const float warm = std::clamp(temperature, 0.0F, 1.0F);
    switch (material) {
    case PlanetSurfaceMaterial::DeepWater:
        return {0.014F, 0.052F, 0.118F};
    case PlanetSurfaceMaterial::ShallowWater:
        return {0.038F, 0.155F, 0.205F};
    case PlanetSurfaceMaterial::Beach:
        return {0.560F, 0.492F, 0.315F};
    case PlanetSurfaceMaterial::Lowland: {
        const float blend = std::clamp((elevation + 0.08F) / 0.32F, 0.0F, 1.0F);
        const cubey::math::Vec3 dry{
            lerp(0.205F, 0.335F, blend),
            lerp(0.225F, 0.300F, blend),
            lerp(0.125F, 0.155F, blend),
        };
        const cubey::math::Vec3 green{
            lerp(0.060F, 0.115F, blend),
            lerp(0.185F, 0.320F, blend),
            lerp(0.100F, 0.110F, blend),
        };
        const float green_mix = std::clamp(wet * (0.45F + warm * 0.55F), 0.0F, 1.0F);
        return {
            lerp(dry.r, green.r, green_mix),
            lerp(dry.g, green.g, green_mix),
            lerp(dry.b, green.b, green_mix),
        };
    }
    case PlanetSurfaceMaterial::Highland: {
        const float height_blend = std::clamp((elevation - 0.22F) / 0.44F, 0.0F, 1.0F);
        const float rock_blend = std::max(height_blend, slope);
        return {
            lerp(0.215F, 0.500F, rock_blend),
            lerp(0.210F, 0.475F, rock_blend),
            lerp(0.185F, 0.430F, rock_blend),
        };
    }
    case PlanetSurfaceMaterial::Snow:
        return {0.720F, 0.745F, 0.790F};
    }
    return {0.12F, 0.28F, 0.10F};
}

PlanetSurfaceSample planet_surface_sample_field(const PlanetConfig& config, PlanetSurfacePatchId id,
                                                float u, float v) {
    const cubey::math::Vec3 sphere_normal =
        glm::normalize(planet_surface_cube_face_point(id.face, u, v));
    const float height_m = planet_surface_terrain_height_m(config, sphere_normal);
    const double radius = surface_radius_m(config, height_m);
    const cubey::math::DVec3 world_position_m{
        static_cast<double>(sphere_normal.x) * radius,
        static_cast<double>(sphere_normal.y) * radius,
        static_cast<double>(sphere_normal.z) * radius,
    };
    const cubey::math::Vec3 normal = terrain_normal(config, id, u, v, sphere_normal);
    const float normalized_elevation =
        config.terrain_height_scale_m > 0.0F
            ? std::clamp(height_m / config.terrain_height_scale_m, -1.0F, 1.0F)
            : 0.0F;
    const float height_above_sea = planet_surface_height_above_sea_m(config, height_m);
    const float water_depth = planet_surface_water_depth_m(config, height_m);
    const float normalized_bathymetry = planet_surface_normalized_bathymetry(config, height_m);
    const float shoreline_mask = planet_surface_shoreline_mask(config, height_m);
    const float slope = normalized_slope(sphere_normal, normal);
    const float land_mask = terrain_land_mask(height_above_sea, config.terrain_height_scale_m);
    const float temperature = terrain_temperature(config, sphere_normal, normalized_elevation);
    const float moisture =
        terrain_moisture(config, sphere_normal, shoreline_mask, normalized_elevation);
    const PlanetSurfaceMaterial material =
        planet_surface_material(height_above_sea, water_depth, shoreline_mask,
                                normalized_elevation, slope, moisture, temperature);
    return {
        .sphere_normal = sphere_normal,
        .normal = normal,
        .world_position_m = world_position_m,
        .height_m = height_m,
        .height_above_sea_m = height_above_sea,
        .water_depth_m = water_depth,
        .normalized_bathymetry = normalized_bathymetry,
        .shoreline_mask = shoreline_mask,
        .normalized_elevation = normalized_elevation,
        .normalized_slope = slope,
        .land_mask = land_mask,
        .moisture = moisture,
        .temperature = temperature,
        .roughness = terrain_roughness(material, slope, moisture),
        .material = material,
    };
}

PlanetSurfaceTileKey planet_surface_tile_key_from_patch_id(PlanetSurfacePatchId id) {
    return {
        .face = id.face,
        .level = id.level,
        .x = id.x,
        .y = id.y,
    };
}

PlanetSurfacePatchId planet_surface_patch_id_from_tile_key(PlanetSurfaceTileKey key) {
    return {
        .face = key.face,
        .level = key.level,
        .x = key.x,
        .y = key.y,
    };
}

PlanetSurfaceTilePayload make_planet_surface_tile_payload(const PlanetConfig& config,
                                                          PlanetSurfaceTileKey key,
                                                          std::uint32_t sample_resolution) {
    validate_planet_config(config);
    const std::uint32_t resolution = std::max(sample_resolution, 1U);
    const PlanetSurfacePatchId patch_id = planet_surface_patch_id_from_tile_key(key);
    const PlanetSurfacePatchBounds bounds = planet_surface_patch_bounds(config, patch_id);
    PlanetSurfaceTileSummary summary{};

    for (std::uint32_t y = 0; y <= resolution; ++y) {
        const float ty = static_cast<float>(y) / static_cast<float>(resolution);
        const float v = bounds.v0 + (bounds.v1 - bounds.v0) * ty;
        for (std::uint32_t x = 0; x <= resolution; ++x) {
            const float tx = static_cast<float>(x) / static_cast<float>(resolution);
            const float u = bounds.u0 + (bounds.u1 - bounds.u0) * tx;
            const PlanetSurfaceSample sample = planet_surface_sample_field(config, patch_id, u, v);
            summary.min_height_m = std::min(summary.min_height_m, sample.height_m);
            summary.max_height_m = std::max(summary.max_height_m, sample.height_m);
            summary.min_height_above_sea_m =
                std::min(summary.min_height_above_sea_m, sample.height_above_sea_m);
            summary.max_height_above_sea_m =
                std::max(summary.max_height_above_sea_m, sample.height_above_sea_m);
            summary.min_moisture = std::min(summary.min_moisture, sample.moisture);
            summary.max_moisture = std::max(summary.max_moisture, sample.moisture);
            summary.min_temperature = std::min(summary.min_temperature, sample.temperature);
            summary.max_temperature = std::max(summary.max_temperature, sample.temperature);
            summary.min_roughness = std::min(summary.min_roughness, sample.roughness);
            summary.max_roughness = std::max(summary.max_roughness, sample.roughness);
            summary.average_height_m += sample.height_m;
            summary.average_height_above_sea_m += sample.height_above_sea_m;
            summary.average_moisture += sample.moisture;
            summary.average_temperature += sample.temperature;
            summary.average_roughness += sample.roughness;
            summary.average_normalized_slope += sample.normalized_slope;
            summary.max_water_depth_m =
                std::max(summary.max_water_depth_m, sample.water_depth_m);
            summary.max_shoreline_mask =
                std::max(summary.max_shoreline_mask, sample.shoreline_mask);
            summary.land_coverage += sample.land_mask;
            summary.water_coverage += 1.0F - sample.land_mask;
            summary.shoreline_coverage += sample.shoreline_mask;
            summary.max_normalized_slope =
                std::max(summary.max_normalized_slope, sample.normalized_slope);
            const std::uint32_t material_index = static_cast<std::uint32_t>(sample.material);
            summary.material_mask |= 1U << material_index;
            if (material_index < summary.material_counts.size()) {
                ++summary.material_counts[material_index];
            }
            ++summary.sample_count;
        }
    }
    finalize_summary_coverage(summary);

    return {
        .key = key,
        .bounds = bounds,
        .source = PlanetSurfaceTileSource::Procedural,
        .generator_revision = terrain_generator_revision(config),
        .summary = summary,
    };
}

} // namespace cubey::projects::planet
