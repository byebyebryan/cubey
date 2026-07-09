#include <cubey/render/atmosphere_night_sky_atlas.h>

#include <cubey/procedural/artifact_metadata.h>
#include <cubey/procedural/hash.h>
#include <cubey/procedural/noise.h>
#include <cubey/procedural/seed.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace cubey::render {
namespace {

struct Vec2 {
    float x = 0.0F;
    float y = 0.0F;
};

struct Vec3 {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
};

[[nodiscard]] float clamp01(float value) {
    return std::clamp(value, 0.0F, 1.0F);
}

[[nodiscard]] float mix(float a, float b, float t) {
    return a + (b - a) * t;
}

[[nodiscard]] Vec3 mix(Vec3 a, Vec3 b, float t) {
    return {
        mix(a.x, b.x, t),
        mix(a.y, b.y, t),
        mix(a.z, b.z, t),
    };
}

[[nodiscard]] float smoothstep(float edge0, float edge1, float value) {
    const float t = clamp01((value - edge0) / (edge1 - edge0));
    return t * t * (3.0F - 2.0F * t);
}

[[nodiscard]] Vec3 operator+(Vec3 lhs, Vec3 rhs) {
    return {lhs.x + rhs.x, lhs.y + rhs.y, lhs.z + rhs.z};
}

[[nodiscard]] Vec3 operator-(Vec3 lhs, Vec3 rhs) {
    return {lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z};
}

[[nodiscard]] Vec3 operator*(Vec3 value, float scale) {
    return {value.x * scale, value.y * scale, value.z * scale};
}

[[nodiscard]] Vec3 operator*(Vec3 lhs, Vec3 rhs) {
    return {lhs.x * rhs.x, lhs.y * rhs.y, lhs.z * rhs.z};
}

[[nodiscard]] Vec3 operator/(Vec3 value, float scale) {
    return {value.x / scale, value.y / scale, value.z / scale};
}

[[nodiscard]] float dot(Vec3 lhs, Vec3 rhs) {
    return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z;
}

[[nodiscard]] Vec3 cross(Vec3 lhs, Vec3 rhs) {
    return {
        lhs.y * rhs.z - lhs.z * rhs.y,
        lhs.z * rhs.x - lhs.x * rhs.z,
        lhs.x * rhs.y - lhs.y * rhs.x,
    };
}

[[nodiscard]] float length(Vec3 value) {
    return std::sqrt(dot(value, value));
}

[[nodiscard]] Vec3 normalize(Vec3 value) {
    const float len = length(value);
    if (len <= std::numeric_limits<float>::epsilon()) {
        return {0.0F, 1.0F, 0.0F};
    }
    return value / len;
}

[[nodiscard]] float luminance(Vec3 color) {
    return color.x * 0.2126F + color.y * 0.7152F + color.z * 0.0722F;
}

[[nodiscard]] std::uint32_t hash_u32(std::uint32_t value) {
    value ^= value >> 16U;
    value *= 0x7feb352dU;
    value ^= value >> 15U;
    value *= 0x846ca68bU;
    value ^= value >> 16U;
    return value;
}

[[nodiscard]] float hash_to_unit(std::uint32_t value) {
    constexpr float kInv24Bit = 1.0F / 16'777'215.0F;
    return static_cast<float>(hash_u32(value) >> 8U) * kInv24Bit;
}

[[nodiscard]] float lattice_hash(int x, int y, int z, std::uint32_t seed) {
    return hash_to_unit(seed ^ (static_cast<std::uint32_t>(x) * 0x9e3779b9U) ^
                        (static_cast<std::uint32_t>(y) * 0x85ebca6bU) ^
                        (static_cast<std::uint32_t>(z) * 0xc2b2ae35U));
}

[[nodiscard]] float value_noise(Vec3 position, std::uint32_t seed) {
    const int x0 = static_cast<int>(std::floor(position.x));
    const int y0 = static_cast<int>(std::floor(position.y));
    const int z0 = static_cast<int>(std::floor(position.z));
    const float tx = position.x - static_cast<float>(x0);
    const float ty = position.y - static_cast<float>(y0);
    const float tz = position.z - static_cast<float>(z0);
    const float sx = tx * tx * (3.0F - 2.0F * tx);
    const float sy = ty * ty * (3.0F - 2.0F * ty);
    const float sz = tz * tz * (3.0F - 2.0F * tz);

    const float c000 = lattice_hash(x0, y0, z0, seed);
    const float c100 = lattice_hash(x0 + 1, y0, z0, seed);
    const float c010 = lattice_hash(x0, y0 + 1, z0, seed);
    const float c110 = lattice_hash(x0 + 1, y0 + 1, z0, seed);
    const float c001 = lattice_hash(x0, y0, z0 + 1, seed);
    const float c101 = lattice_hash(x0 + 1, y0, z0 + 1, seed);
    const float c011 = lattice_hash(x0, y0 + 1, z0 + 1, seed);
    const float c111 = lattice_hash(x0 + 1, y0 + 1, z0 + 1, seed);
    const float x00 = mix(c000, c100, sx);
    const float x10 = mix(c010, c110, sx);
    const float x01 = mix(c001, c101, sx);
    const float x11 = mix(c011, c111, sx);
    return mix(mix(x00, x10, sy), mix(x01, x11, sy), sz);
}

[[nodiscard]] float fbm(Vec3 position, std::uint32_t seed, int octaves) {
    float value = 0.0F;
    float amplitude = 0.5F;
    float frequency = 1.0F;
    float norm = 0.0F;
    for (int octave = 0; octave < octaves; ++octave) {
        value += amplitude *
                 value_noise({position.x * frequency, position.y * frequency,
                              position.z * frequency},
                             seed + static_cast<std::uint32_t>(octave) * 101U);
        norm += amplitude;
        amplitude *= 0.52F;
        frequency *= 2.07F;
    }
    return norm > 0.0F ? value / norm : 0.0F;
}

[[nodiscard]] float ridged_fbm(Vec3 position, std::uint32_t seed, int octaves) {
    float value = 0.0F;
    float amplitude = 0.5F;
    float frequency = 1.0F;
    float norm = 0.0F;
    for (int octave = 0; octave < octaves; ++octave) {
        const float noise =
            value_noise({position.x * frequency, position.y * frequency, position.z * frequency},
                        seed + static_cast<std::uint32_t>(octave) * 157U);
        value += amplitude * (1.0F - std::abs(noise * 2.0F - 1.0F));
        norm += amplitude;
        amplitude *= 0.50F;
        frequency *= 2.13F;
    }
    return norm > 0.0F ? value / norm : 0.0F;
}

[[nodiscard]] float angle_delta(float value, float center) {
    return std::atan2(std::sin(value - center), std::cos(value - center));
}

[[nodiscard]] Vec3 cubemap_direction(std::uint32_t face, std::uint32_t x, std::uint32_t y,
                                     std::uint32_t extent) {
    const float u = ((static_cast<float>(x) + 0.5F) / static_cast<float>(extent)) * 2.0F - 1.0F;
    const float v = ((static_cast<float>(y) + 0.5F) / static_cast<float>(extent)) * 2.0F - 1.0F;
    switch (face) {
    case 0:
        return normalize({1.0F, -v, -u});
    case 1:
        return normalize({-1.0F, -v, u});
    case 2:
        return normalize({u, 1.0F, v});
    case 3:
        return normalize({u, -1.0F, -v});
    case 4:
        return normalize({u, -v, 1.0F});
    default:
        return normalize({-u, -v, -1.0F});
    }
}

struct GalacticFrame {
    Vec3 pole{};
    Vec3 center{};
    Vec3 tangent{};
};

struct GalacticSample {
    float longitude = 0.0F;
    float latitude = 0.0F;
    float center_axis = 0.0F;
    float tangent_axis = 0.0F;
    float pole_axis = 0.0F;
    Vec3 local_direction{};
};

struct SkyStamp {
    float longitude = 0.0F;
    float latitude = 0.0F;
    float longitude_radius = 0.35F;
    float latitude_radius = 0.045F;
    float strength = 1.0F;
};

struct ProceduralMilkyWayLayers {
    Vec3 stellar_emission{};
    float dust_tau = 0.0F;
    Vec3 star_clouds{};
    Vec3 hii_emission{};
    float speckles = 0.0F;
    Vec3 final_rgb{};
};

[[nodiscard]] GalacticFrame galactic_frame() {
    const Vec3 pole = normalize({0.31F, 0.84F, 0.44F});
    const Vec3 center_hint = normalize({-0.45F, -0.12F, -0.89F});
    const Vec3 center = normalize(center_hint - pole * dot(center_hint, pole));
    return {
        .pole = pole,
        .center = center,
        .tangent = normalize(cross(pole, center)),
    };
}

[[nodiscard]] GalacticSample galactic_sample(Vec3 direction) {
    const GalacticFrame frame = galactic_frame();
    const float center_axis = dot(direction, frame.center);
    const float tangent_axis = dot(direction, frame.tangent);
    const float pole_axis = dot(direction, frame.pole);
    return {
        .longitude = std::atan2(tangent_axis, center_axis),
        .latitude = std::asin(std::clamp(pole_axis, -1.0F, 1.0F)),
        .center_axis = center_axis,
        .tangent_axis = tangent_axis,
        .pole_axis = pole_axis,
        .local_direction = {center_axis, tangent_axis, pole_axis},
    };
}

[[nodiscard]] float elliptical_stamp(const GalacticSample& sample, const SkyStamp& stamp) {
    const float dl = angle_delta(sample.longitude, stamp.longitude) / stamp.longitude_radius;
    const float db = (sample.latitude - stamp.latitude) / stamp.latitude_radius;
    return stamp.strength * std::exp(-(dl * dl + db * db));
}

[[nodiscard]] float sparse_speckles(Vec3 position, std::uint32_t seed) {
    const float coarse = value_noise(position, seed);
    const float fine = value_noise({position.x * 2.13F + 17.0F, position.y * 2.13F - 11.0F,
                                    position.z * 2.13F + 5.0F},
                                   seed + 31U);
    return smoothstep(0.76F, 0.985F, coarse) * std::pow(fine, 5.0F);
}

[[nodiscard]] ProceduralMilkyWayLayers procedural_milky_way_layers_v1(Vec3 direction,
                                                                      std::uint32_t seed) {
    GalacticSample sample = galactic_sample(direction);
    const Vec3 domain{
        sample.local_direction.x * 3.0F + static_cast<float>(seed % 37U) * 0.17F,
        sample.local_direction.y * 3.0F + static_cast<float>(seed % 53U) * 0.11F,
        sample.local_direction.z * 11.5F + static_cast<float>(seed % 71U) * 0.07F,
    };
    const Vec3 filament_domain{
        sample.local_direction.x * 7.4F + static_cast<float>(seed % 97U) * 0.09F,
        sample.local_direction.y * 7.4F - static_cast<float>(seed % 89U) * 0.08F,
        sample.local_direction.z * 21.0F + static_cast<float>(seed % 79U) * 0.05F,
    };

    const float warp =
        (fbm(domain, seed + 17U, 2) - 0.5F) * 0.060F +
        (ridged_fbm(filament_domain, seed + 29U, 1) - 0.5F) * 0.024F;
    const float latitude_warp =
        sample.latitude + warp * smoothstep(0.0F, 0.58F, 1.0F - std::abs(sample.pole_axis));
    const float center_longitude = angle_delta(sample.longitude, 0.0F);
    const float anticenter_longitude =
        angle_delta(sample.longitude, std::numbers::pi_v<float>);
    const float center_weight = std::exp(-(center_longitude * center_longitude) / 0.56F);
    const float anticenter_weight =
        std::exp(-(anticenter_longitude * anticenter_longitude) / 0.42F);
    const float band_sigma = std::clamp(mix(0.045F, 0.115F, center_weight) -
                                            anticenter_weight * 0.010F,
                                        0.034F, 0.125F);
    const float broad_sigma = band_sigma * 2.85F;
    const float inner_sigma = band_sigma * 0.46F;
    const float band =
        std::exp(-(latitude_warp * latitude_warp) / (2.0F * band_sigma * band_sigma));
    const float broad_band =
        std::exp(-(latitude_warp * latitude_warp) / (2.0F * broad_sigma * broad_sigma));
    const float inner_band =
        std::exp(-(latitude_warp * latitude_warp) / (2.0F * inner_sigma * inner_sigma));
    const float core =
        std::exp(-(center_longitude * center_longitude) / 0.16F -
                 (latitude_warp * latitude_warp) / 0.020F);
    const float bar =
        std::exp(-(angle_delta(sample.longitude, -0.24F) *
                   angle_delta(sample.longitude, -0.24F)) /
                     0.40F -
                 ((latitude_warp + 0.018F) * (latitude_warp + 0.018F)) / 0.018F);

    const float cloud_noise = fbm({domain.x * 1.55F, domain.y * 1.55F, domain.z * 0.58F},
                                  seed + 91U, 3);
    const float fine_clouds =
        ridged_fbm({filament_domain.x * 1.25F, filament_domain.y * 1.25F,
                    filament_domain.z * 0.55F},
                   seed + 151U, 3);
    const float dust_noise = fbm({domain.x * 2.3F + 4.0F, domain.y * 2.3F - 2.0F,
                                  domain.z * 0.74F + 7.0F},
                                 seed + 211U, 3);
    const float filament_noise =
        ridged_fbm({filament_domain.x * 1.7F + 3.0F, filament_domain.y * 1.7F - 7.0F,
                    filament_domain.z * 0.70F + 2.0F},
                   seed + 257U, 3);

    static constexpr std::array kCloudStamps{
        SkyStamp{-1.20F, 0.030F, 0.34F, 0.055F, 0.85F},
        SkyStamp{-0.78F, -0.018F, 0.26F, 0.044F, 0.90F},
        SkyStamp{0.02F, 0.030F, 0.34F, 0.065F, 1.45F},
        SkyStamp{1.08F, 0.014F, 0.35F, 0.052F, 0.80F},
        SkyStamp{2.10F, -0.008F, 0.44F, 0.065F, 0.46F},
    };
    static constexpr std::array kDustStamps{
        SkyStamp{-0.36F, 0.048F, 0.14F, 0.030F, 0.44F},
        SkyStamp{0.10F, 0.070F, 0.16F, 0.036F, 0.64F},
        SkyStamp{0.35F, -0.058F, 0.20F, 0.038F, 0.52F},
        SkyStamp{1.42F, -0.022F, 0.24F, 0.043F, 0.34F},
    };
    static constexpr std::array kHiiStamps{
        SkyStamp{-0.96F, 0.024F, 0.12F, 0.026F, 0.65F},
        SkyStamp{-0.08F, 0.052F, 0.13F, 0.030F, 0.78F},
        SkyStamp{0.30F, -0.018F, 0.10F, 0.024F, 0.48F},
        SkyStamp{0.98F, 0.030F, 0.15F, 0.030F, 0.56F},
    };

    float cloud_knots = 0.0F;
    for (const SkyStamp& stamp : kCloudStamps) {
        cloud_knots += elliptical_stamp(sample, stamp);
    }
    float dust_stamps = 0.0F;
    for (const SkyStamp& stamp : kDustStamps) {
        dust_stamps += elliptical_stamp(sample, stamp);
    }
    float hii_stamps = 0.0F;
    for (const SkyStamp& stamp : kHiiStamps) {
        hii_stamps += elliptical_stamp(sample, stamp);
    }

    const float branch =
        std::sin(sample.longitude * 3.0F + (dust_noise - 0.5F) * 2.5F) * 0.035F +
        std::sin(sample.longitude * 8.0F + (filament_noise - 0.5F) * 3.2F) * 0.012F;
    const float lane_width = mix(0.012F, 0.045F, dust_noise);
    const float primary_dust =
        std::exp(-std::abs(latitude_warp - branch) / lane_width) *
        smoothstep(0.10F, 0.94F, band);
    const float secondary_center =
        -0.065F + std::sin(sample.longitude * 2.0F + (cloud_noise - 0.5F) * 2.0F) * 0.020F;
    const float secondary_dust =
        std::exp(-std::abs(latitude_warp - secondary_center) / 0.034F) *
        smoothstep(0.14F, 0.80F, band) * smoothstep(1.85F, 0.20F, std::abs(sample.longitude));
    const float core_dust =
        std::exp(-std::abs(latitude_warp + 0.043F) / 0.032F) *
        std::exp(-(center_longitude * center_longitude) / 0.58F);

    const float base_dust = broad_band * mix(0.10F, 0.34F, dust_noise);
    const float filament_dust = primary_dust * mix(0.42F, 0.88F, filament_noise);
    const float dust_tau = std::clamp(base_dust + filament_dust + secondary_dust * 0.46F +
                                          core_dust * 0.76F + dust_stamps * 0.50F,
                                      0.0F, 2.35F);

    const Vec3 band_color = {0.70F, 0.78F, 1.0F};
    const Vec3 core_color = {1.0F, 0.80F, 0.52F};
    const Vec3 cloud_color = mix({0.72F, 0.78F, 1.0F}, {0.95F, 0.91F, 0.78F},
                                 clamp01(center_weight * 0.72F + cloud_knots * 0.10F));
    const float core_emission = core * 0.026F + bar * 0.010F;
    ProceduralMilkyWayLayers layers;
    layers.stellar_emission =
        mix(band_color, core_color, clamp01(core * 1.4F + bar * 0.55F)) *
        (broad_band * 0.0010F + band * 0.0036F + inner_band * 0.0023F + core_emission);

    const float cloud_field =
        band * mix(0.0012F, 0.0078F, cloud_noise) * mix(0.65F, 1.52F, fine_clouds) +
        cloud_knots * 0.0068F + center_weight * inner_band * 0.0034F;
    layers.star_clouds = cloud_color * cloud_field;

    const float speckle_mask = smoothstep(0.08F, 0.92F, band);
    layers.speckles =
        sparse_speckles({sample.local_direction.x * 52.0F + 12.0F,
                         sample.local_direction.y * 52.0F - 4.0F,
                         sample.local_direction.z * 72.0F + 9.0F},
                        seed + 401U) *
        speckle_mask * mix(0.0008F, 0.0042F, clamp01(cloud_noise + cloud_knots * 0.18F));

    const float hii_density =
        hii_stamps * smoothstep(0.05F, 0.70F, band) * mix(0.35F, 1.0F, cloud_noise);
    layers.hii_emission = Vec3{1.0F, 0.30F, 0.16F} * hii_density * 0.0038F;

    const Vec3 extinction{
        std::exp(-dust_tau * 0.58F),
        std::exp(-dust_tau * 0.86F),
        std::exp(-dust_tau * 1.22F),
    };
    const Vec3 speckle_color = mix({0.65F, 0.72F, 1.0F}, {1.0F, 0.84F, 0.62F},
                                   clamp01(center_weight * 0.55F + cloud_noise * 0.25F));
    layers.final_rgb =
        (layers.stellar_emission + layers.star_clouds + speckle_color * layers.speckles) *
            extinction +
        layers.hii_emission;
    layers.dust_tau = dust_tau;
    return layers;
}

[[nodiscard]] std::uint32_t derived_seed32(std::uint32_t seed, std::string_view domain,
                                           std::uint64_t salt = 0U) {
    return static_cast<std::uint32_t>(
        cubey::procedural::derive_seed(static_cast<std::uint64_t>(seed), domain, salt));
}

[[nodiscard]] float shared_fbm_signed(Vec3 position, std::uint32_t seed, std::uint32_t octaves,
                                      float lacunarity = 2.03F, float gain = 0.52F) {
    return cubey::procedural::fbm_3d(
        position.x, position.y, position.z, seed,
        cubey::procedural::Fbm3DConfig{
            .octaves = octaves,
            .lacunarity = lacunarity,
            .gain = gain,
            .initial_amplitude = 0.5F,
            .seed_stride = 1013U,
        });
}

[[nodiscard]] float shared_fbm01(Vec3 position, std::uint32_t seed, std::uint32_t octaves,
                                 float lacunarity = 2.03F, float gain = 0.52F) {
    return clamp01(shared_fbm_signed(position, seed, octaves, lacunarity, gain) * 0.5F + 0.5F);
}

[[nodiscard]] float shared_ridged_fbm(Vec3 position, std::uint32_t seed, std::uint32_t octaves,
                                      float lacunarity = 2.08F, float gain = 0.52F) {
    return cubey::procedural::ridged_fbm_3d(
        position.x, position.y, position.z, seed,
        cubey::procedural::Fbm3DConfig{
            .octaves = octaves,
            .lacunarity = lacunarity,
            .gain = gain,
            .initial_amplitude = 0.5F,
            .seed_stride = 1019U,
        });
}

[[nodiscard]] Vec3 scaled(Vec3 value, float scale) {
    return {value.x * scale, value.y * scale, value.z * scale};
}

[[nodiscard]] int positive_mod(int value, int divisor) {
    const int result = value % divisor;
    return result < 0 ? result + divisor : result;
}

[[nodiscard]] float procedural_hii_cells(float longitude, float latitude, float disk_gate,
                                         float cloud_gate, std::uint32_t seed) {
    constexpr int kLongitudeCells = 56;
    constexpr int kLatitudeCells = 8;
    constexpr float kLatitudeSpan = 0.48F;
    const float u = (longitude + std::numbers::pi_v<float>) /
                    (2.0F * std::numbers::pi_v<float>) *
                    static_cast<float>(kLongitudeCells);
    const float v = (latitude / kLatitudeSpan + 0.5F) * static_cast<float>(kLatitudeCells);
    const int base_x = static_cast<int>(std::floor(u));
    const int base_y = static_cast<int>(std::floor(v));
    float density = 0.0F;
    for (int oy = -1; oy <= 1; ++oy) {
        const int y = base_y + oy;
        if (y < 0 || y >= kLatitudeCells) {
            continue;
        }
        for (int ox = -1; ox <= 1; ++ox) {
            const int x = positive_mod(base_x + ox, kLongitudeCells);
            const std::uint32_t index =
                static_cast<std::uint32_t>(y * kLongitudeCells + x);
            const float active =
                cubey::procedural::random01(seed, "milky-way-v2.hii", index, 0U);
            if (active < 0.70F) {
                continue;
            }
            const float center_u =
                static_cast<float>(x) +
                cubey::procedural::random01(seed, "milky-way-v2.hii", index, 1U);
            const float center_v =
                static_cast<float>(y) +
                cubey::procedural::random01(seed, "milky-way-v2.hii", index, 2U);
            float dx = u - center_u;
            dx -= std::round(dx / static_cast<float>(kLongitudeCells)) *
                  static_cast<float>(kLongitudeCells);
            const float dy = v - center_v;
            const float radius_u =
                mix(0.14F, 0.38F,
                    cubey::procedural::random01(seed, "milky-way-v2.hii", index, 3U));
            const float radius_v =
                mix(0.08F, 0.22F,
                    cubey::procedural::random01(seed, "milky-way-v2.hii", index, 4U));
            const float strength =
                smoothstep(0.70F, 1.0F, active) *
                mix(0.55F, 1.35F,
                    cubey::procedural::random01(seed, "milky-way-v2.hii", index, 5U));
            density += strength *
                       std::exp(-((dx * dx) / (radius_u * radius_u) +
                                  (dy * dy) / (radius_v * radius_v)));
        }
    }
    return density * smoothstep(0.18F, 0.86F, disk_gate) * smoothstep(0.08F, 0.62F, cloud_gate);
}

[[nodiscard]] float shared_sparse_speckles(Vec3 position, std::uint32_t seed) {
    const float coarse = shared_fbm01(position, derived_seed32(seed, "milky-way-v2.speckle.coarse"),
                                     2U, 2.31F, 0.48F);
    const float fine =
        shared_fbm01({position.x * 2.17F + 9.0F, position.y * 2.17F - 3.0F,
                      position.z * 2.17F + 15.0F},
                     derived_seed32(seed, "milky-way-v2.speckle.fine"), 2U, 2.17F, 0.50F);
    return smoothstep(0.67F, 0.91F, coarse) * std::pow(fine, 6.0F);
}

[[nodiscard]] ProceduralMilkyWayLayers procedural_milky_way_layers_v2(Vec3 direction,
                                                                      std::uint32_t seed) {
    const GalacticSample sample = galactic_sample(direction);
    const Vec3 local = sample.local_direction;
    const float disk_gate = smoothstep(0.0F, 0.62F, 1.0F - std::abs(sample.pole_axis));
    const Vec3 macro_domain = scaled(local, 2.45F);
    const Vec3 detail_domain = {local.x * 8.7F, local.y * 8.7F, local.z * 19.0F};
    const float warp_lat =
        shared_fbm_signed(macro_domain, derived_seed32(seed, "milky-way-v2.warp-lat"), 3U) *
            0.062F +
        (shared_ridged_fbm(scaled(detail_domain, 0.46F),
                           derived_seed32(seed, "milky-way-v2.warp-ridge"), 2U) -
         0.5F) *
            0.030F;
    const float warp_lon =
        shared_fbm_signed({macro_domain.y + 11.0F, macro_domain.z - 7.0F,
                           macro_domain.x + 3.0F},
                          derived_seed32(seed, "milky-way-v2.warp-lon"), 3U) *
        0.18F;
    const float latitude_warp = sample.latitude + warp_lat * disk_gate;
    const float longitude_warp = sample.longitude + warp_lon * disk_gate;
    const float center_longitude = angle_delta(longitude_warp, 0.0F);
    const float anticenter_longitude =
        angle_delta(longitude_warp, std::numbers::pi_v<float>);
    const float center_weight = std::exp(-(center_longitude * center_longitude) / 0.50F);
    const float anticenter_weight =
        std::exp(-(anticenter_longitude * anticenter_longitude) / 0.42F);

    const float disk_sigma =
        std::clamp(mix(0.048F, 0.100F, center_weight) - anticenter_weight * 0.006F,
                   0.038F, 0.110F);
    const float thin_sigma = disk_sigma * 0.52F;
    const float broad_sigma = disk_sigma * 2.35F;
    const float broad_disk =
        std::exp(-(latitude_warp * latitude_warp) / (2.0F * broad_sigma * broad_sigma));
    const float thin_disk =
        std::exp(-(latitude_warp * latitude_warp) / (2.0F * thin_sigma * thin_sigma));
    const float inner_disk =
        std::exp(-(latitude_warp * latitude_warp) / (2.0F * (disk_sigma * 0.35F) *
                                                     (disk_sigma * 0.35F)));
    const float bulge =
        std::exp(-(center_longitude * center_longitude) / 0.18F -
                 (latitude_warp * latitude_warp) / 0.028F);
    const float bar_longitude = angle_delta(longitude_warp, -0.30F);
    const float bar =
        std::exp(-(bar_longitude * bar_longitude) / 0.54F -
                 ((latitude_warp + 0.018F) * (latitude_warp + 0.018F)) / 0.022F);

    const float arm_noise =
        shared_fbm_signed(scaled(macro_domain, 1.35F),
                          derived_seed32(seed, "milky-way-v2.arm-noise"), 3U) *
        1.40F;
    const float arm_wave_a =
        1.0F -
        std::abs(std::sin(longitude_warp * 2.4F + latitude_warp * 10.0F + arm_noise));
    const float arm_wave_b =
        1.0F -
        std::abs(std::sin(longitude_warp * 4.8F - latitude_warp * 7.0F + arm_noise * 0.63F));
    const float arm_probability =
        smoothstep(0.42F, 0.92F, std::max(arm_wave_a * 0.82F, arm_wave_b * 0.56F));

    const float dust_noise =
        shared_fbm01({detail_domain.x * 0.72F + 4.0F, detail_domain.y * 0.72F - 8.0F,
                      detail_domain.z * 0.52F + 2.0F},
                     derived_seed32(seed, "milky-way-v2.dust-base"), 4U, 2.11F, 0.54F);
    const float dust_ridge =
        shared_ridged_fbm({detail_domain.x * 1.28F - 5.0F, detail_domain.y * 1.28F + 6.0F,
                           detail_domain.z * 0.70F - 1.0F},
                          derived_seed32(seed, "milky-way-v2.dust-ridge"), 4U, 2.16F, 0.50F);
    const float primary_lane_center =
        std::sin(longitude_warp * 2.35F + arm_noise * 0.82F) * 0.036F +
        shared_fbm_signed({macro_domain.x * 1.8F, macro_domain.y * 1.8F,
                           macro_domain.z * 0.8F},
                          derived_seed32(seed, "milky-way-v2.lane-center"), 2U) *
            0.030F;
    const float secondary_lane_center =
        -0.060F + std::sin(longitude_warp * 1.55F - arm_noise * 0.58F) * 0.030F;
    const float primary_lane_width = mix(0.010F, 0.035F, dust_noise);
    const float primary_lane =
        std::exp(-std::abs(latitude_warp - primary_lane_center) / primary_lane_width) *
        smoothstep(0.12F, 0.88F, thin_disk);
    const float secondary_lane =
        std::exp(-std::abs(latitude_warp - secondary_lane_center) / 0.035F) *
        smoothstep(0.08F, 0.76F, broad_disk) *
        smoothstep(1.95F, 0.24F, std::abs(center_longitude));
    const float core_lane =
        std::exp(-std::abs(latitude_warp + 0.035F) / 0.030F) *
        std::exp(-(center_longitude * center_longitude) / 0.52F);
    const float dust_tau =
        std::clamp(broad_disk * mix(0.04F, 0.18F, dust_noise) +
                       primary_lane * mix(0.35F, 1.05F, dust_ridge) +
                       secondary_lane * 0.38F + core_lane * 0.70F +
                       arm_probability * thin_disk * dust_ridge * 0.24F,
                   0.0F, 2.45F);

    const float cloud_noise =
        shared_fbm01({detail_domain.x * 1.10F, detail_domain.y * 1.10F,
                      detail_domain.z * 0.62F},
                     derived_seed32(seed, "milky-way-v2.cloud-base"), 4U, 2.07F, 0.53F);
    const float cloud_ridge =
        shared_ridged_fbm({detail_domain.x * 1.86F + 2.0F, detail_domain.y * 1.86F - 6.0F,
                           detail_domain.z * 0.92F + 11.0F},
                          derived_seed32(seed, "milky-way-v2.cloud-ridge"), 4U, 2.18F, 0.49F);
    const float cloud_clumps =
        smoothstep(0.42F, 0.90F, cloud_noise * 0.55F + cloud_ridge * 0.52F +
                                      arm_probability * 0.24F + center_weight * 0.16F);
    const float dust_clear = 1.0F - smoothstep(0.68F, 1.90F, dust_tau) * 0.72F;
    const float star_cloud_density =
        ((thin_disk * 0.0052F + broad_disk * 0.0010F) *
             mix(0.55F, 1.55F, cloud_clumps) +
         bulge * 0.0040F + bar * 0.0022F) *
        dust_clear;

    const Vec3 band_color = {0.70F, 0.78F, 1.0F};
    const Vec3 core_color = {1.0F, 0.80F, 0.52F};
    const Vec3 cloud_color =
        mix({0.70F, 0.78F, 1.0F}, {0.98F, 0.90F, 0.70F},
            clamp01(center_weight * 0.62F + arm_probability * 0.12F));

    ProceduralMilkyWayLayers layers;
    const float stellar_density =
        broad_disk * 0.0008F + thin_disk * 0.0032F + inner_disk * 0.0018F +
        bulge * 0.023F + bar * 0.008F;
    const float stellar_texture = mix(0.78F, 1.30F, cloud_noise) * mix(0.86F, 1.24F, cloud_ridge);
    layers.stellar_emission =
        mix(band_color, core_color, clamp01(bulge * 1.25F + bar * 0.48F)) *
        (stellar_density * stellar_texture);
    layers.star_clouds = cloud_color * star_cloud_density;

    const float speckle_mask = smoothstep(0.07F, 0.88F, thin_disk + broad_disk * 0.32F);
    layers.speckles =
        shared_sparse_speckles({local.x * 62.0F + 7.0F, local.y * 62.0F - 13.0F,
                                local.z * 86.0F + 19.0F},
                               seed) *
        speckle_mask * mix(0.0007F, 0.0038F, clamp01(cloud_clumps + center_weight * 0.15F));

    const float hii_density =
        procedural_hii_cells(longitude_warp, latitude_warp, thin_disk + bulge * 0.5F,
                             cloud_clumps, derived_seed32(seed, "milky-way-v2.hii.seed"));
    layers.hii_emission = Vec3{1.0F, 0.32F, 0.15F} * hii_density * 0.0028F;

    const Vec3 extinction{
        std::exp(-dust_tau * 0.48F),
        std::exp(-dust_tau * 0.78F),
        std::exp(-dust_tau * 1.14F),
    };
    const Vec3 speckle_color =
        mix({0.62F, 0.70F, 1.0F}, {1.0F, 0.84F, 0.58F},
            clamp01(center_weight * 0.55F + cloud_noise * 0.20F));
    layers.final_rgb =
        (layers.stellar_emission + layers.star_clouds + speckle_color * layers.speckles) *
            extinction +
        layers.hii_emission;
    layers.dust_tau = dust_tau;
    return layers;
}

[[nodiscard]] Vec3 procedural_layer_color(const ProceduralMilkyWayLayers& layers,
                                          NightSkyLayerView layer) {
    switch (layer) {
    case NightSkyLayerView::Final:
        return layers.final_rgb;
    case NightSkyLayerView::StellarEmission:
        return layers.stellar_emission;
    case NightSkyLayerView::DustTau:
        return Vec3{layers.dust_tau, layers.dust_tau, layers.dust_tau} * 0.018F;
    case NightSkyLayerView::StarClouds:
        return layers.star_clouds;
    case NightSkyLayerView::HiiEmission:
        return layers.hii_emission * 4.0F;
    case NightSkyLayerView::Speckles:
        return Vec3{0.78F, 0.84F, 1.0F} * (layers.speckles * 3.5F);
    }
    return layers.final_rgb;
}

[[nodiscard]] Vec3 procedural_milky_way(Vec3 direction, std::uint32_t seed,
                                        NightSkyLayerView layer,
                                        NightSkyAtlasFormula formula) {
    const ProceduralMilkyWayLayers layers =
        formula == NightSkyAtlasFormula::V2 ? procedural_milky_way_layers_v2(direction, seed)
                                            : procedural_milky_way_layers_v1(direction, seed);
    return procedural_layer_color(layers, layer);
}

void set_texel(NightSkyAtlas& atlas, std::uint32_t mip, std::uint32_t face, std::uint32_t x,
               std::uint32_t y, Vec3 color) {
    const NightSkyAtlasMip& level = atlas.mips.at(mip);
    const std::size_t texel_offset = level.byte_offset / sizeof(float) +
                                     ((static_cast<std::size_t>(face) * level.extent *
                                           level.extent +
                                       static_cast<std::size_t>(y) * level.extent + x) *
                                      4U);
    const float alpha = clamp01(luminance(color) * 64.0F);
    atlas.rgba32f[texel_offset] = std::max(color.x, 0.0F);
    atlas.rgba32f[texel_offset + 1U] = std::max(color.y, 0.0F);
    atlas.rgba32f[texel_offset + 2U] = std::max(color.z, 0.0F);
    atlas.rgba32f[texel_offset + 3U] = alpha;
}

[[nodiscard]] Vec3 get_texel(const NightSkyAtlas& atlas, std::uint32_t mip, std::uint32_t face,
                             std::uint32_t x, std::uint32_t y) {
    const NightSkyAtlasMip& level = atlas.mips.at(mip);
    x = std::min(x, level.extent - 1U);
    y = std::min(y, level.extent - 1U);
    const std::size_t texel_offset = level.byte_offset / sizeof(float) +
                                     ((static_cast<std::size_t>(face) * level.extent *
                                           level.extent +
                                       static_cast<std::size_t>(y) * level.extent + x) *
                                      4U);
    return {atlas.rgba32f[texel_offset], atlas.rgba32f[texel_offset + 1U],
            atlas.rgba32f[texel_offset + 2U]};
}

void build_mips(NightSkyAtlas& atlas) {
    for (std::uint32_t mip = 1; mip < atlas.mip_levels; ++mip) {
        const std::uint32_t extent = atlas.mips.at(mip).extent;
        for (std::uint32_t face = 0; face < 6U; ++face) {
            for (std::uint32_t y = 0; y < extent; ++y) {
                for (std::uint32_t x = 0; x < extent; ++x) {
                    Vec3 sum{};
                    for (std::uint32_t oy = 0; oy < 2U; ++oy) {
                        for (std::uint32_t ox = 0; ox < 2U; ++ox) {
                            sum = sum + get_texel(atlas, mip - 1U, face, x * 2U + ox,
                                                  y * 2U + oy);
                        }
                    }
                    set_texel(atlas, mip, face, x, y, sum * 0.25F);
                }
            }
        }
    }
}

[[nodiscard]] NightSkyAtlas make_empty_atlas(std::uint32_t extent, NightSkyLayerView layer,
                                             NightSkyAtlasFormula formula) {
    const std::uint32_t mip_levels = night_sky_atlas_mip_count(extent);
    NightSkyAtlas atlas{
        .extent = extent,
        .mip_levels = mip_levels,
        .layer = layer,
        .formula = formula,
    };
    atlas.mips.reserve(mip_levels);
    std::size_t byte_offset = 0;
    for (std::uint32_t mip = 0; mip < mip_levels; ++mip) {
        const std::uint32_t mip_extent = std::max(1U, extent >> mip);
        const std::size_t byte_count = static_cast<std::size_t>(mip_extent) *
                                       static_cast<std::size_t>(mip_extent) * 6U * 4U *
                                       sizeof(float);
        atlas.mips.push_back(NightSkyAtlasMip{
            .extent = mip_extent,
            .byte_offset = byte_offset,
            .byte_count = byte_count,
        });
        byte_offset += byte_count;
    }
    atlas.rgba32f.resize(byte_offset / sizeof(float));
    return atlas;
}

} // namespace

