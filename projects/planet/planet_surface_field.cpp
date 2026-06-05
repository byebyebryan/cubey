#include "planet_surface_field.h"

#include <algorithm>
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

float planet_surface_terrain_height_m(const PlanetConfig& config, cubey::math::Vec3 sphere_normal) {
    if (!config.terrain_enabled || config.terrain_height_scale_m <= 0.0F) {
        return 0.0F;
    }
    const cubey::math::Vec3 p = sphere_normal * config.terrain_noise_scale;
    const float broad = fbm(p + cubey::math::Vec3{1.7F, -3.2F, 5.1F}, config.terrain_seed, 4U);
    const float ridge_source =
        fbm(p * 2.35F + cubey::math::Vec3{-4.0F, 2.4F, 8.5F}, config.terrain_seed + 37U, 5U);
    const float mid =
        ((1.0F - std::abs(ridge_source)) * 2.0F - 1.0F) * config.terrain_mid_detail_strength;
    const float fine =
        fbm(p * config.terrain_fine_detail_scale + cubey::math::Vec3{6.3F, 1.1F, -7.4F},
            config.terrain_seed + 113U, 3U) *
        config.terrain_fine_detail_strength;
    const float height = (broad * 0.58F + mid + fine) * config.terrain_height_scale_m;
    return std::clamp(height, -config.terrain_height_scale_m, config.terrain_height_scale_m);
}

PlanetSurfaceMaterial planet_surface_material(float normalized_elevation, float normalized_slope) {
    if (normalized_elevation < -0.15F) {
        return PlanetSurfaceMaterial::Water;
    }
    if (normalized_elevation > 0.66F ||
        (normalized_elevation > 0.45F && normalized_slope < 0.22F)) {
        return PlanetSurfaceMaterial::Snow;
    }
    if (normalized_elevation > 0.22F || normalized_slope > 0.30F) {
        return PlanetSurfaceMaterial::Highland;
    }
    return PlanetSurfaceMaterial::Lowland;
}

cubey::math::Vec3 planet_surface_material_color(PlanetSurfaceMaterial material,
                                                float normalized_elevation,
                                                float normalized_slope) {
    const float elevation = std::clamp(normalized_elevation, -1.0F, 1.0F);
    const float slope = std::clamp(normalized_slope, 0.0F, 1.0F);
    switch (material) {
    case PlanetSurfaceMaterial::Water:
        return {0.035F, 0.105F, 0.190F};
    case PlanetSurfaceMaterial::Lowland: {
        const float blend = std::clamp((elevation + 0.15F) / 0.37F, 0.0F, 1.0F);
        return {
            lerp(0.070F, 0.145F, blend),
            lerp(0.170F, 0.310F, blend),
            lerp(0.130F, 0.105F, blend),
        };
    }
    case PlanetSurfaceMaterial::Highland: {
        const float height_blend = std::clamp((elevation - 0.22F) / 0.44F, 0.0F, 1.0F);
        const float rock_blend = std::max(height_blend, slope);
        return {
            lerp(0.170F, 0.460F, rock_blend),
            lerp(0.235F, 0.395F, rock_blend),
            lerp(0.130F, 0.310F, rock_blend),
        };
    }
    case PlanetSurfaceMaterial::Snow:
        return {0.66F, 0.70F, 0.76F};
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
    const float slope = normalized_slope(sphere_normal, normal);
    return {
        .sphere_normal = sphere_normal,
        .normal = normal,
        .world_position_m = world_position_m,
        .height_m = height_m,
        .normalized_elevation = normalized_elevation,
        .normalized_slope = slope,
        .material = planet_surface_material(normalized_elevation, slope),
    };
}

} // namespace cubey::projects::planet
