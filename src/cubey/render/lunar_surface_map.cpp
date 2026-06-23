#include <cubey/render/lunar_surface_map.h>

#include <cubey/procedural/hash.h>
#include <cubey/procedural/noise.h>
#include <cubey/procedural/sample_domain.h>
#include <cubey/procedural/seed.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <stdexcept>
#include <vector>

namespace cubey::render {
namespace {

constexpr std::uint64_t kLunarSurfaceBaseSeed = 0x6c75'6e61'722d'7631ULL;
constexpr float kPi = std::numbers::pi_v<float>;
constexpr float kTwoPi = kPi * 2.0F;

struct Vec3 {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
};

struct MariaPlain {
    float longitude_degrees = 0.0F;
    float latitude_degrees = 0.0F;
    float major_degrees = 1.0F;
    float minor_degrees = 1.0F;
    float rotation_degrees = 0.0F;
    float strength = 1.0F;
};

struct MareField {
    float coverage = 0.0F;
    float fill = 0.0F;
};

struct Crater {
    Vec3 direction{};
    float radius_radians = 0.01F;
    float depth = 0.0F;
    float rim = 0.0F;
    float ray = 0.0F;
};

[[nodiscard]] float saturate(float value) {
    return std::clamp(value, 0.0F, 1.0F);
}

[[nodiscard]] float mix(float from, float to, float amount) {
    return from + (to - from) * amount;
}

[[nodiscard]] float smoothstep(float edge0, float edge1, float value) {
    if (edge0 == edge1) {
        return value < edge0 ? 0.0F : 1.0F;
    }
    const float t = saturate((value - edge0) / (edge1 - edge0));
    return t * t * (3.0F - (2.0F * t));
}

[[nodiscard]] float dot(Vec3 lhs, Vec3 rhs) {
    return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z;
}

[[nodiscard]] Vec3 normalize(Vec3 value) {
    const float length =
        std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z);
    if (length <= 0.000001F) {
        return {0.0F, 0.0F, 1.0F};
    }
    return {
        value.x / length,
        value.y / length,
        value.z / length,
    };
}

[[nodiscard]] Vec3 tangent_east(float longitude_radians) {
    return {
        -std::sin(longitude_radians),
        std::cos(longitude_radians),
        0.0F,
    };
}

[[nodiscard]] Vec3 tangent_north(float longitude_radians, float latitude_radians) {
    return {
        -std::cos(longitude_radians) * std::sin(latitude_radians),
        -std::sin(longitude_radians) * std::sin(latitude_radians),
        std::cos(latitude_radians),
    };
}

[[nodiscard]] float radians(float degrees) {
    return degrees * kPi / 180.0F;
}

[[nodiscard]] Vec3 direction_from_lon_lat(float longitude_radians, float latitude_radians) {
    const float horizontal = std::cos(latitude_radians);
    return {
        std::cos(longitude_radians) * horizontal,
        std::sin(longitude_radians) * horizontal,
        std::sin(latitude_radians),
    };
}

[[nodiscard]] Vec3 direction_for_texel(std::uint32_t x, std::uint32_t y, std::uint32_t width,
                                       std::uint32_t height) {
    const float u = (static_cast<float>(x) + 0.5F) / static_cast<float>(width);
    const float v = (static_cast<float>(y) + 0.5F) / static_cast<float>(height);
    const float longitude = (u - 0.5F) * kTwoPi;
    const float latitude = (0.5F - v) * kPi;
    return direction_from_lon_lat(longitude, latitude);
}

[[nodiscard]] std::uint32_t seed32(std::string_view domain) {
    return static_cast<std::uint32_t>(
        cubey::procedural::derive_seed(kLunarSurfaceBaseSeed, domain));
}

[[nodiscard]] float fbm(Vec3 direction, float scale, std::string_view domain,
                        std::uint32_t octaves = 4U, float gain = 0.5F) {
    return cubey::procedural::fbm_3d(direction.x * scale, direction.y * scale, direction.z * scale,
                                     seed32(domain),
                                     {
                                         .octaves = octaves,
                                         .lacunarity = 2.02F,
                                         .gain = gain,
                                         .initial_amplitude = 0.58F,
                                     });
}

[[nodiscard]] float ridged(Vec3 direction, float scale, std::string_view domain,
                           std::uint32_t octaves = 4U) {
    return cubey::procedural::ridged_fbm_3d(direction.x * scale, direction.y * scale,
                                            direction.z * scale, seed32(domain),
                                            {
                                                .octaves = octaves,
                                                .lacunarity = 2.09F,
                                                .gain = 0.52F,
                                                .initial_amplitude = 0.52F,
                                            });
}

[[nodiscard]] Vec3 material_direction(Vec3 direction) {
    constexpr float kOffset = 0.055F;
    const float nx = mix(0.90F, 1.10F,
                         fbm({direction.x + kOffset, direction.y, direction.z}, 3.0F,
                             "material normal perturb x", 4U, 0.50F));
    const float ny = mix(0.90F, 1.10F,
                         fbm({direction.x, direction.y + kOffset, direction.z}, 3.0F,
                             "material normal perturb y", 4U, 0.50F));
    const float nz = mix(0.90F, 1.10F,
                         fbm({direction.x, direction.y, direction.z + kOffset}, 3.0F,
                             "material normal perturb z", 4U, 0.50F));
    return normalize({direction.x * nx, direction.y * ny, direction.z * nz});
}

[[nodiscard]] float plain_mask(Vec3 direction, const MariaPlain& plain) {
    const float longitude = radians(plain.longitude_degrees);
    const float latitude = radians(plain.latitude_degrees);
    const Vec3 center = direction_from_lon_lat(longitude, latitude);
    const float center_dot = std::clamp(dot(direction, center), -1.0F, 1.0F);
    const float angle = std::acos(center_dot);
    const float sin_angle = std::max(std::sqrt(std::max(1.0F - center_dot * center_dot, 0.0F)),
                                     0.00001F);
    const float tangent_scale = angle / sin_angle;
    const float east = dot(direction, tangent_east(longitude)) * tangent_scale;
    const float north = dot(direction, tangent_north(longitude, latitude)) * tangent_scale;
    const float rotation = radians(plain.rotation_degrees);
    const float rotated_major = east * std::cos(rotation) + north * std::sin(rotation);
    const float rotated_minor = -east * std::sin(rotation) + north * std::cos(rotation);
    const float major = std::max(radians(plain.major_degrees), 0.0001F);
    const float minor = std::max(radians(plain.minor_degrees), 0.0001F);
    const float superellipse = std::sqrt((rotated_major * rotated_major) / (major * major) +
                                         (rotated_minor * rotated_minor) / (minor * minor));
    const float shore_noise =
        (fbm(direction, 6.2F, "maria shore breakup", 4U, 0.50F) - 0.5F) * 0.10F;
    const float edge_weight =
        smoothstep(0.58F, 1.08F, superellipse) * (1.0F - smoothstep(1.08F, 1.34F, superellipse));
    const float edge = superellipse + shore_noise * edge_weight;
    const float core = 1.0F - smoothstep(0.70F, 1.00F, edge);
    const float shelf = (1.0F - smoothstep(1.00F, 1.18F, edge)) * 0.06F;
    return saturate((core + shelf) * plain.strength);
}

template <std::size_t N>
[[nodiscard]] float complex_mask(Vec3 direction, const std::array<MariaPlain, N>& plains) {
    float mask = 0.0F;
    for (const MariaPlain& plain : plains) {
        mask = std::max(mask, plain_mask(direction, plain));
    }
    return mask;
}

[[nodiscard]] MareField mare_field(Vec3 direction) {
    // Reference-guided near-side mare complexes. The broad complexes come first
    // so the Moon reads as basin-shaped basalt plains rather than scattered dots.
    constexpr std::array<MariaPlain, 8> kWesternComplex{
        MariaPlain{.longitude_degrees = -61.0F,
                   .latitude_degrees = 1.0F,
                   .major_degrees = 18.0F,
                   .minor_degrees = 44.0F,
                   .rotation_degrees = -7.0F,
                   .strength = 0.52F},
        MariaPlain{.longitude_degrees = -43.0F,
                   .latitude_degrees = -18.0F,
                   .major_degrees = 18.0F,
                   .minor_degrees = 23.0F,
                   .rotation_degrees = -18.0F,
                   .strength = 0.45F},
        MariaPlain{.longitude_degrees = -38.0F,
                   .latitude_degrees = 5.0F,
                   .major_degrees = 31.0F,
                   .minor_degrees = 26.0F,
                   .rotation_degrees = -10.0F,
                   .strength = 0.48F},
        MariaPlain{.longitude_degrees = -22.0F,
                   .latitude_degrees = 34.0F,
                   .major_degrees = 25.0F,
                   .minor_degrees = 17.0F,
                   .rotation_degrees = 12.0F,
                   .strength = 0.62F},
        MariaPlain{.longitude_degrees = -4.0F,
                   .latitude_degrees = 57.0F,
                   .major_degrees = 45.0F,
                   .minor_degrees = 6.0F,
                   .rotation_degrees = 0.0F,
                   .strength = 0.26F},
        MariaPlain{.longitude_degrees = -39.0F,
                   .latitude_degrees = -24.0F,
                   .major_degrees = 13.0F,
                   .minor_degrees = 11.0F,
                   .rotation_degrees = 6.0F,
                   .strength = 0.40F},
        MariaPlain{.longitude_degrees = -15.0F,
                   .latitude_degrees = -21.0F,
                   .major_degrees = 22.0F,
                   .minor_degrees = 13.0F,
                   .rotation_degrees = -10.0F,
                   .strength = 0.42F},
        MariaPlain{.longitude_degrees = -25.0F,
                   .latitude_degrees = -5.0F,
                   .major_degrees = 26.0F,
                   .minor_degrees = 22.0F,
                   .rotation_degrees = -16.0F,
                   .strength = 0.38F},
    };
    constexpr std::array<MariaPlain, 6> kEasternComplex{
        MariaPlain{.longitude_degrees = 18.0F,
                   .latitude_degrees = 28.0F,
                   .major_degrees = 15.0F,
                   .minor_degrees = 12.0F,
                   .rotation_degrees = -9.0F,
                   .strength = 0.46F},
        MariaPlain{.longitude_degrees = 33.0F,
                   .latitude_degrees = 8.0F,
                   .major_degrees = 31.0F,
                   .minor_degrees = 16.0F,
                   .rotation_degrees = -17.0F,
                   .strength = 0.56F},
        MariaPlain{.longitude_degrees = 40.0F,
                   .latitude_degrees = -1.0F,
                   .major_degrees = 42.0F,
                   .minor_degrees = 22.0F,
                   .rotation_degrees = -24.0F,
                   .strength = 0.42F},
        MariaPlain{.longitude_degrees = 54.0F,
                   .latitude_degrees = -8.0F,
                   .major_degrees = 24.0F,
                   .minor_degrees = 16.0F,
                   .rotation_degrees = -26.0F,
                   .strength = 0.46F},
        MariaPlain{.longitude_degrees = 59.0F,
                   .latitude_degrees = 17.0F,
                   .major_degrees = 12.0F,
                   .minor_degrees = 10.0F,
                   .rotation_degrees = -6.0F,
                   .strength = 0.38F},
        MariaPlain{.longitude_degrees = 35.0F,
                   .latitude_degrees = -16.0F,
                   .major_degrees = 12.0F,
                   .minor_degrees = 8.0F,
                   .rotation_degrees = -4.0F,
                   .strength = 0.34F},
    };
    constexpr std::array<MariaPlain, 3> kIsolatedBasins{
        MariaPlain{.longitude_degrees = 76.0F,
                   .latitude_degrees = 2.0F,
                   .major_degrees = 11.0F,
                   .minor_degrees = 7.0F,
                   .rotation_degrees = -20.0F,
                   .strength = 0.24F},
        MariaPlain{.longitude_degrees = -35.0F,
                   .latitude_degrees = 44.0F,
                   .major_degrees = 14.0F,
                   .minor_degrees = 7.0F,
                   .rotation_degrees = 10.0F,
                   .strength = 0.28F},
        MariaPlain{.longitude_degrees = 35.0F,
                   .latitude_degrees = -16.0F,
                   .major_degrees = 12.0F,
                   .minor_degrees = 8.0F,
                   .rotation_degrees = -4.0F,
                   .strength = 0.28F},
    };
    constexpr std::array<MariaPlain, 4> kHighlandBays{
        MariaPlain{.longitude_degrees = 2.0F,
                   .latitude_degrees = 22.0F,
                   .major_degrees = 8.0F,
                   .minor_degrees = 24.0F,
                   .rotation_degrees = -8.0F,
                   .strength = 0.26F},
        MariaPlain{.longitude_degrees = 25.0F,
                   .latitude_degrees = 17.0F,
                   .major_degrees = 7.5F,
                   .minor_degrees = 12.0F,
                   .rotation_degrees = 14.0F,
                   .strength = 0.18F},
        MariaPlain{.longitude_degrees = 47.0F,
                   .latitude_degrees = 14.0F,
                   .major_degrees = 7.0F,
                   .minor_degrees = 13.0F,
                   .rotation_degrees = -2.0F,
                   .strength = 0.24F},
        MariaPlain{.longitude_degrees = 9.0F,
                   .latitude_degrees = -16.0F,
                   .major_degrees = 14.0F,
                   .minor_degrees = 8.0F,
                   .rotation_degrees = -18.0F,
                   .strength = 0.22F},
    };

    const float west = complex_mask(direction, kWesternComplex);
    const float east = complex_mask(direction, kEasternComplex);
    const float isolated = complex_mask(direction, kIsolatedBasins);
    float bay = 0.0F;
    for (const MariaPlain& highland : kHighlandBays) {
        bay = std::max(bay, plain_mask(direction, highland));
    }
    const float mask = std::max(std::max(west, east), isolated);
    const float edge =
        smoothstep(0.08F, 0.40F, mask) * (1.0F - smoothstep(0.62F, 0.96F, mask));
    const float breakup = (fbm(direction, 4.8F, "maria edge breakup", 4U, 0.52F) - 0.5F) * 0.04F;
    const float coverage = saturate(mask * (1.0F - bay * 0.32F) + breakup * edge);
    return {
        .coverage = coverage,
        .fill = coverage,
    };
}

[[nodiscard]] std::vector<Crater> generate_craters() {
    std::vector<Crater> craters;
    craters.reserve(64U);
    const std::array<Crater, 5> kRayCraters{
        Crater{.direction = direction_from_lon_lat(radians(-11.0F), radians(-43.0F)),
               .radius_radians = radians(4.6F),
               .depth = 0.060F,
               .rim = 0.068F,
               .ray = 0.120F},
        Crater{.direction = direction_from_lon_lat(radians(-20.0F), radians(-11.0F)),
               .radius_radians = radians(3.1F),
               .depth = 0.044F,
               .rim = 0.050F,
               .ray = 0.070F},
        Crater{.direction = direction_from_lon_lat(radians(15.0F), radians(-58.0F)),
               .radius_radians = radians(3.8F),
               .depth = 0.048F,
               .rim = 0.052F,
               .ray = 0.055F},
        Crater{.direction = direction_from_lon_lat(radians(-94.0F), radians(10.0F)),
               .radius_radians = radians(5.2F),
               .depth = 0.050F,
               .rim = 0.060F,
               .ray = 0.040F},
        Crater{.direction = direction_from_lon_lat(radians(95.0F), radians(-28.0F)),
               .radius_radians = radians(4.4F),
               .depth = 0.048F,
               .rim = 0.054F,
               .ray = 0.030F},
    };
    craters.insert(craters.end(), kRayCraters.begin(), kRayCraters.end());

    for (std::uint32_t index = 0; index < 59U; ++index) {
        const float longitude =
            (cubey::procedural::random01(kLunarSurfaceBaseSeed, "crater longitude", index, 0U) -
             0.5F) *
            kTwoPi;
        const float latitude = std::asin(
            (cubey::procedural::random01(kLunarSurfaceBaseSeed, "crater latitude", index, 0U) *
             1.92F) -
            0.96F);
        const float size =
            cubey::procedural::random01(kLunarSurfaceBaseSeed, "crater radius", index, 0U);
        const float radius = radians(mix(0.55F, 3.1F, size * size));
        const float prominence =
            cubey::procedural::random01(kLunarSurfaceBaseSeed, "crater prominence", index, 0U);
        craters.push_back(Crater{
            .direction = direction_from_lon_lat(longitude, latitude),
            .radius_radians = radius,
            .depth = mix(0.012F, 0.040F, prominence),
            .rim = mix(0.010F, 0.034F, prominence),
            .ray = prominence > 0.92F ? mix(0.018F, 0.045F, prominence) : 0.0F,
        });
    }
    return craters;
}

[[nodiscard]] float crater_weight(Vec3 direction, const Crater& crater) {
    const float denominator = std::max(1.0F - std::cos(crater.radius_radians), 0.000001F);
    return saturate((1.0F - dot(direction, crater.direction)) / denominator);
}

[[nodiscard]] float ray_weight(Vec3 direction, const Crater& crater) {
    if (crater.ray <= 0.0F) {
        return 0.0F;
    }
    const float center_dot = std::clamp(dot(direction, crater.direction), -1.0F, 1.0F);
    const float angle = std::acos(center_dot);
    if (angle <= crater.radius_radians || angle > crater.radius_radians * 9.0F) {
        return 0.0F;
    }
    const float falloff =
        1.0F - smoothstep(crater.radius_radians * 1.4F, crater.radius_radians * 9.0F, angle);
    const float grain =
        ridged(direction, 36.0F + crater.radius_radians * 700.0F, "crater rays", 3U);
    return crater.ray * falloff * smoothstep(0.56F, 0.93F, grain);
}

struct SurfaceSample {
    float albedo = 0.0F;
    float height = 0.0F;
    float roughness = 0.0F;
};

[[nodiscard]] std::size_t texel_index(std::uint32_t x, std::uint32_t y, std::uint32_t width) {
    return static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
           static_cast<std::size_t>(x);
}

[[nodiscard]] std::vector<float> blur_scalar_field(std::span<const float> source,
                                                   std::uint32_t width, std::uint32_t height,
                                                   std::uint32_t radius) {
    if (radius == 0U) {
        return std::vector<float>{source.begin(), source.end()};
    }

    std::vector<float> horizontal(source.size(), 0.0F);
    std::vector<float> result(source.size(), 0.0F);
    const float sample_count = static_cast<float>(radius * 2U + 1U);
    for (std::uint32_t y = 0; y < height; ++y) {
        for (std::uint32_t x = 0; x < width; ++x) {
            float sum = 0.0F;
            for (std::uint32_t offset = 0; offset <= radius * 2U; ++offset) {
                const std::int32_t dx = static_cast<std::int32_t>(offset) -
                                        static_cast<std::int32_t>(radius);
                const std::uint32_t sx =
                    static_cast<std::uint32_t>((static_cast<std::int32_t>(x) + dx +
                                                static_cast<std::int32_t>(width)) %
                                               static_cast<std::int32_t>(width));
                sum += source[texel_index(sx, y, width)];
            }
            horizontal[texel_index(x, y, width)] = sum / sample_count;
        }
    }

    for (std::uint32_t y = 0; y < height; ++y) {
        for (std::uint32_t x = 0; x < width; ++x) {
            float sum = 0.0F;
            for (std::uint32_t offset = 0; offset <= radius * 2U; ++offset) {
                const std::int32_t dy = static_cast<std::int32_t>(offset) -
                                        static_cast<std::int32_t>(radius);
                const std::uint32_t sy = static_cast<std::uint32_t>(std::clamp(
                    static_cast<std::int32_t>(y) + dy, 0, static_cast<std::int32_t>(height - 1U)));
                sum += horizontal[texel_index(x, sy, width)];
            }
            result[texel_index(x, y, width)] = sum / sample_count;
        }
    }
    return result;
}

[[nodiscard]] SurfaceSample sample_surface(Vec3 direction, MareField mare_field_sample,
                                           std::span<const Crater> craters) {
    const float mare_coverage = mare_field_sample.coverage;
    const float mare_fill = mare_field_sample.fill;
    const Vec3 material = material_direction(direction);
    const float broad = fbm(material, 2.5F, "broad regolith", 5U, 0.55F);
    const float mid = fbm(material, 11.0F, "mid regolith", 4U, 0.52F);
    const float fine = fbm(material, 44.0F, "fine regolith", 3U, 0.48F);
    const float normal_tone = fbm(material, 4.0F, "normal-space surface tone", 5U, 0.50F) - 0.5F;
    const float highland_pores = ridged(material, 29.0F, "highland pores", 4U) - 0.48F;
    const float mare_plains = fbm(material, 1.7F, "mare basalt plains", 4U, 0.50F);
    const float mare_mottling = fbm(material, 12.0F, "mare subtle mottling", 3U, 0.42F);
    const float highlands =
        0.610F + broad * 0.088F + mid * 0.055F + fine * 0.030F + highland_pores * 0.032F +
        normal_tone * 0.036F;
    const float mare =
        0.370F + mare_plains * 0.016F + mare_mottling * 0.006F + fine * 0.003F +
        normal_tone * 0.020F;

    float albedo = mix(highlands, mare, mare_fill);
    float height =
        broad * 0.028F + mid * 0.014F + fine * 0.006F + normal_tone * 0.008F -
        mare_coverage * 0.036F;
    float roughness = mix(0.86F, 0.72F, mare_fill) + highland_pores * 0.050F;

    for (const Crater& crater : craters) {
        const float weight = crater_weight(direction, crater);
        if (weight < 1.0F) {
            const float crater_albedo_scale = mix(1.0F, 0.42F, mare_fill);
            const float crater_height_scale = mix(1.0F, 0.58F, mare_coverage);
            const float floor = 1.0F - smoothstep(0.18F, 0.82F, weight);
            const float rim =
                smoothstep(0.62F, 0.86F, weight) * (1.0F - smoothstep(0.86F, 1.0F, weight));
            const float ejecta = 1.0F - smoothstep(0.82F, 1.0F, weight);
            albedo += (rim * crater.rim * 0.82F - floor * crater.depth * 0.24F +
                       ejecta * crater.rim * 0.12F) *
                      crater_albedo_scale;
            height += (rim * crater.rim - floor * crater.depth) * crater_height_scale;
            roughness += rim * 0.06F;
        }
        const float ray = ray_weight(direction, crater) * mix(1.0F, 0.62F, mare_fill);
        albedo += ray;
        height += ray * 0.004F;
    }

    return {
        .albedo = std::clamp(albedo, 0.12F, 0.88F),
        .height = std::clamp(height, -0.16F, 0.18F),
        .roughness = std::clamp(roughness, 0.50F, 0.95F),
    };
}

[[nodiscard]] std::uint8_t pack_unorm(float value) {
    return static_cast<std::uint8_t>(std::round(saturate(value) * 255.0F));
}

[[nodiscard]] std::uint8_t pack_signed_normal(float value) {
    return pack_unorm(value * 0.5F + 0.5F);
}

[[nodiscard]] std::vector<std::uint8_t> downsample_mip(const std::vector<std::uint8_t>& source,
                                                       std::uint32_t width, std::uint32_t height) {
    const std::uint32_t next_width = std::max(width / 2U, 1U);
    const std::uint32_t next_height = std::max(height / 2U, 1U);
    std::vector<std::uint8_t> result(static_cast<std::size_t>(next_width) *
                                     static_cast<std::size_t>(next_height) * 4U);
    for (std::uint32_t y = 0; y < next_height; ++y) {
        for (std::uint32_t x = 0; x < next_width; ++x) {
            std::array<std::uint32_t, 4> sum{};
            std::uint32_t count = 0U;
            for (std::uint32_t oy = 0; oy < 2U; ++oy) {
                for (std::uint32_t ox = 0; ox < 2U; ++ox) {
                    const std::uint32_t sx = std::min(x * 2U + ox, width - 1U);
                    const std::uint32_t sy = std::min(y * 2U + oy, height - 1U);
                    const std::size_t source_offset = (texel_index(sx, sy, width) * 4U);
                    for (std::uint32_t channel = 0; channel < 4U; ++channel) {
                        sum[channel] += source[source_offset + channel];
                    }
                    ++count;
                }
            }
            const std::size_t out = texel_index(x, y, next_width) * 4U;
            for (std::uint32_t channel = 0; channel < 4U; ++channel) {
                result[out + channel] = static_cast<std::uint8_t>(sum[channel] / count);
            }
        }
    }
    return result;
}

void append_mip(LunarSurfaceMap& map, const std::vector<std::uint8_t>& mip, std::uint32_t width,
                std::uint32_t height) {
    const std::size_t offset = map.rgba8.size();
    map.rgba8.insert(map.rgba8.end(), mip.begin(), mip.end());
    map.mips.push_back(LunarSurfaceMapMip{
        .width = width,
        .height = height,
        .byte_offset = offset,
        .byte_count = mip.size(),
    });
}

[[nodiscard]] bool is_power_of_two(std::uint32_t value) {
    return value != 0U && (value & (value - 1U)) == 0U;
}

} // namespace

