#include "procedural_terrain_fields.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <vector>

namespace cubey::projects::procedural_terrain {
namespace {

constexpr float kDistanceInfinity = 1.0e20F;

[[nodiscard]] float saturate(float value) {
    return std::clamp(value, 0.0F, 1.0F);
}

[[nodiscard]] float smoothstep(float edge0, float edge1, float value) {
    const float t = saturate((value - edge0) / (edge1 - edge0));
    return t * t * (3.0F - (2.0F * t));
}

[[nodiscard]] float lerp(float a, float b, float t) {
    return a + ((b - a) * t);
}

[[nodiscard]] std::uint32_t hash_u32(std::int32_t x, std::int32_t y, std::uint64_t seed) {
    std::uint64_t value = seed;
    value ^= static_cast<std::uint32_t>(x) + 0x9e37'79b9U + (value << 6U) + (value >> 2U);
    value ^= static_cast<std::uint32_t>(y) + 0x85eb'ca6bU + (value << 6U) + (value >> 2U);
    value ^= value >> 33U;
    value *= 0xff51afd7ed558ccdULL;
    value ^= value >> 33U;
    value *= 0xc4ceb9fe1a85ec53ULL;
    value ^= value >> 33U;
    return static_cast<std::uint32_t>(value);
}

[[nodiscard]] float value_noise(float x, float y, std::uint64_t seed) {
    const float floor_x = std::floor(x);
    const float floor_y = std::floor(y);
    const auto x0 = static_cast<std::int32_t>(floor_x);
    const auto y0 = static_cast<std::int32_t>(floor_y);
    const float tx = smoothstep(0.0F, 1.0F, x - floor_x);
    const float ty = smoothstep(0.0F, 1.0F, y - floor_y);

    const auto corner = [seed](std::int32_t ix, std::int32_t iy) {
        constexpr float kScale =
            1.0F / static_cast<float>(std::numeric_limits<std::uint32_t>::max());
        return (static_cast<float>(hash_u32(ix, iy, seed)) * kScale * 2.0F) - 1.0F;
    };

    const float a = lerp(corner(x0, y0), corner(x0 + 1, y0), tx);
    const float b = lerp(corner(x0, y0 + 1), corner(x0 + 1, y0 + 1), tx);
    return lerp(a, b, ty);
}

[[nodiscard]] float fbm(float x, float y, std::uint64_t seed, std::uint32_t octaves) {
    float frequency = 1.0F;
    float amplitude = 0.5F;
    float sum = 0.0F;
    float weight = 0.0F;
    for (std::uint32_t octave = 0; octave < octaves; ++octave) {
        sum += value_noise(x * frequency, y * frequency, seed + octave * 1013U) * amplitude;
        weight += amplitude;
        frequency *= 2.04F;
        amplitude *= 0.52F;
    }
    return weight == 0.0F ? 0.0F : sum / weight;
}

[[nodiscard]] float ridged(float x, float y, std::uint64_t seed) {
    const float noise = fbm(x, y, seed, 4);
    const float ridge = 1.0F - std::abs(noise);
    return ridge * ridge;
}

[[nodiscard]] std::vector<float> squared_distance_to_mask(const std::vector<bool>& mask,
                                                          std::uint32_t width, std::uint32_t height,
                                                          bool feature_value) {
    const std::size_t count = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    std::vector<float> source(count, kDistanceInfinity);
    std::size_t feature_count = 0;
    for (std::size_t index = 0; index < count; ++index) {
        if (mask[index] == feature_value) {
            source[index] = 0.0F;
            ++feature_count;
        }
    }
    if (feature_count == 0) {
        return source;
    }

    auto transform_1d = [](const std::vector<float>& f, std::vector<float>& d) {
        const auto n = static_cast<std::int32_t>(f.size());
        std::vector<std::int32_t> v(f.size());
        std::vector<float> z(f.size() + 1U);
        std::int32_t k = 0;
        v[0] = 0;
        z[0] = -std::numeric_limits<float>::infinity();
        z[1] = std::numeric_limits<float>::infinity();
        for (std::int32_t q = 1; q < n; ++q) {
            float s = 0.0F;
            do {
                const std::int32_t p = v[static_cast<std::size_t>(k)];
                s = ((f[static_cast<std::size_t>(q)] + static_cast<float>(q * q)) -
                     (f[static_cast<std::size_t>(p)] + static_cast<float>(p * p))) /
                    static_cast<float>((2 * q) - (2 * p));
                if (s <= z[static_cast<std::size_t>(k)]) {
                    --k;
                }
            } while (k >= 0 && s <= z[static_cast<std::size_t>(k)]);
            ++k;
            v[static_cast<std::size_t>(k)] = q;
            z[static_cast<std::size_t>(k)] = s;
            z[static_cast<std::size_t>(k + 1)] = std::numeric_limits<float>::infinity();
        }
        k = 0;
        for (std::int32_t q = 0; q < n; ++q) {
            while (z[static_cast<std::size_t>(k + 1)] < static_cast<float>(q)) {
                ++k;
            }
            const std::int32_t p = v[static_cast<std::size_t>(k)];
            const float delta = static_cast<float>(q - p);
            d[static_cast<std::size_t>(q)] = (delta * delta) + f[static_cast<std::size_t>(p)];
        }
    };

    std::vector<float> tmp(count);
    std::vector<float> row_source(width);
    std::vector<float> row_distance(width);
    for (std::uint32_t y = 0; y < height; ++y) {
        const std::size_t row_offset = static_cast<std::size_t>(y) * width;
        std::copy_n(source.begin() + static_cast<std::ptrdiff_t>(row_offset), width,
                    row_source.begin());
        transform_1d(row_source, row_distance);
        std::copy(row_distance.begin(), row_distance.end(),
                  tmp.begin() + static_cast<std::ptrdiff_t>(row_offset));
    }

    std::vector<float> result(count);
    std::vector<float> column_source(height);
    std::vector<float> column_distance(height);
    for (std::uint32_t x = 0; x < width; ++x) {
        for (std::uint32_t y = 0; y < height; ++y) {
            column_source[y] = tmp[static_cast<std::size_t>(y) * width + x];
        }
        transform_1d(column_source, column_distance);
        for (std::uint32_t y = 0; y < height; ++y) {
            result[static_cast<std::size_t>(y) * width + x] = column_distance[y];
        }
    }
    return result;
}

[[nodiscard]] TerrainMaterialMask normalized_mask(float sand, float rock, float vegetation,
                                                  float sediment, bool underwater) {
    sand = std::max(sand, 0.0F);
    rock = std::max(rock, 0.0F);
    vegetation = std::max(vegetation, 0.0F);
    sediment = std::max(sediment, 0.0F);
    float sum = sand + rock + vegetation + sediment;
    if (sum <= 0.0001F) {
        sediment = underwater ? 1.0F : 0.0F;
        sand = underwater ? 0.0F : 1.0F;
        rock = 0.0F;
        vegetation = 0.0F;
        sum = 1.0F;
    }
    return {
        .sand = sand / sum,
        .rock = rock / sum,
        .vegetation = vegetation / sum,
        .sediment = sediment / sum,
    };
}

} // namespace

std::size_t TerrainFieldData::sample_count() const {
    return static_cast<std::size_t>(desc.width) * static_cast<std::size_t>(desc.height);
}

std::size_t TerrainFieldData::index(std::uint32_t x, std::uint32_t y) const {
    if (x >= desc.width || y >= desc.height) {
        throw std::runtime_error("terrain field index is out of bounds");
    }
    return static_cast<std::size_t>(y) * desc.width + x;
}

TerrainFieldData generate_terrain_fields(const TerrainConfig& config) {
    validate_terrain_config(config);

    TerrainFieldData fields;
    fields.desc = {
        .seed = config.seed,
        .width = config.grid_width,
        .height = config.grid_height,
        .cell_size_m = config.cell_size_m,
        .sea_level_m = config.sea_level_m,
    };
    const std::size_t count = fields.sample_count();
    fields.height_m.resize(count);
    fields.water_depth_m.resize(count);
    fields.shore_sdf_m.resize(count);
    fields.slope.resize(count);
    fields.material_masks.resize(count);

    std::vector<bool> land_mask(count, false);
    for (std::uint32_t y = 0; y < fields.desc.height; ++y) {
        const float v = fields.desc.height == 1U
                            ? 0.0F
                            : (static_cast<float>(y) / static_cast<float>(fields.desc.height - 1U));
        for (std::uint32_t x = 0; x < fields.desc.width; ++x) {
            const float u =
                fields.desc.width == 1U
                    ? 0.0F
                    : (static_cast<float>(x) / static_cast<float>(fields.desc.width - 1U));
            float px = (u * 2.0F) - 1.0F;
            float py = (v * 2.0F) - 1.0F;
            px += value_noise(px * 1.8F, py * 1.8F, config.seed + 17U) * 0.08F;
            py += value_noise(px * 1.6F + 9.2F, py * 1.6F - 4.7F, config.seed + 29U) * 0.08F;
            const float radius = std::sqrt((px * px * 0.92F) + (py * py * 1.12F));
            const float island = 1.0F - std::pow(radius, 1.85F);
            const float coast_noise =
                fbm(px * 2.2F + 11.0F, py * 2.2F - 3.0F, config.seed + 41U, 4);
            const float land_potential = island - 0.18F + (coast_noise * 0.30F);
            land_mask[fields.index(x, y)] = land_potential >= 0.0F;
        }
    }

    const std::vector<float> squared_to_water =
        squared_distance_to_mask(land_mask, fields.desc.width, fields.desc.height, false);
    const std::vector<float> squared_to_land =
        squared_distance_to_mask(land_mask, fields.desc.width, fields.desc.height, true);

    fields.min_height_m = std::numeric_limits<float>::max();
    fields.max_height_m = std::numeric_limits<float>::lowest();
    for (std::uint32_t y = 0; y < fields.desc.height; ++y) {
        const float z =
            (static_cast<float>(y) - (static_cast<float>(fields.desc.height - 1U) * 0.5F)) *
            fields.desc.cell_size_m;
        for (std::uint32_t x = 0; x < fields.desc.width; ++x) {
            const float world_x =
                (static_cast<float>(x) - (static_cast<float>(fields.desc.width - 1U) * 0.5F)) *
                fields.desc.cell_size_m;
            const std::size_t sample = fields.index(x, y);
            const bool land = land_mask[sample];
            const float shore_distance_cells = std::sqrt(
                std::max(0.0F, land ? squared_to_water[sample] : squared_to_land[sample]));
            const float shore_sdf =
                (land ? shore_distance_cells : -shore_distance_cells) * fields.desc.cell_size_m;
            fields.shore_sdf_m[sample] = shore_sdf;
            fields.max_abs_shore_sdf_m = std::max(fields.max_abs_shore_sdf_m, std::abs(shore_sdf));

            const float nx = world_x / 280.0F;
            const float nz = z / 280.0F;
            float height = fields.desc.sea_level_m;
            if (land) {
                const float coast = smoothstep(0.0F, 64.0F, shore_sdf);
                const float inland = smoothstep(55.0F, 230.0F, shore_sdf);
                const float broad = fbm(nx, nz, config.seed + 101U, 5) * 22.0F;
                const float ridge =
                    ridged(nx * 1.7F + 8.0F, nz * 1.7F - 5.0F, config.seed + 211U) * 48.0F * inland;
                height = fields.desc.sea_level_m + 0.5F + (coast * 12.0F) +
                         (inland * (42.0F + broad + ridge));
            } else {
                const float depth_distance = -shore_sdf;
                const float near = smoothstep(0.0F, 96.0F, depth_distance);
                const float shelf = smoothstep(110.0F, 420.0F, depth_distance);
                const float abyss = smoothstep(420.0F, 880.0F, depth_distance);
                const float seabed =
                    fbm(nx * 0.8F - 3.0F, nz * 0.8F + 5.0F, config.seed + 307U, 4) *
                    (4.0F + (shelf * 8.0F));
                const float depth =
                    1.5F + (near * 12.0F) + (shelf * 54.0F) + (abyss * 120.0F) + seabed;
                height = fields.desc.sea_level_m - std::max(depth, 0.5F);
            }

            fields.height_m[sample] = height;
            fields.water_depth_m[sample] = std::max(0.0F, fields.desc.sea_level_m - height);
            fields.max_water_depth_m =
                std::max(fields.max_water_depth_m, fields.water_depth_m[sample]);
            fields.min_height_m = std::min(fields.min_height_m, height);
            fields.max_height_m = std::max(fields.max_height_m, height);
        }
    }

    for (std::uint32_t y = 0; y < fields.desc.height; ++y) {
        for (std::uint32_t x = 0; x < fields.desc.width; ++x) {
            const std::uint32_t left = x == 0U ? x : x - 1U;
            const std::uint32_t right = std::min(x + 1U, fields.desc.width - 1U);
            const std::uint32_t down = y == 0U ? y : y - 1U;
            const std::uint32_t up = std::min(y + 1U, fields.desc.height - 1U);
            const float dx = static_cast<float>(right - left) * fields.desc.cell_size_m;
            const float dz = static_cast<float>(up - down) * fields.desc.cell_size_m;
            const float dhdx =
                (fields.height_m[fields.index(right, y)] - fields.height_m[fields.index(left, y)]) /
                std::max(dx, fields.desc.cell_size_m);
            const float dhdz =
                (fields.height_m[fields.index(x, up)] - fields.height_m[fields.index(x, down)]) /
                std::max(dz, fields.desc.cell_size_m);
            const std::size_t sample = fields.index(x, y);
            const float slope = std::sqrt((dhdx * dhdx) + (dhdz * dhdz));
            fields.slope[sample] = slope;

            const float height = fields.height_m[sample];
            const float water_depth = fields.water_depth_m[sample];
            const float shore = std::abs(fields.shore_sdf_m[sample]);
            const bool underwater = water_depth > 0.0F;
            const float low_slope = 1.0F - smoothstep(0.12F, 0.55F, slope);
            const float steep = smoothstep(0.35F, 1.10F, slope);
            const float sand = low_slope * (1.0F - smoothstep(18.0F, 95.0F, shore)) *
                               (1.0F - smoothstep(20.0F, 45.0F, height));
            const float rock = std::max(steep, smoothstep(55.0F, 120.0F, height)) *
                               (1.0F - smoothstep(0.0F, 18.0F, water_depth));
            const float vegetation = (underwater ? 0.0F : 1.0F) * (1.0F - steep) *
                                     smoothstep(10.0F, 42.0F, height) *
                                     (1.0F - smoothstep(110.0F, 150.0F, height));
            const float sediment =
                (underwater ? 1.0F : 0.0F) * low_slope * smoothstep(2.0F, 28.0F, water_depth);
            fields.material_masks[sample] =
                normalized_mask(sand, rock, vegetation, sediment, underwater);
        }
    }

    return fields;
}

} // namespace cubey::projects::procedural_terrain
