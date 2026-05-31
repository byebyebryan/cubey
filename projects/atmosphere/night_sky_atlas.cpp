#include "night_sky_atlas.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <vector>

namespace cubey::projects::atmosphere {
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

[[nodiscard]] ProceduralMilkyWayLayers procedural_milky_way_layers(Vec3 direction,
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
                                        NightSkyLayerView layer) {
    return procedural_layer_color(procedural_milky_way_layers(direction, seed), layer);
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

[[nodiscard]] NightSkyAtlas make_empty_atlas(std::uint32_t extent, NightSkyLayerView layer) {
    const std::uint32_t mip_levels = night_sky_atlas_mip_count(extent);
    NightSkyAtlas atlas{
        .extent = extent,
        .mip_levels = mip_levels,
        .layer = layer,
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

    NightSkyAtlas atlas = make_empty_atlas(extent, config.layer);
    const std::uint32_t variation_seed =
        hash_u32(static_cast<std::uint32_t>(std::round(config.procedural_variation * 1000.0F)) +
                 0x7a41c6d3U);
    for (std::uint32_t face = 0; face < 6U; ++face) {
        for (std::uint32_t y = 0; y < extent; ++y) {
            for (std::uint32_t x = 0; x < extent; ++x) {
                const Vec3 direction = cubemap_direction(face, x, y, extent);
                const Vec3 color =
                    procedural_milky_way(direction, variation_seed, config.layer);
                set_texel(atlas, 0U, face, x, y, color);
            }
        }
    }
    build_mips(atlas);
    return atlas;
}

std::uint64_t night_sky_atlas_hash(std::span<const float> values) {
    std::uint64_t hash = 1469598103934665603ULL;
    for (const float value : values) {
        const std::uint32_t quantized =
            static_cast<std::uint32_t>(std::round(std::clamp(value, 0.0F, 16.0F) * 65535.0F));
        hash ^= quantized & 0xffU;
        hash *= 1099511628211ULL;
        hash ^= (quantized >> 8U) & 0xffU;
        hash *= 1099511628211ULL;
        hash ^= (quantized >> 16U) & 0xffU;
        hash *= 1099511628211ULL;
        hash ^= (quantized >> 24U) & 0xffU;
        hash *= 1099511628211ULL;
    }
    return hash;
}

} // namespace cubey::projects::atmosphere
