#include <cubey/render/atmosphere_lunar_atlas.h>

#include <cubey/procedural/artifact_metadata.h>
#include <cubey/procedural/hash.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numbers>
#include <stdexcept>
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

struct Basin {
    Vec2 center;
    Vec2 radius;
    float rotation = 0.0F;
    float strength = 1.0F;
    std::uint32_t seed = 0;
};

struct Crater {
    Vec2 center;
    float radius = 0.01F;
    float strength = 1.0F;
};

struct RaySource {
    Vec2 center;
    float radius = 0.05F;
    float strength = 0.02F;
    float angle_scale = 12.0F;
    std::uint32_t seed = 0;
};

class Rng {
  public:
    explicit Rng(std::uint32_t seed) : state_(seed) {}

    [[nodiscard]] std::uint32_t next() {
        state_ ^= state_ << 13U;
        state_ ^= state_ >> 17U;
        state_ ^= state_ << 5U;
        return state_;
    }

    [[nodiscard]] float unit() {
        constexpr float kInv24Bit = 1.0F / 16'777'215.0F;
        return static_cast<float>(next() >> 8U) * kInv24Bit;
    }

  private:
    std::uint32_t state_ = 1;
};

[[nodiscard]] float clamp01(float value) {
    return std::clamp(value, 0.0F, 1.0F);
}

[[nodiscard]] float mix(float a, float b, float t) {
    return a + (b - a) * t;
}

[[nodiscard]] float smoothstep(float edge0, float edge1, float value) {
    const float t = clamp01((value - edge0) / (edge1 - edge0));
    return t * t * (3.0F - 2.0F * t);
}

[[nodiscard]] Vec2 operator+(Vec2 lhs, Vec2 rhs) {
    return {lhs.x + rhs.x, lhs.y + rhs.y};
}

[[nodiscard]] Vec2 operator-(Vec2 lhs, Vec2 rhs) {
    return {lhs.x - rhs.x, lhs.y - rhs.y};
}

[[nodiscard]] Vec2 operator*(Vec2 value, float scale) {
    return {value.x * scale, value.y * scale};
}

[[nodiscard]] float dot(Vec2 lhs, Vec2 rhs) {
    return lhs.x * rhs.x + lhs.y * rhs.y;
}

[[nodiscard]] float length(Vec2 value) {
    return std::sqrt(dot(value, value));
}

[[nodiscard]] Vec3 normalize(Vec3 value) {
    const float len = std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z);
    if (len <= std::numeric_limits<float>::epsilon()) {
        return {0.0F, 0.0F, 1.0F};
    }
    return {value.x / len, value.y / len, value.z / len};
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

[[nodiscard]] float lattice_hash(int x, int y, std::uint32_t seed) {
    const std::uint32_t ux = static_cast<std::uint32_t>(x);
    const std::uint32_t uy = static_cast<std::uint32_t>(y);
    return hash_to_unit(seed ^ (ux * 0x9e3779b9U) ^ (uy * 0x85ebca6bU));
}

[[nodiscard]] float value_noise(Vec2 position, std::uint32_t seed) {
    const int x0 = static_cast<int>(std::floor(position.x));
    const int y0 = static_cast<int>(std::floor(position.y));
    const float tx = position.x - static_cast<float>(x0);
    const float ty = position.y - static_cast<float>(y0);
    const float sx = tx * tx * (3.0F - 2.0F * tx);
    const float sy = ty * ty * (3.0F - 2.0F * ty);

    const float a = lattice_hash(x0, y0, seed);
    const float b = lattice_hash(x0 + 1, y0, seed);
    const float c = lattice_hash(x0, y0 + 1, seed);
    const float d = lattice_hash(x0 + 1, y0 + 1, seed);
    return mix(mix(a, b, sx), mix(c, d, sx), sy);
}

