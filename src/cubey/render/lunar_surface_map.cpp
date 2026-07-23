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

[[nodiscard]] float radians(float degrees) {
    return degrees * kPi / 180.0F;
}

[[nodiscard]] Vec3 rotate_x(Vec3 value, float angle) {
    const float s = std::sin(angle);
    const float c = std::cos(angle);
    return {
        value.x,
        value.y * c - value.z * s,
        value.y * s + value.z * c,
    };
}

[[nodiscard]] Vec3 rotate_y(Vec3 value, float angle) {
    const float s = std::sin(angle);
    const float c = std::cos(angle);
    return {
        value.x * c + value.z * s,
        value.y,
        -value.x * s + value.z * c,
    };
}

[[nodiscard]] Vec3 rotate_z(Vec3 value, float angle) {
    const float s = std::sin(angle);
    const float c = std::cos(angle);
    return {
        value.x * c - value.y * s,
        value.x * s + value.y * c,
        value.z,
    };
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

[[nodiscard]] Vec3 near_side_surface_direction(Vec3 direction) {
    return normalize(rotate_x(direction, radians(-20.0F)));
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

[[nodiscard]] Vec3 mare_field_direction(Vec3 direction) {
    // Rotate only the mare noise domain so broad plains land on the generated near-side face.
    return normalize(rotate_y(rotate_z(direction, radians(-38.0F)), radians(26.0F)));
}

[[nodiscard]] MareField mare_field(Vec3 direction) {
    const Vec3 mare_direction = mare_field_direction(direction);
    const Vec3 broad_warp = normalize({
        mare_direction.x +
            (fbm(mare_direction, 1.05F, "body-space mare warp x", 4U, 0.52F) - 0.5F) *
                0.18F,
        mare_direction.y +
            (fbm(mare_direction, 1.05F, "body-space mare warp y", 4U, 0.52F) - 0.5F) *
                0.18F,
        mare_direction.z +
            (fbm(mare_direction, 1.05F, "body-space mare warp z", 4U, 0.52F) - 0.5F) *
                0.18F,
    });
    const Vec3 warped = normalize({
        broad_warp.x +
            (fbm(broad_warp, 2.4F, "body-space mare lobe warp x", 3U, 0.48F) - 0.5F) * 0.06F,
        broad_warp.y +
            (fbm(broad_warp, 2.4F, "body-space mare lobe warp y", 3U, 0.48F) - 0.5F) * 0.06F,
        broad_warp.z +
            (fbm(broad_warp, 2.4F, "body-space mare lobe warp z", 3U, 0.48F) - 0.5F) * 0.06F,
    });
    const float basin = fbm(warped, 0.78F, "body-space near-side mare mass", 5U, 0.60F);
    const float broad = fbm(warped, 1.35F, "body-space broad mare field", 5U, 0.58F);
    const float lobe = fbm(warped, 1.95F, "body-space mare lobe field", 4U, 0.52F);
    const float near_side_bias = smoothstep(-0.12F, 0.70F, direction.x);
    const float central_basin_bias = smoothstep(0.18F, 0.86F, direction.x);
    const float center_lift =
        near_side_bias * smoothstep(-0.26F, 0.74F, mare_direction.x) * 0.105F;
    const float central_basin_lift =
        central_basin_bias * (0.075F + smoothstep(0.32F, 0.74F, basin) * 0.060F);
    const float limb_fade = mix(0.54F, 1.0F, central_basin_bias);
    const float field =
        (basin * 0.34F + broad * 0.60F + lobe * 0.06F + center_lift + central_basin_lift) *
        limb_fade;
    const float coverage = smoothstep(-0.03F, 0.37F, field);
    return {
        .coverage = coverage,
        .fill = smoothstep(0.04F, 0.66F, coverage) * 0.98F,
    };
}

[[nodiscard]] std::vector<Crater> generate_craters() {
    std::vector<Crater> craters;
    craters.reserve(88U);
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

    for (std::uint32_t index = 0; index < 83U; ++index) {
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
        const float radius = radians(mix(0.34F, 2.7F, size * size));
        const float prominence =
            cubey::procedural::random01(kLunarSurfaceBaseSeed, "crater prominence", index, 0U);
        craters.push_back(Crater{
            .direction = direction_from_lon_lat(longitude, latitude),
            .radius_radians = radius,
            .depth = mix(0.010F, 0.034F, prominence),
            .rim = mix(0.009F, 0.029F, prominence),
            .ray = prominence > 0.94F ? mix(0.014F, 0.036F, prominence) : 0.0F,
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

[[nodiscard]] SurfaceSample sample_surface(Vec3 direction, MareField mare_field_sample,
                                           std::span<const Crater> craters) {
    const float mare_coverage = mare_field_sample.coverage;
    const float mare_fill = mare_field_sample.fill;
    const Vec3 material = material_direction(direction);
    const float broad = fbm(material, 2.5F, "broad regolith", 5U, 0.55F);
    const float mid = fbm(material, 11.0F, "mid regolith", 4U, 0.52F);
    const float fine = fbm(material, 38.0F, "fine regolith", 3U, 0.46F);
    const float normal_tone = fbm(material, 4.0F, "normal-space surface tone", 5U, 0.50F) - 0.5F;
    const float subtle_disk_tone =
        smoothstep(0.25F, 0.70F, fbm(material, 0.85F, "subtle moon disk tone", 4U, 0.50F));
    const float surface_tone_multiplier = 0.94F + subtle_disk_tone * 0.08F;
    const float highland_pores = ridged(material, 29.0F, "highland pores", 4U) - 0.48F;
    const float micro_crater_flecks = ridged(material, 66.0F, "micro crater flecks", 3U) - 0.54F;
    const float mare_plains = fbm(material, 1.7F, "mare basalt plains", 4U, 0.50F);
    const float mare_mottling = fbm(material, 12.0F, "mare subtle mottling", 3U, 0.42F);
    const float highlands =
        0.655F + broad * 0.086F + mid * 0.058F + fine * 0.024F + highland_pores * 0.030F +
        micro_crater_flecks * 0.016F + normal_tone * 0.032F;
    const float mare =
        0.272F + mare_plains * 0.030F + mare_mottling * 0.014F + fine * 0.005F +
        normal_tone * 0.026F;

    float albedo = mix(highlands, mare, mare_fill) * surface_tone_multiplier;
    float height =
        broad * 0.028F + mid * 0.014F + fine * 0.006F + normal_tone * 0.008F -
        mare_coverage * 0.036F;
    float roughness = mix(0.86F, 0.72F, mare_fill) + highland_pores * 0.050F;

    for (const Crater& crater : craters) {
        const float weight = crater_weight(direction, crater);
        if (weight < 1.0F) {
            const float crater_albedo_scale = mix(1.0F, 0.48F, mare_fill);
            const float crater_height_scale = mix(0.86F, 0.50F, mare_coverage);
            const float floor = 1.0F - smoothstep(0.18F, 0.82F, weight);
            const float rim =
                smoothstep(0.62F, 0.86F, weight) * (1.0F - smoothstep(0.86F, 1.0F, weight));
            const float ejecta = 1.0F - smoothstep(0.82F, 1.0F, weight);
            albedo += (rim * crater.rim * 0.88F - floor * crater.depth * 0.20F +
                       ejecta * crater.rim * 0.10F) *
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

cubey::procedural::ProceduralArtifactRecipe lunar_surface_map_recipe(std::uint32_t width,
                                                                     std::uint32_t height) {
    const std::uint32_t mip_levels = lunar_surface_map_mip_count(width, height);
    cubey::procedural::ProceduralHashBuilder parameters;
    parameters.append_string("lunar-surface-map-parameters-v1");
    parameters.append_u32(width);
    parameters.append_u32(height);
    return {
        .name = "lunar surface map",
        .generator = "cubey::render::generate_lunar_surface_map",
        .formula_version = std::string{kLunarSurfaceMapFormulaVersion},
        .domain = "render.lunar_surface_map",
        .seed = cubey::procedural::derive_seed(kLunarSurfaceBaseSeed, "render.lunar_surface_map"),
        .parameter_hash = parameters.value(),
        .space = cubey::procedural::ProceduralDomainSpace::Atlas,
        .kind = cubey::procedural::ProceduralArtifactKind::Texture2D,
        .format = cubey::procedural::ProceduralArtifactValueFormat::Rgba8Unorm,
        .extent =
            {.width = width, .height = height, .depth = 1U, .faces = 1U, .mip_levels = mip_levels},
    };
}

LunarSurfaceMap generate_lunar_surface_map(std::uint32_t width, std::uint32_t height) {
    const cubey::procedural::ProceduralArtifactRecipe recipe =
        lunar_surface_map_recipe(width, height);
    const std::uint32_t mip_levels = recipe.extent.mip_levels;
    const std::size_t texel_count =
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    const std::vector<Crater> craters = generate_craters();
    std::vector<Vec3> directions(texel_count);
    std::vector<float> albedo(texel_count, 0.0F);
    std::vector<float> surface_height(texel_count, 0.0F);
    std::vector<float> roughness(texel_count, 0.0F);

    for (std::uint32_t y = 0; y < height; ++y) {
        for (std::uint32_t x = 0; x < width; ++x) {
            const std::size_t index = texel_index(x, y, width);
            directions[index] =
                near_side_surface_direction(direction_for_texel(x, y, width, height));
            const MareField field = mare_field(directions[index]);
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
            const float normal_x = std::clamp(-dx * 10.0F, -0.68F, 0.68F);
            const float normal_y = std::clamp(-dy * 14.0F, -0.68F, 0.68F);
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
        cubey::procedural::make_procedural_artifact_identity(recipe.name, recipe.generator,
                                                             recipe.formula_version, recipe.domain,
                                                             recipe.seed, recipe.space),
        recipe.kind, recipe.format, recipe.extent, lunar_surface_map_hash(map.rgba8));
    return map;
}

std::uint64_t lunar_surface_map_hash(std::span<const std::uint8_t> bytes) {
    return cubey::procedural::procedural_hash_bytes(bytes);
}

} // namespace cubey::render