std::uint32_t lunar_surface_map_mip_count(std::uint32_t width, std::uint32_t height) {
    if (!is_power_of_two(width) || !is_power_of_two(height)) {
        throw std::runtime_error("lunar surface map dimensions must be powers of two");
    }
    if (width != height * 2U) {
        throw std::runtime_error("lunar surface map dimensions must use a 2:1 equirect ratio");
    }

    std::uint32_t levels = 1U;
    while (width > 1U || height > 1U) {
        width = std::max(width / 2U, 1U);
        height = std::max(height / 2U, 1U);
        ++levels;
    }
    return levels;
}

LunarSurfaceMap generate_lunar_surface_map(std::uint32_t width, std::uint32_t height) {
    const std::uint32_t mip_levels = lunar_surface_map_mip_count(width, height);
    const std::size_t texel_count =
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    const std::vector<Crater> craters = generate_craters();
    std::vector<Vec3> directions(texel_count);
    std::vector<float> raw_mare_coverage(texel_count, 0.0F);
    std::vector<float> albedo(texel_count, 0.0F);
    std::vector<float> surface_height(texel_count, 0.0F);
    std::vector<float> roughness(texel_count, 0.0F);

    for (std::uint32_t y = 0; y < height; ++y) {
        for (std::uint32_t x = 0; x < width; ++x) {
            const std::size_t index = texel_index(x, y, width);
            directions[index] = direction_for_texel(x, y, width, height);
            raw_mare_coverage[index] = mare_field(directions[index]).coverage;
        }
    }

    std::vector<float> mare_fill_shape(texel_count, 0.0F);
    for (std::size_t index = 0; index < texel_count; ++index) {
        mare_fill_shape[index] = smoothstep(0.07F, 0.24F, raw_mare_coverage[index]);
    }

    const std::uint32_t blur_radius = std::clamp(width / 80U, 2U, 16U);
    std::vector<float> mare_fill_field =
        blur_scalar_field(mare_fill_shape, width, height, blur_radius);
    mare_fill_field = blur_scalar_field(mare_fill_field, width, height, blur_radius);

    for (std::uint32_t y = 0; y < height; ++y) {
        for (std::uint32_t x = 0; x < width; ++x) {
            const std::size_t index = texel_index(x, y, width);
            const MareField field{
                .coverage = raw_mare_coverage[index],
                .fill = smoothstep(0.08F, 0.72F, mare_fill_field[index]),
            };
            const SurfaceSample sample =
                sample_surface(directions[index], field, std::span<const Crater>{craters});
            albedo[index] = sample.albedo;
            surface_height[index] = sample.height;
            roughness[index] = sample.roughness;
        }
    }

    std::vector<std::uint8_t> current(texel_count * 4U);
    for (std::uint32_t y = 0; y < height; ++y) {
        for (std::uint32_t x = 0; x < width; ++x) {
            const std::uint32_t left = x == 0U ? width - 1U : x - 1U;
            const std::uint32_t right = x + 1U == width ? 0U : x + 1U;
            const std::uint32_t up = y == 0U ? 0U : y - 1U;
            const std::uint32_t down = y + 1U == height ? height - 1U : y + 1U;
            const float dx = surface_height[texel_index(right, y, width)] -
                             surface_height[texel_index(left, y, width)];
            const float dy = surface_height[texel_index(x, down, width)] -
                             surface_height[texel_index(x, up, width)];
            const float normal_x = std::clamp(-dx * 12.0F, -0.72F, 0.72F);
            const float normal_y = std::clamp(-dy * 18.0F, -0.72F, 0.72F);
            const std::size_t out = texel_index(x, y, width) * 4U;
            current[out] = pack_unorm(albedo[texel_index(x, y, width)]);
            current[out + 1U] = pack_signed_normal(normal_x);
            current[out + 2U] = pack_signed_normal(normal_y);
            current[out + 3U] = pack_unorm(roughness[texel_index(x, y, width)]);
        }
    }

    LunarSurfaceMap map{
        .width = width,
        .height = height,
        .mip_levels = mip_levels,
    };
    map.mips.reserve(mip_levels);
    append_mip(map, current, width, height);
    std::uint32_t mip_width = width;
    std::uint32_t mip_height = height;
    while (mip_width > 1U || mip_height > 1U) {
        current = downsample_mip(current, mip_width, mip_height);
        mip_width = std::max(mip_width / 2U, 1U);
        mip_height = std::max(mip_height / 2U, 1U);
        append_mip(map, current, mip_width, mip_height);
    }

    map.metadata = cubey::procedural::make_procedural_artifact_metadata(
        cubey::procedural::make_procedural_artifact_identity(
            "lunar surface map", "cubey::render::generate_lunar_surface_map",
            "lunar-surface-map-v8", "render.lunar_surface_map",
            cubey::procedural::derive_seed(kLunarSurfaceBaseSeed, "render.lunar_surface_map"),
            cubey::procedural::ProceduralDomainSpace::Atlas),
        cubey::procedural::ProceduralArtifactKind::Texture2D,
        cubey::procedural::ProceduralArtifactValueFormat::Rgba8Unorm,
        {.width = width, .height = height, .depth = 1U, .faces = 1U, .mip_levels = mip_levels},
        lunar_surface_map_hash(map.rgba8));
    return map;
}

std::uint64_t lunar_surface_map_hash(std::span<const std::uint8_t> bytes) {
    return cubey::procedural::procedural_hash_bytes(bytes);
}

} // namespace cubey::render