[[nodiscard]] float fbm(Vec2 position, std::uint32_t seed, int octaves) {
    float amplitude = 0.5F;
    float frequency = 1.0F;
    float sum = 0.0F;
    float norm = 0.0F;
    for (int octave = 0; octave < octaves; ++octave) {
        sum += amplitude *
               value_noise(position * frequency, seed + static_cast<std::uint32_t>(octave) * 97U);
        norm += amplitude;
        frequency *= 2.03F;
        amplitude *= 0.52F;
    }
    return norm > 0.0F ? sum / norm : 0.0F;
}

[[nodiscard]] float centered_fbm(Vec2 position, std::uint32_t seed, int octaves) {
    return fbm(position, seed, octaves) - 0.5F;
}

[[nodiscard]] float ridged_fbm(Vec2 position, std::uint32_t seed, int octaves) {
    float amplitude = 0.5F;
    float frequency = 1.0F;
    float sum = 0.0F;
    float norm = 0.0F;
    for (int octave = 0; octave < octaves; ++octave) {
        const float noise =
            value_noise(position * frequency, seed + static_cast<std::uint32_t>(octave) * 113U);
        sum += amplitude * (1.0F - std::abs(noise * 2.0F - 1.0F));
        norm += amplitude;
        frequency *= 2.11F;
        amplitude *= 0.50F;
    }
    return norm > 0.0F ? sum / norm : 0.0F;
}

[[nodiscard]] Vec2 rotate(Vec2 value, float angle) {
    const float c = std::cos(angle);
    const float s = std::sin(angle);
    return {c * value.x + s * value.y, -s * value.x + c * value.y};
}

[[nodiscard]] float basin_mask(Vec2 position, const Basin& basin) {
    const Vec2 offset = rotate(position - basin.center, basin.rotation);
    const Vec2 uv{offset.x / basin.radius.x, offset.y / basin.radius.y};
    const float edge_noise =
        centered_fbm(uv * 2.4F + Vec2{static_cast<float>(basin.seed) * 0.01F, 4.2F}, basin.seed,
                     4) *
            0.34F +
        centered_fbm(uv * 6.5F + Vec2{1.7F, static_cast<float>(basin.seed) * 0.013F},
                     basin.seed + 43U, 3) *
            0.14F;
    const float distance = length(uv) + edge_noise;
    const float edge = 1.0F - smoothstep(0.62F, 1.08F, distance);
    const float fill_variation =
        0.88F + centered_fbm(uv * 11.0F + Vec2{-3.0F, 7.0F}, basin.seed + 151U, 4) * 0.22F;
    return basin.strength * edge * fill_variation;
}

[[nodiscard]] float maria_mask(Vec2 position) {
    static constexpr std::array kBasins{
        Basin{{-0.43F, 0.03F}, {0.33F, 0.58F}, -0.16F, 0.86F, 11U},
        Basin{{-0.23F, 0.38F}, {0.22F, 0.18F}, 0.06F, 0.88F, 23U},
        Basin{{0.18F, 0.34F}, {0.16F, 0.14F}, -0.18F, 0.82F, 37U},
        Basin{{0.31F, 0.12F}, {0.21F, 0.13F}, 0.18F, 0.78F, 41U},
        Basin{{0.55F, 0.18F}, {0.11F, 0.10F}, -0.04F, 0.74F, 53U},
        Basin{{-0.16F, -0.37F}, {0.17F, 0.12F}, 0.12F, 0.66F, 67U},
        Basin{{-0.36F, -0.44F}, {0.10F, 0.10F}, -0.08F, 0.66F, 71U},
        Basin{{0.42F, -0.18F}, {0.18F, 0.15F}, 0.10F, 0.70F, 83U},
        Basin{{0.23F, -0.32F}, {0.11F, 0.09F}, -0.15F, 0.52F, 97U},
    };

    float mask = 0.0F;
    for (const Basin& basin : kBasins) {
        const float basin_value = clamp01(basin_mask(position, basin));
        mask = 1.0F - (1.0F - mask) * (1.0F - basin_value);
    }
    const float mottling = centered_fbm(position * 5.5F + Vec2{3.0F, -1.0F}, 109U, 4);
    const float grain = centered_fbm(position * 19.0F + Vec2{-8.0F, 6.0F}, 127U, 4);
    return clamp01(mask * (0.86F + grain * 0.22F) + mottling * 0.07F);
}