std::uint32_t night_sky_atlas_mip_count(std::uint32_t extent) {
    if (extent == 0U) {
        throw std::runtime_error("night sky atlas extent must be positive");
    }
    std::uint32_t levels = 1;
    while (extent > 1U) {
        extent >>= 1U;
        ++levels;
    }
    return levels;
}

NightSkyAtlas generate_night_sky_atlas(const NightSkyAtlasConfig& config, std::uint32_t extent) {
    if (extent == 0U || (extent & (extent - 1U)) != 0U) {
        throw std::runtime_error("night sky atlas extent must be a power of two");
    }

    NightSkyAtlas atlas = make_empty_atlas(extent, config.layer, config.formula);
    const std::uint32_t variation_seed =
        hash_u32(static_cast<std::uint32_t>(std::round(config.procedural_variation * 1000.0F)) +
                 0x7a41c6d3U);
    for (std::uint32_t face = 0; face < 6U; ++face) {
        for (std::uint32_t y = 0; y < extent; ++y) {
            for (std::uint32_t x = 0; x < extent; ++x) {
                const Vec3 direction = cubemap_direction(face, x, y, extent);
                const Vec3 color =
                    procedural_milky_way(direction, variation_seed, config.layer, config.formula);
                set_texel(atlas, 0U, face, x, y, color);
            }
        }
    }
    build_mips(atlas);
    atlas.metadata = cubey::procedural::make_procedural_artifact_metadata(
        cubey::procedural::make_procedural_artifact_identity(
            "atmosphere night sky atlas",
            "cubey::render::generate_night_sky_atlas",
            config.formula == NightSkyAtlasFormula::V2 ? "atmosphere-night-sky-atlas-v2"
                                                       : "atmosphere-night-sky-atlas-v1",
            "atmosphere.night_sky_atlas",
            variation_seed,
            cubey::procedural::ProceduralDomainSpace::Spherical),
        cubey::procedural::ProceduralArtifactKind::TextureCube,
        cubey::procedural::ProceduralArtifactValueFormat::Rgba32Float,
        {.width = extent, .height = extent, .depth = 1, .faces = 6, .mip_levels = atlas.mip_levels},
        night_sky_atlas_hash(atlas.rgba32f));
    return atlas;
}

std::uint64_t night_sky_atlas_hash(std::span<const float> values) {
    cubey::procedural::ProceduralHashBuilder hash(1469598103934665603ULL);
    for (const float value : values) {
        const std::uint32_t quantized =
            static_cast<std::uint32_t>(std::round(std::clamp(value, 0.0F, 16.0F) * 65535.0F));
        hash.append_u32(quantized);
    }
    return hash.value();
}

} // namespace cubey::render