[[nodiscard]] float micro_crater_layer(Vec2 position, float scale, std::uint32_t seed,
                                       float strength) {
    const Vec2 uv = position * scale +
                    Vec2{static_cast<float>(seed) * 0.013F, static_cast<float>(seed) * 0.021F};
    const int cell_x = static_cast<int>(std::floor(uv.x));
    const int cell_y = static_cast<int>(std::floor(uv.y));
    const Vec2 local{uv.x - static_cast<float>(cell_x), uv.y - static_cast<float>(cell_y)};

    float result = 0.0F;
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            const std::uint32_t cell_seed =
                seed ^ hash_u32(static_cast<std::uint32_t>(cell_x + x) * 0x9e3779b9U) ^
                hash_u32(static_cast<std::uint32_t>(cell_y + y) * 0x85ebca6bU);
            if (hash_to_unit(cell_seed) < 0.64F) {
                continue;
            }

            const Vec2 center{static_cast<float>(x) + hash_to_unit(cell_seed + 17U),
                              static_cast<float>(y) + hash_to_unit(cell_seed + 31U)};
            const float radius = mix(0.12F, 0.35F, hash_to_unit(cell_seed + 47U));
            const float distance = length(local - center) / radius;
            if (distance > 2.4F) {
                continue;
            }

            const float rim = std::exp(-54.0F * (distance - 1.0F) * (distance - 1.0F)) *
                              (1.0F - smoothstep(1.02F, 1.72F, distance));
            const float floor = 1.0F - smoothstep(0.18F, 0.84F, distance);
            const float halo =
                std::exp(-2.8F * distance) * (1.0F - smoothstep(1.0F, 2.4F, distance));
            result += strength * (0.70F * rim - 0.42F * floor + 0.12F * halo);
        }
    }
    return result;
}

[[nodiscard]] float crater_ray_layer(Vec2 position, const RaySource& source) {
    const Vec2 delta = position - source.center;
    const float distance = length(delta);
    if (distance < source.radius || distance > 1.55F) {
        return 0.0F;
    }

    const float angle = std::atan2(delta.y, delta.x);
    const float angle_warp =
        centered_fbm(Vec2{std::cos(angle), std::sin(angle)} * 4.0F + delta * 1.6F, source.seed, 4) *
        1.8F;
    const float ray_a =
        std::pow(std::abs(std::cos(angle * source.angle_scale + angle_warp)), 18.0F);
    const float ray_b = std::pow(
        std::abs(std::cos(angle * (source.angle_scale * 0.57F + 3.0F) - angle_warp)), 28.0F);
    const float broken = smoothstep(
        0.26F, 0.72F,
        fbm(Vec2{angle * 2.4F, distance * 18.0F} + Vec2{3.0F, -7.0F}, source.seed + 19U, 4));
    const float fade = smoothstep(source.radius * 1.2F, source.radius * 2.8F, distance) *
                       (1.0F - smoothstep(0.62F, 1.34F, distance));
    return source.strength * fade * broken * (ray_a + ray_b * 0.65F);
}

[[nodiscard]] float bright_ray_systems(Vec2 position) {
    static constexpr std::array kRays{
        RaySource{{-0.48F, 0.18F}, 0.045F, 0.026F, 13.0F, 301U},
        RaySource{{-0.10F, -0.58F}, 0.055F, 0.034F, 17.0F, 337U},
        RaySource{{0.34F, -0.24F}, 0.038F, 0.016F, 11.0F, 359U},
    };

    float result = 0.0F;
    for (const RaySource& source : kRays) {
        result += crater_ray_layer(position, source);
    }
    return result;
}

[[nodiscard]] Vec2 atlas_position(std::uint32_t x, std::uint32_t y, std::uint32_t extent) {
    const float inv_extent = 1.0F / static_cast<float>(extent);
    return {
        (static_cast<float>(x) + 0.5F) * inv_extent * 2.0F - 1.0F,
        (static_cast<float>(y) + 0.5F) * inv_extent * 2.0F - 1.0F,
    };
}

[[nodiscard]] Vec2 clamp_to_disk(Vec2 position) {
    const float radius = length(position);
    if (radius <= 0.999F) {
        return position;
    }
    return position * (0.999F / radius);
}

[[nodiscard]] std::vector<Crater> generate_craters() {
    std::vector<Crater> craters;
    craters.reserve(2'400);
    Rng rng(0x4d6f6f6eU);
    for (std::size_t i = 0; i < 2'500; ++i) {
        const float angle = rng.unit() * 2.0F * std::numbers::pi_v<float>;
        const float radial = std::sqrt(rng.unit()) * 0.985F;
        const Vec2 center{std::cos(angle) * radial, std::sin(angle) * radial};
        const float mare = maria_mask(center);
        const float keep_probability = mix(0.94F, 0.54F, mare);
        if (rng.unit() > keep_probability) {
            continue;
        }

        const float family = rng.unit();
        float radius = 0.0F;
        if (family < 0.07F) {
            radius = std::exp(mix(std::log(0.028F), std::log(0.085F), rng.unit()));
        } else if (family < 0.34F) {
            radius = std::exp(mix(std::log(0.010F), std::log(0.034F), rng.unit()));
        } else {
            radius = std::exp(mix(std::log(0.0030F), std::log(0.014F), rng.unit()));
        }
        const float strength = mix(0.32F, 0.82F, rng.unit()) * mix(1.0F, 0.70F, mare);
        craters.push_back(Crater{center, radius, strength});
    }

    const std::array prominent{
        Crater{{-0.10F, -0.58F}, 0.055F, 0.42F}, Crater{{0.36F, -0.42F}, 0.047F, 0.40F},
        Crater{{-0.53F, -0.20F}, 0.041F, 0.36F}, Crater{{0.18F, -0.62F}, 0.035F, 0.34F},
        Crater{{0.52F, -0.06F}, 0.030F, 0.30F},
    };
    craters.insert(craters.end(), prominent.begin(), prominent.end());
    return craters;
}

[[nodiscard]] std::size_t texel_index(std::uint32_t x, std::uint32_t y, std::uint32_t width) {
    return static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + x;
}

void add_crater(std::vector<float>& albedo, std::vector<float>& height, std::uint32_t extent,
                const Crater& crater) {
    const float influence_radius = crater.radius * 2.65F;
    const auto to_pixel = [extent](float coordinate) {
        return static_cast<int>(
            std::floor((coordinate * 0.5F + 0.5F) * static_cast<float>(extent)));
    };
    const int min_x = std::max(0, to_pixel(crater.center.x - influence_radius));
    const int max_x =
        std::min(static_cast<int>(extent) - 1, to_pixel(crater.center.x + influence_radius));
    const int min_y = std::max(0, to_pixel(crater.center.y - influence_radius));
    const int max_y =
        std::min(static_cast<int>(extent) - 1, to_pixel(crater.center.y + influence_radius));

    for (int y = min_y; y <= max_y; ++y) {
        for (int x = min_x; x <= max_x; ++x) {
            const Vec2 position = atlas_position(static_cast<std::uint32_t>(x),
                                                 static_cast<std::uint32_t>(y), extent);
            if (dot(position, position) > 1.02F) {
                continue;
            }
            const float distance = length(position - crater.center) / crater.radius;
            if (distance > 2.65F) {
                continue;
            }

            const float rim = std::exp(-42.0F * (distance - 1.0F) * (distance - 1.0F)) *
                              (1.0F - smoothstep(1.10F, 1.85F, distance));
            const float floor = 1.0F - smoothstep(0.14F, 0.82F, distance);
            const float ejecta =
                std::exp(-2.2F * distance) * (1.0F - smoothstep(1.0F, 2.65F, distance));
            const float size_gain = std::clamp(std::sqrt(crater.radius / 0.018F), 0.55F, 1.55F);
            const std::size_t index =
                texel_index(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y), extent);

            height[index] +=
                crater.strength * size_gain * (0.038F * rim - 0.032F * floor + 0.005F * ejecta);
            albedo[index] += crater.strength * (0.016F * rim - 0.016F * floor + 0.004F * ejecta);
        }
    }
}

[[nodiscard]] float sample_height(const std::vector<float>& height, std::uint32_t extent, int x,
                                  int y) {
    const int clamped_x = std::clamp(x, 0, static_cast<int>(extent) - 1);
    const int clamped_y = std::clamp(y, 0, static_cast<int>(extent) - 1);
    return height[texel_index(static_cast<std::uint32_t>(clamped_x),
                              static_cast<std::uint32_t>(clamped_y), extent)];
}

[[nodiscard]] std::uint8_t pack_unorm(float value) {
    const float clamped = std::clamp(value, 0.0F, 1.0F);
    return static_cast<std::uint8_t>(std::lround(clamped * 255.0F));
}

[[nodiscard]] std::uint8_t pack_signed_normal(float value) {
    return pack_unorm(value * 0.5F + 0.5F);
}

[[nodiscard]] Vec3 unpack_normal(std::uint8_t packed_x, std::uint8_t packed_y) {
    const float nx = (static_cast<float>(packed_x) / 255.0F) * 2.0F - 1.0F;
    const float ny = (static_cast<float>(packed_y) / 255.0F) * 2.0F - 1.0F;
    const float nz = std::sqrt(std::max(1.0F - nx * nx - ny * ny, 0.0F));
    return normalize({nx, ny, nz});
}

[[nodiscard]] std::vector<std::uint8_t> build_base_mip(const std::vector<float>& albedo,
                                                       const std::vector<float>& height,
                                                       std::uint32_t extent) {
    std::vector<std::uint8_t> rgba(static_cast<std::size_t>(extent) *
                                   static_cast<std::size_t>(extent) * 4U);
    const float texel_size = 2.0F / static_cast<float>(extent);
    for (std::uint32_t y = 0; y < extent; ++y) {
        for (std::uint32_t x = 0; x < extent; ++x) {
            const float dx =
                (sample_height(height, extent, static_cast<int>(x) + 1, static_cast<int>(y)) -
                 sample_height(height, extent, static_cast<int>(x) - 1, static_cast<int>(y))) /
                (2.0F * texel_size);
            const float dy =
                (sample_height(height, extent, static_cast<int>(x), static_cast<int>(y) + 1) -
                 sample_height(height, extent, static_cast<int>(x), static_cast<int>(y) - 1)) /
                (2.0F * texel_size);
            const Vec3 normal = normalize({-dx * 0.28F, -dy * 0.28F, 1.0F});
            const std::size_t out = texel_index(x, y, extent) * 4U;
            rgba[out + 0U] = pack_unorm(albedo[texel_index(x, y, extent)]);
            rgba[out + 1U] = pack_signed_normal(std::clamp(normal.x, -0.72F, 0.72F));
            rgba[out + 2U] = pack_signed_normal(std::clamp(normal.y, -0.72F, 0.72F));
            rgba[out + 3U] = 255U;
        }
    }
    return rgba;
}

[[nodiscard]] std::vector<std::uint8_t> downsample_mip(const std::vector<std::uint8_t>& source,
                                                       std::uint32_t source_width,
                                                       std::uint32_t source_height) {
    const std::uint32_t width = std::max(source_width / 2U, 1U);
    const std::uint32_t height = std::max(source_height / 2U, 1U);
    std::vector<std::uint8_t> result(static_cast<std::size_t>(width) *
                                     static_cast<std::size_t>(height) * 4U);

    for (std::uint32_t y = 0; y < height; ++y) {
        for (std::uint32_t x = 0; x < width; ++x) {
            float albedo_sum = 0.0F;
            Vec3 normal_sum{};
            for (std::uint32_t oy = 0; oy < 2U; ++oy) {
                for (std::uint32_t ox = 0; ox < 2U; ++ox) {
                    const std::uint32_t sx = std::min(x * 2U + ox, source_width - 1U);
                    const std::uint32_t sy = std::min(y * 2U + oy, source_height - 1U);
                    const std::size_t in = texel_index(sx, sy, source_width) * 4U;
                    albedo_sum += static_cast<float>(source[in + 0U]) / 255.0F;
                    const Vec3 normal = unpack_normal(source[in + 1U], source[in + 2U]);
                    normal_sum.x += normal.x;
                    normal_sum.y += normal.y;
                    normal_sum.z += normal.z;
                }
            }

            const Vec3 normal = normalize(normal_sum);
            const std::size_t out = texel_index(x, y, width) * 4U;
            result[out + 0U] = pack_unorm(albedo_sum * 0.25F);
            result[out + 1U] = pack_signed_normal(normal.x);
            result[out + 2U] = pack_signed_normal(normal.y);
            result[out + 3U] = 255U;
        }
    }
    return result;
}

void append_mip(LunarAtlas& atlas, const std::vector<std::uint8_t>& mip, std::uint32_t width,
                std::uint32_t height) {
    const std::size_t offset = atlas.rgba8.size();
    atlas.rgba8.insert(atlas.rgba8.end(), mip.begin(), mip.end());
    atlas.mips.push_back(LunarAtlasMip{
        .width = width,
        .height = height,
        .byte_offset = offset,
        .byte_count = mip.size(),
    });
}

} // namespace

std::uint32_t lunar_atlas_mip_count(std::uint32_t extent) {
    if (extent == 0U) {
        throw std::runtime_error("lunar atlas extent must be nonzero");
    }
    if ((extent & (extent - 1U)) != 0U) {
        throw std::runtime_error("lunar atlas extent must be a power of two");
    }

    std::uint32_t levels = 1;
    while (extent > 1U) {
        extent /= 2U;
        ++levels;
    }
    return levels;
}

LunarAtlas generate_lunar_atlas(std::uint32_t extent) {
    const std::uint32_t mip_levels = lunar_atlas_mip_count(extent);
    const std::size_t texel_count = static_cast<std::size_t>(extent) * extent;
    std::vector<float> albedo(texel_count, 0.0F);
    std::vector<float> height(texel_count, 0.0F);

    for (std::uint32_t y = 0; y < extent; ++y) {
        for (std::uint32_t x = 0; x < extent; ++x) {
            const Vec2 disk_position = clamp_to_disk(atlas_position(x, y, extent));
            const float radius = length(disk_position);
            const float limb = smoothstep(0.92F, 1.0F, radius);
            const float maria = maria_mask(disk_position);
            const float broad = centered_fbm(disk_position * 2.8F + Vec2{1.1F, -3.0F}, 131U, 5);
            const float mid = centered_fbm(disk_position * 12.0F + Vec2{-7.0F, 2.0F}, 149U, 4);
            const float fine = centered_fbm(disk_position * 42.0F + Vec2{11.0F, -5.0F}, 157U, 3);
            const float micro = centered_fbm(disk_position * 96.0F + Vec2{-13.0F, 19.0F}, 163U, 2);
            const float rough_highland =
                ridged_fbm(disk_position * 26.0F + Vec2{5.0F, -4.0F}, 179U, 4) - 0.55F;
            const float regional = centered_fbm(disk_position * 6.0F + Vec2{-2.0F, 5.0F}, 181U, 5);
            const float pepper =
                micro_crater_layer(disk_position, 30.0F, 397U, 0.058F) +
                micro_crater_layer(disk_position + Vec2{0.02F, -0.04F}, 70.0F, 421U, 0.034F);
            const float highland_weight = 1.0F - smoothstep(0.02F, 0.58F, maria);
            const float highland_field =
                centered_fbm(disk_position * 3.8F + Vec2{-1.0F, 9.0F}, 431U, 5) * 0.170F +
                (ridged_fbm(disk_position * 6.4F + Vec2{12.0F, -3.0F}, 433U, 4) - 0.55F) * 0.145F +
                centered_fbm(disk_position * 8.2F + Vec2{4.0F, 13.0F}, 437U, 4) * 0.105F;
            const float highland_mottle =
                centered_fbm(disk_position * 11.0F + Vec2{8.0F, -2.0F}, 439U, 5) * 0.105F +
                centered_fbm(disk_position * 18.0F + Vec2{-6.0F, 11.0F}, 443U, 4) * 0.085F +
                (ridged_fbm(disk_position * 27.0F + Vec2{3.0F, 5.0F}, 449U, 4) - 0.54F) * 0.080F;
            const float highland_pores =
                -smoothstep(0.54F, 0.84F,
                            ridged_fbm(disk_position * 34.0F + Vec2{-9.0F, 4.0F}, 457U, 4)) *
                0.080F;
            const float highland_craters =
                micro_crater_layer(disk_position + Vec2{-0.03F, 0.05F}, 12.0F, 463U, 0.135F) +
                micro_crater_layer(disk_position + Vec2{0.06F, 0.01F}, 24.0F, 467U, 0.082F);
            const float highland_breakup = highland_weight * (highland_field + highland_mottle +
                                                              highland_pores + highland_craters);
            const float rays = bright_ray_systems(disk_position);
            const float highlands = 0.565F + broad * 0.085F + mid * 0.055F + fine * 0.042F +
                                    micro * 0.016F + rough_highland * 0.060F;
            const float mare_albedo =
                0.315F + centered_fbm(disk_position * 8.0F + Vec2{4.0F, 8.0F}, 173U, 4) * 0.055F +
                fine * 0.030F + regional * 0.026F;
            const std::size_t index = texel_index(x, y, extent);
            albedo[index] =
                std::clamp(mix(highlands, mare_albedo, maria) + pepper * mix(1.0F, 0.55F, maria) +
                               highland_breakup * 1.35F + rays - limb * 0.018F,
                           0.16F, 0.82F);
            height[index] =
                centered_fbm(disk_position * 3.5F + Vec2{2.0F, -1.0F}, 191U, 5) * 0.032F +
                centered_fbm(disk_position * 22.0F + Vec2{-5.0F, 9.0F}, 211U, 4) * 0.018F +
                centered_fbm(disk_position * 74.0F + Vec2{8.0F, -11.0F}, 223U, 3) * 0.006F +
                pepper * 0.018F + highland_breakup * 0.010F - maria * 0.040F;
        }
    }

    const std::vector<Crater> craters = generate_craters();
    for (const Crater& crater : craters) {
        add_crater(albedo, height, extent, crater);
    }
    for (float& value : albedo) {
        value = std::clamp(value, 0.16F, 0.82F);
    }

    LunarAtlas atlas{
        .width = extent,
        .height = extent,
        .mip_levels = mip_levels,
    };
    atlas.mips.reserve(mip_levels);

    std::vector<std::uint8_t> current = build_base_mip(albedo, height, extent);
    std::uint32_t width = extent;
    std::uint32_t height_extent = extent;
    append_mip(atlas, current, width, height_extent);
    while (width > 1U || height_extent > 1U) {
        current = downsample_mip(current, width, height_extent);
        width = std::max(width / 2U, 1U);
        height_extent = std::max(height_extent / 2U, 1U);
        append_mip(atlas, current, width, height_extent);
    }
    atlas.metadata = cubey::procedural::make_procedural_artifact_metadata(
        cubey::procedural::make_domain_procedural_artifact_identity(
            "atmosphere lunar atlas",
            "cubey::render::generate_lunar_atlas",
            "atmosphere-lunar-atlas-v1",
            "atmosphere.lunar_atlas",
            cubey::procedural::ProceduralDomainSpace::Atlas),
        cubey::procedural::ProceduralArtifactKind::Texture2D,
        cubey::procedural::ProceduralArtifactValueFormat::Rgba8Unorm,
        {.width = extent, .height = extent, .depth = 1, .faces = 1, .mip_levels = mip_levels},
        lunar_atlas_hash(atlas.rgba8));
    return atlas;
}

std::uint64_t lunar_atlas_hash(std::span<const std::uint8_t> bytes) {
    return cubey::procedural::procedural_hash_bytes(bytes);
}

} // namespace cubey::render
