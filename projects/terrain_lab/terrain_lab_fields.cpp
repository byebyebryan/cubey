#include "terrain_lab_fields.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <vector>

namespace cubey::projects::terrain_lab {
namespace {

struct Point2 {
    float x = 0.0F;
    float z = 0.0F;
};

constexpr float kMaterialMaskTolerance = 0.001F;
constexpr std::uint8_t kFlowSinkDirection = 8U;
constexpr std::uint32_t kTerrainLabWatershedCount = 4U;

constexpr std::array<std::int32_t, 8> kFlowDx{-1, 0, 1, -1, 1, -1, 0, 1};
constexpr std::array<std::int32_t, 8> kFlowDy{-1, -1, -1, 0, 0, 1, 1, 1};

struct WatershedBasinFeature {
    Point2 center{};
    float outlet_x = 0.0F;
    float bend_phase = 0.0F;
    float tributary_side = 1.0F;
};

struct WatershedSampleFeatures {
    std::uint32_t watershed_id = 0;
    float divide_influence = 0.0F;
    float channel_influence = 0.0F;
    float channel_distance_m = 0.0F;
};

struct AridMesaSampleFeatures {
    float canyon_floor = 0.0F;
    float canyon_wall = 0.0F;
    float wash_influence = 0.0F;
    float plateau_influence = 0.0F;
    float rim_influence = 0.0F;
    float bench_influence = 0.0F;
    float talus_influence = 0.0F;
    float ridge_influence = 0.0F;
    float valley_influence = 0.0F;
    float basin_influence = 0.0F;
    float divide_influence = 0.0F;
    float channel_influence = 0.0F;
    float channel_distance_m = 0.0F;
};

struct DesertDuneSampleFeatures {
    float dune_crest = 0.0F;
    float slip_face = 0.0F;
    float interdune_flat = 0.0F;
    float wind_shadow = 0.0F;
    float ridge_influence = 0.0F;
    float valley_influence = 0.0F;
    float basin_influence = 0.0F;
    float divide_influence = 0.0F;
    float channel_influence = 0.0F;
    float channel_distance_m = 0.0F;
};

struct AlpineGlacialSampleFeatures {
    float glacier_floor = 0.0F;
    float valley_wall = 0.0F;
    float hanging_valley = 0.0F;
    float moraine_influence = 0.0F;
    float cirque_influence = 0.0F;
    float peak_influence = 0.0F;
    float ridge_influence = 0.0F;
    float valley_influence = 0.0F;
    float basin_influence = 0.0F;
    float divide_influence = 0.0F;
    float channel_influence = 0.0F;
    float channel_distance_m = 0.0F;
};

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

[[nodiscard]] float fract(float value) {
    return value - std::floor(value);
}

[[nodiscard]] float length(Point2 p) {
    return std::sqrt((p.x * p.x) + (p.z * p.z));
}

[[nodiscard]] float dot(Point2 a, Point2 b) {
    return (a.x * b.x) + (a.z * b.z);
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

[[nodiscard]] float random01(std::uint64_t seed, std::uint32_t index, std::uint32_t channel) {
    constexpr float kScale = 1.0F / static_cast<float>(std::numeric_limits<std::uint32_t>::max());
    return static_cast<float>(hash_u32(static_cast<std::int32_t>(index),
                                       static_cast<std::int32_t>(channel), seed)) *
           kScale;
}

[[nodiscard]] float value_noise(float x, float z, std::uint64_t seed) {
    const float floor_x = std::floor(x);
    const float floor_z = std::floor(z);
    const auto x0 = static_cast<std::int32_t>(floor_x);
    const auto z0 = static_cast<std::int32_t>(floor_z);
    const float tx = smoothstep(0.0F, 1.0F, x - floor_x);
    const float tz = smoothstep(0.0F, 1.0F, z - floor_z);

    const auto corner = [seed](std::int32_t ix, std::int32_t iz) {
        constexpr float kScale =
            1.0F / static_cast<float>(std::numeric_limits<std::uint32_t>::max());
        return (static_cast<float>(hash_u32(ix, iz, seed)) * kScale * 2.0F) - 1.0F;
    };

    const float a = lerp(corner(x0, z0), corner(x0 + 1, z0), tx);
    const float b = lerp(corner(x0, z0 + 1), corner(x0 + 1, z0 + 1), tx);
    return lerp(a, b, tz);
}

[[nodiscard]] float fbm(float x, float z, std::uint64_t seed, std::uint32_t octaves) {
    float frequency = 1.0F;
    float amplitude = 0.5F;
    float sum = 0.0F;
    float weight = 0.0F;
    for (std::uint32_t octave = 0; octave < octaves; ++octave) {
        sum += value_noise(x * frequency, z * frequency, seed + octave * 1009U) * amplitude;
        weight += amplitude;
        frequency *= 2.03F;
        amplitude *= 0.52F;
    }
    return weight == 0.0F ? 0.0F : sum / weight;
}

[[nodiscard]] std::size_t grid_index(std::uint32_t x, std::uint32_t y, std::uint32_t width) {
    return static_cast<std::size_t>(y) * width + x;
}

[[nodiscard]] float half_extent_x_m(const TerrainLabGridDesc& desc) {
    return static_cast<float>(desc.width - 1U) * desc.cell_size_m * 0.5F;
}

[[nodiscard]] float half_extent_z_m(const TerrainLabGridDesc& desc) {
    return static_cast<float>(desc.height - 1U) * desc.cell_size_m * 0.5F;
}

[[nodiscard]] Point2 normalized_sample(const TerrainLabGridDesc& desc, std::uint32_t x,
                                       std::uint32_t y) {
    const float half_x = half_extent_x_m(desc);
    const float half_z = half_extent_z_m(desc);
    return {
        .x = half_x == 0.0F ? 0.0F : terrain_lab_grid_sample_x_m(desc, x) / half_x,
        .z = half_z == 0.0F ? 0.0F : terrain_lab_grid_sample_z_m(desc, y) / half_z,
    };
}

[[nodiscard]] Point2 warp_sample_for_watershed(Point2 p, const TerrainLabConfig& config) {
    return {
        .x = p.x + fbm((p.x * 1.6F) - 5.0F, (p.z * 1.6F) + 1.0F, config.seed + 1301U, 4) * 0.075F,
        .z = p.z + fbm((p.x * 1.7F) + 3.0F, (p.z * 1.7F) - 6.0F, config.seed + 1303U, 4) * 0.060F,
    };
}

[[nodiscard]] std::array<WatershedBasinFeature, kTerrainLabWatershedCount>
watershed_basins(const TerrainLabConfig& config) {
    std::array<WatershedBasinFeature, kTerrainLabWatershedCount> basins{};
    for (std::uint32_t index = 0; index < kTerrainLabWatershedCount; ++index) {
        const bool right = (index % 2U) != 0U;
        const bool downstream = index >= 2U;
        const float base_x = right ? 0.46F : -0.46F;
        const float base_z = downstream ? 0.34F : -0.42F;
        basins[index] = {
            .center =
                {
                    base_x + lerp(-0.12F, 0.12F, random01(config.seed, index, 401U)),
                    base_z + lerp(-0.10F, 0.10F, random01(config.seed, index, 409U)),
                },
            .outlet_x = base_x * 0.34F + lerp(-0.10F, 0.10F, random01(config.seed, index, 419U)),
            .bend_phase = random01(config.seed, index, 431U) * 6.28318530718F,
            .tributary_side = right ? -1.0F : 1.0F,
        };
    }
    return basins;
}

[[nodiscard]] WatershedSampleFeatures
watershed_features_at(Point2 p, const TerrainLabGridDesc& desc,
                      const std::array<WatershedBasinFeature, kTerrainLabWatershedCount>& basins,
                      const TerrainLabConfig& config) {
    const Point2 warped = warp_sample_for_watershed(p, config);
    float best_distance = std::numeric_limits<float>::max();
    float second_distance = std::numeric_limits<float>::max();
    std::uint32_t watershed_id = 0;
    for (std::uint32_t index = 0; index < kTerrainLabWatershedCount; ++index) {
        const Point2 offset{warped.x - basins[index].center.x, warped.z - basins[index].center.z};
        const float warp =
            fbm((warped.x * 2.1F) + static_cast<float>(index),
                (warped.z * 2.1F) - static_cast<float>(index), config.seed + 1709U + index, 3) *
            0.045F;
        const float distance = (offset.x * offset.x * 0.96F) + (offset.z * offset.z * 0.78F) + warp;
        if (distance < best_distance) {
            second_distance = best_distance;
            best_distance = distance;
            watershed_id = index;
        } else {
            second_distance = std::min(second_distance, distance);
        }
    }

    const WatershedBasinFeature basin = basins[watershed_id];
    const float t = saturate((warped.z + 1.0F) * 0.5F);
    const float main_channel_x = lerp(basin.center.x * 0.72F, basin.outlet_x, t) +
                                 std::sin((t * 3.14159265359F) + basin.bend_phase) * 0.15F;
    const float main_distance = std::abs(warped.x - main_channel_x);
    const float main_channel =
        (1.0F - smoothstep(0.026F, 0.18F, main_distance)) * smoothstep(-0.92F, -0.56F, warped.z);

    const float tributary_center = main_channel_x + basin.tributary_side * (0.25F - (t * 0.13F));
    const float tributary_line =
        tributary_center + basin.tributary_side * (warped.z - basin.center.z) * 0.38F;
    const float tributary_distance = std::abs(warped.x - tributary_line);
    const float tributary_gate =
        smoothstep(-0.72F, -0.05F, warped.z) * (1.0F - smoothstep(0.34F, 0.86F, warped.z));
    const float tributary_channel =
        (1.0F - smoothstep(0.02F, 0.14F, tributary_distance)) * tributary_gate * 0.52F;

    const float divide_noise =
        fbm((p.x * 4.8F) + 8.0F, (p.z * 4.8F) - 11.0F, config.seed + 1723U, 4) * 0.5F + 0.5F;
    const float distance_gap = std::max(second_distance - best_distance, 0.0F);
    const float divide_influence =
        (1.0F - smoothstep(0.018F, 0.30F, distance_gap)) * lerp(0.58F, 0.86F, divide_noise);
    const float channel_influence =
        saturate((main_channel + tributary_channel) * (1.0F - divide_influence * 0.45F));
    const float channel_distance_norm = std::min(main_distance, tributary_distance);
    const float channel_distance_m =
        channel_distance_norm * std::max(half_extent_x_m(desc), half_extent_z_m(desc));
    return {
        .watershed_id = watershed_id,
        .divide_influence = saturate(divide_influence),
        .channel_influence = channel_influence,
        .channel_distance_m = std::max(channel_distance_m, 0.0F),
    };
}

void rasterize_watershed_features(const TerrainLabConfig& config, TerrainLabFieldData& fields) {
    const std::array<WatershedBasinFeature, kTerrainLabWatershedCount> basins =
        watershed_basins(config);
    fields.watershed_count = kTerrainLabWatershedCount;
    fields.max_channel_distance_m = 0.0F;
    for (std::uint32_t y = 0; y < fields.desc.height; ++y) {
        for (std::uint32_t x = 0; x < fields.desc.width; ++x) {
            const std::size_t sample = fields.index(x, y);
            const WatershedSampleFeatures features = watershed_features_at(
                normalized_sample(fields.desc, x, y), fields.desc, basins, config);
            fields.watershed_id[sample] = features.watershed_id;
            fields.divide_influence[sample] = features.divide_influence;
            fields.channel_influence[sample] = features.channel_influence;
            fields.channel_distance_m[sample] = features.channel_distance_m;
            fields.max_channel_distance_m =
                std::max(fields.max_channel_distance_m, features.channel_distance_m);
        }
    }
}

[[nodiscard]] float ridge_influence(Point2 p, const TerrainLabConfig& config) {
    const float angle = lerp(-0.92F, -0.48F, random01(config.seed, 3U, 11U));
    const Point2 axis{std::cos(angle), std::sin(angle)};
    const Point2 normal{-axis.z, axis.x};
    const float along = dot(p, axis);
    const float offset = lerp(-0.10F, 0.14F, random01(config.seed, 3U, 13U));
    const float phase = random01(config.seed, 3U, 17U) * 6.28318530718F;
    const float curve = std::sin((along * 4.2F) + phase) * 0.095F;
    const float warp = fbm(p.x * 2.5F + 4.0F, p.z * 2.5F - 7.0F, config.seed + 37U, 4) * 0.22F;
    const float distance = std::abs(dot(p, normal) - offset + warp + curve);
    const float main_ridge = 1.0F - smoothstep(0.08F, 0.48F, distance);
    const float shoulder = 1.0F - smoothstep(0.24F, 0.88F, std::abs(along - 0.15F));
    const float broken =
        smoothstep(0.16F, 0.88F,
                   (fbm(p.x * 5.8F - 1.0F, p.z * 5.8F + 2.0F, config.seed + 73U, 4) * 0.5F) + 0.5F);
    return saturate(main_ridge * (0.42F + shoulder * 0.18F + broken * 0.22F));
}

[[nodiscard]] float basin_influence(Point2 p, const TerrainLabConfig& config) {
    const Point2 basin_center{
        lerp(-0.22F, 0.18F, random01(config.seed, 4U, 19U)),
        lerp(0.16F, 0.42F, random01(config.seed, 4U, 23U)),
    };
    const float distance = length({p.x - basin_center.x, p.z - basin_center.z});
    return 1.0F - smoothstep(0.22F, 0.92F, distance);
}

[[nodiscard]] float valley_influence(Point2 p, const TerrainLabConfig& config) {
    const float bend =
        std::sin((p.z + 1.0F) * 2.6F + random01(config.seed, 5U, 29U) * 6.28318530718F) * 0.16F;
    const float tributary =
        std::sin((p.z + 0.4F) * 5.1F + random01(config.seed, 5U, 31U) * 6.28318530718F) * 0.055F;
    const float center = -0.08F + bend + tributary;
    const float distance = std::abs(p.x - center);
    const float main_channel = 1.0F - smoothstep(0.025F, 0.22F, distance);
    const float downstream = smoothstep(-0.92F, 0.84F, p.z);
    const float side_valleys =
        (1.0F - smoothstep(0.03F, 0.18F, std::abs(p.x + (p.z * 0.48F) + 0.30F))) *
        smoothstep(-0.72F, 0.20F, p.z);
    return saturate((main_channel * (0.48F + downstream * 0.52F)) + side_valleys * 0.38F);
}

[[nodiscard]] TerrainLabMaterialMask normalized_material_mask(float rock, float soil, float scree,
                                                              float meadow, float forest,
                                                              float snow, float sand) {
    rock = std::max(rock, 0.0F);
    soil = std::max(soil, 0.0F);
    scree = std::max(scree, 0.0F);
    meadow = std::max(meadow, 0.0F);
    forest = std::max(forest, 0.0F);
    snow = std::max(snow, 0.0F);
    sand = std::max(sand, 0.0F);
    const float sum = rock + soil + scree + meadow + forest + snow + sand;
    if (sum <= 0.0F) {
        return {.soil = 1.0F};
    }
    const float inv_sum = 1.0F / sum;
    return {
        .rock = rock * inv_sum,
        .soil = soil * inv_sum,
        .scree = scree * inv_sum,
        .meadow = meadow * inv_sum,
        .forest = forest * inv_sum,
        .snow = snow * inv_sum,
        .sand = sand * inv_sum,
    };
}

[[nodiscard]] float material_entropy(const TerrainLabMaterialMask& mask) {
    const std::array<float, 7> weights{
        mask.rock, mask.soil, mask.scree, mask.meadow, mask.forest, mask.snow, mask.sand,
    };
    float entropy = 0.0F;
    for (const float weight : weights) {
        if (weight > 0.0F) {
            entropy -= weight * std::log(weight);
        }
    }
    return entropy / std::log(static_cast<float>(weights.size()));
}

[[nodiscard]] TerrainLabFieldData make_empty_terrain_lab_fields(const TerrainLabConfig& config) {
    TerrainLabFieldData fields;
    fields.desc = {
        .seed = config.seed,
        .width = config.grid_width,
        .height = config.grid_height,
        .cell_size_m = config.cell_size_m,
        .origin_x_m = 0.0F,
        .origin_z_m = 0.0F,
    };

    const std::size_t count = fields.sample_count();
    fields.height_m.assign(count, 0.0F);
    fields.structure_height_m.assign(count, 0.0F);
    fields.process_delta_m.assign(count, 0.0F);
    fields.detail_height_m.assign(count, 0.0F);
    fields.slope.assign(count, 0.0F);
    fields.curvature.assign(count, 0.0F);
    fields.flow_direction.assign(count, kFlowSinkDirection);
    fields.flow_accumulation.assign(count, 1.0F);
    fields.stream_power.assign(count, 0.0F);
    fields.wetness.assign(count, 0.0F);
    fields.deposition.assign(count, 0.0F);
    fields.material_masks.assign(count, {});
    fields.grass_density.assign(count, 0.0F);
    fields.shrub_density.assign(count, 0.0F);
    fields.tree_density.assign(count, 0.0F);
    fields.canopy_height_m.assign(count, 0.0F);
    fields.ridge_influence.assign(count, 0.0F);
    fields.valley_influence.assign(count, 0.0F);
    fields.basin_influence.assign(count, 0.0F);
    fields.watershed_id.assign(count, 0U);
    fields.divide_influence.assign(count, 0.0F);
    fields.channel_influence.assign(count, 0.0F);
    fields.channel_distance_m.assign(count, 0.0F);
    return fields;
}

[[nodiscard]] AridMesaSampleFeatures arid_mesa_features_at(Point2 p, const TerrainLabGridDesc& desc,
                                                           const TerrainLabConfig& config) {
    const float phase = random01(config.seed, 11U, 701U) * 6.28318530718F;
    const float canyon_noise =
        fbm((p.x * 2.4F) - 3.0F, (p.z * 2.4F) + 5.0F, config.seed + 2609U, 4);
    const float canyon_center = lerp(-0.10F, 0.06F, random01(config.seed, 11U, 709U)) +
                                (p.z * 0.18F) + std::sin((p.z * 2.20F) + phase) * 0.045F +
                                canyon_noise * 0.035F;
    const float canyon_distance = std::abs(p.x - canyon_center);
    const float downstream = smoothstep(-0.95F, 0.92F, p.z);
    const float canyon_floor =
        (1.0F - smoothstep(0.046F, 0.165F, canyon_distance)) * lerp(0.82F, 1.0F, downstream);
    const float canyon_broad = 1.0F - smoothstep(0.18F, 0.64F, canyon_distance);
    const float canyon_wall = smoothstep(0.068F, 0.20F, canyon_distance) *
                              (1.0F - smoothstep(0.26F, 0.48F, canyon_distance));
    const float rim_influence = smoothstep(0.14F, 0.28F, canyon_distance) *
                                (1.0F - smoothstep(0.30F, 0.50F, canyon_distance));
    const float bench_influence = smoothstep(0.30F, 0.50F, canyon_distance) *
                                  (1.0F - smoothstep(0.62F, 0.88F, canyon_distance));
    const float talus_influence = smoothstep(0.12F, 0.24F, canyon_distance) *
                                  (1.0F - smoothstep(0.28F, 0.46F, canyon_distance));

    const float left_t = smoothstep(-0.78F, -0.18F, p.z) * (1.0F - smoothstep(-0.05F, 0.24F, p.z));
    const float left_join_t = saturate((p.z + 0.78F) / 0.72F);
    const float left_wash_center =
        canyon_center - (0.46F * (1.0F - left_join_t)) +
        std::sin(left_join_t * 3.14159265359F) * 0.070F +
        fbm((p.x * 3.7F) - 2.0F, (p.z * 3.7F) + 4.0F, config.seed + 2611U, 3) * 0.026F;
    const float left_side_gate =
        1.0F - smoothstep(canyon_center - 0.04F, canyon_center + 0.08F, p.x);
    const float left_distance = std::abs(p.x - left_wash_center);
    const float left_wash =
        (1.0F - smoothstep(0.030F, 0.130F, left_distance)) * left_t * left_side_gate;

    const float right_t = smoothstep(-0.22F, 0.42F, p.z) * (1.0F - smoothstep(0.54F, 0.84F, p.z));
    const float right_join_t = saturate((p.z + 0.22F) / 0.76F);
    const float right_wash_center =
        canyon_center + (0.48F * (1.0F - right_join_t)) -
        std::sin(right_join_t * 3.14159265359F) * 0.060F +
        fbm((p.x * 3.9F) + 6.0F, (p.z * 3.9F) - 9.0F, config.seed + 2613U, 3) * 0.024F;
    const float right_side_gate = smoothstep(canyon_center - 0.08F, canyon_center + 0.04F, p.x);
    const float right_distance = std::abs(p.x - right_wash_center);
    const float right_wash =
        (1.0F - smoothstep(0.030F, 0.125F, right_distance)) * right_t * right_side_gate;

    const float wash_patch = smoothstep(
        0.18F, 0.80F,
        fbm((p.x * 5.2F) + 4.0F, (p.z * 5.2F) - 8.0F, config.seed + 2617U, 4) * 0.5F + 0.5F);
    const float wash_influence =
        saturate(std::max(left_wash, right_wash) * lerp(0.46F, 0.72F, wash_patch));
    const float wash_distance = std::min(left_distance, right_distance);
    const float channel_distance_norm = std::min(canyon_distance, wash_distance);
    const float extent_m = std::max(half_extent_x_m(desc), half_extent_z_m(desc));

    const float plateau_noise =
        fbm((p.x * 1.35F) + 9.0F, (p.z * 1.35F) - 3.0F, config.seed + 2621U, 4) * 0.5F + 0.5F;
    const float plateau = saturate((1.0F - canyon_broad * 0.58F - wash_influence * 0.08F) *
                                   lerp(0.78F, 1.08F, plateau_noise));
    const float channel = saturate(std::max(canyon_floor, wash_influence * 0.22F));
    const float valley = saturate((canyon_broad * 0.80F) + (wash_influence * 0.10F));
    const float basin = saturate((canyon_floor * 0.76F) + (wash_influence * 0.04F));
    const float ridge =
        saturate((canyon_wall * 0.72F) + (rim_influence * 0.42F) + (bench_influence * 0.12F) +
                 plateau * (0.08F + plateau_noise * 0.06F));
    const float divide = saturate((plateau * 0.28F) + (rim_influence * 0.50F) +
                                  (bench_influence * 0.26F) + (1.0F - valley) * 0.10F);

    return {
        .canyon_floor = saturate(canyon_floor),
        .canyon_wall = saturate(canyon_wall),
        .wash_influence = wash_influence,
        .plateau_influence = plateau,
        .rim_influence = saturate(rim_influence),
        .bench_influence = saturate(bench_influence),
        .talus_influence = saturate(talus_influence),
        .ridge_influence = ridge,
        .valley_influence = valley,
        .basin_influence = basin,
        .divide_influence = divide,
        .channel_influence = channel,
        .channel_distance_m = std::max(channel_distance_norm * extent_m, 0.0F),
    };
}

[[nodiscard]] DesertDuneSampleFeatures desert_dune_features_at(Point2 p,
                                                               const TerrainLabGridDesc& desc,
                                                               const TerrainLabConfig& config) {
    const float angle = lerp(0.32F, 0.68F, random01(config.seed, 21U, 1201U));
    const Point2 wind{std::cos(angle), std::sin(angle)};
    const Point2 cross{-wind.z, wind.x};
    const float downwind = dot(p, wind);
    const float lateral = dot(p, cross);
    const float meander =
        std::sin((lateral * 3.7F) + random01(config.seed, 21U, 1207U) * 6.28318530718F) * 0.050F +
        fbm((p.x * 2.4F) + 3.0F, (p.z * 2.4F) - 5.0F, config.seed + 3101U, 4) * 0.055F;
    const float field_mask =
        smoothstep(0.04F, 0.30F,
                   1.0F - length({p.x * 0.72F + 0.08F, p.z * 0.88F - 0.06F}));
    const float patch_noise =
        fbm((p.x * 3.1F) - 2.0F, (p.z * 3.1F) + 4.0F, config.seed + 3103U, 4) * 0.5F +
        0.5F;

    float dune_profile = 0.0F;
    float crest = 0.0F;
    float slip_face = 0.0F;
    auto add_dune = [&](float center_down, float center_lat, float length_down, float width_lat,
                        float curvature, float amplitude) {
        const float lat = lateral - center_lat;
        const float crest_down = center_down + curvature * lat * lat + meander * 0.45F;
        const float local_down = downwind - crest_down;
        const float lateral_envelope =
            1.0F - smoothstep(width_lat * 0.70F, width_lat, std::abs(lat));
        const float downwind_envelope =
            smoothstep(-length_down * 0.70F, -length_down * 0.48F, local_down) *
            (1.0F - smoothstep(length_down * 0.30F, length_down * 0.72F, local_down));
        const float envelope = amplitude * field_mask * lateral_envelope * downwind_envelope *
                               lerp(0.78F, 1.12F, patch_noise);
        const float stoss = smoothstep(-length_down * 0.58F, 0.02F, local_down);
        const float lee = 1.0F - smoothstep(0.02F, length_down * 0.26F, local_down);
        const float body = (local_down < 0.02F ? stoss : lee) * envelope;
        const float crest_band =
            (1.0F - smoothstep(0.0F, length_down * 0.060F, std::abs(local_down))) * envelope;
        const float slip_band =
            smoothstep(0.0F, length_down * 0.075F, local_down) *
            (1.0F - smoothstep(length_down * 0.075F, length_down * 0.27F, local_down)) *
            envelope;
        dune_profile = std::max(dune_profile, body);
        crest = std::max(crest, crest_band);
        slip_face = std::max(slip_face, slip_band);
    };
    const float jitter = random01(config.seed, 21U, 1211U) - 0.5F;
    add_dune(-0.72F + jitter * 0.10F, -0.34F, 0.62F, 0.56F, 0.62F, 0.94F);
    add_dune(-0.32F - jitter * 0.06F, 0.28F, 0.72F, 0.62F, 0.44F, 1.00F);
    add_dune(0.14F + jitter * 0.08F, -0.12F, 0.70F, 0.58F, 0.58F, 0.90F);
    add_dune(0.54F - jitter * 0.08F, 0.36F, 0.58F, 0.48F, 0.72F, 0.76F);

    const float sheet_phase = fract((downwind * 2.85F) + (lateral * 0.18F) + meander + 0.37F);
    const float sheet_rise = smoothstep(0.06F, 0.66F, sheet_phase) *
                             (1.0F - smoothstep(0.70F, 0.98F, sheet_phase));
    const float sheet_patch = smoothstep(0.48F, 0.86F, patch_noise) * field_mask *
                              (1.0F - smoothstep(0.42F, 0.90F, dune_profile));
    dune_profile = saturate(dune_profile + sheet_rise * sheet_patch * 0.22F);
    crest = saturate(crest + sheet_rise * sheet_patch * 0.035F);
    slip_face = saturate(slip_face);
    const float interdune =
        saturate((1.0F - smoothstep(0.10F, 0.42F, dune_profile)) * lerp(0.72F, 1.0F, field_mask));
    const float wind_shadow =
        saturate(slip_face * 0.72F + crest * 0.20F) *
        smoothstep(0.22F, 0.80F,
                   fbm((p.x * 5.0F) - 7.0F, (p.z * 5.0F) + 9.0F, config.seed + 3107U, 3) *
                           0.5F +
                       0.5F);
    const float broad_low =
        1.0F - smoothstep(0.42F, 1.22F, length({p.x * 0.78F + 0.10F, p.z * 0.94F - 0.12F}));
    const float channel = saturate(interdune * 0.20F + wind_shadow * 0.06F);
    const float channel_distance_m =
        (1.0F - interdune) * std::max(half_extent_x_m(desc), half_extent_z_m(desc));
    return {
        .dune_crest = saturate(crest),
        .slip_face = saturate(slip_face),
        .interdune_flat = saturate(interdune),
        .wind_shadow = saturate(wind_shadow),
        .ridge_influence = saturate(crest * 0.62F + slip_face * 0.46F + dune_profile * 0.18F),
        .valley_influence = saturate(interdune * 0.72F + broad_low * 0.18F),
        .basin_influence = saturate(broad_low * 0.70F + interdune * 0.16F),
        .divide_influence = saturate(crest * 0.42F + dune_profile * 0.12F),
        .channel_influence = channel,
        .channel_distance_m = std::max(channel_distance_m, 0.0F),
    };
}

[[nodiscard]] AlpineGlacialSampleFeatures
alpine_glacial_features_at(Point2 p, const TerrainLabGridDesc& desc,
                           const TerrainLabConfig& config) {
    const float phase = random01(config.seed, 31U, 1401U) * 6.28318530718F;
    const float center =
        std::sin((p.z * 2.15F) + phase) * 0.10F +
        fbm((p.x * 1.9F) - 6.0F, (p.z * 1.9F) + 8.0F, config.seed + 3301U, 4) * 0.055F;
    const float distance = std::abs(p.x - center);
    const float downstream = smoothstep(-0.96F, 0.88F, p.z);
    const float width = lerp(0.18F, 0.32F, downstream);
    const float floor = 1.0F - smoothstep(width * 0.72F, width * 1.34F, distance);
    const float wall =
        smoothstep(width * 0.86F, width * 1.28F, distance) *
        (1.0F - smoothstep(width * 1.38F, width * 2.30F, distance));
    auto peak_at = [&](float side_offset, float z_center, float radius_x, float radius_z,
                       float skew, float weight) {
        const float peak_x = center + side_offset + skew * (p.z - z_center);
        const float d = length({(p.x - peak_x) / radius_x, (p.z - z_center) / radius_z});
        const float core = std::pow(saturate(1.0F - d), 1.35F);
        const float shoulder = 1.0F - smoothstep(0.78F, 1.34F, d);
        return saturate(core * 0.86F + shoulder * 0.18F) * weight;
    };
    const float peak_noise =
        fbm((p.x * 5.4F) + 11.0F, (p.z * 5.4F) - 13.0F, config.seed + 3303U, 4) *
            0.5F +
        0.5F;
    const float left_peak = peak_at(-0.66F, -0.44F, 0.58F, 0.54F, -0.14F, 1.00F);
    const float right_peak = peak_at(0.62F, -0.10F, 0.50F, 0.58F, 0.16F, 0.94F);
    const float rear_peak = peak_at(0.34F, 0.48F, 0.56F, 0.48F, 0.10F, 0.78F);
    const float peak =
        saturate(std::max(left_peak, std::max(right_peak, rear_peak)) *
                 (1.0F - floor * 0.78F) * lerp(0.86F, 1.14F, peak_noise));
    const float ridge =
        saturate((smoothstep(width * 1.52F, width * 2.70F, distance) *
                  (0.62F +
                   smoothstep(0.24F, 0.90F,
                              fbm((p.x * 4.6F) + 3.0F, (p.z * 4.6F) - 7.0F,
                                  config.seed + 3307U, 4) *
                                      0.5F +
                                  0.5F) *
                       0.24F)) +
                 peak * 0.86F);
    const float cirque =
        (1.0F - smoothstep(0.36F, 0.88F, length({p.x - center, p.z + 0.76F}))) *
        smoothstep(-0.98F, -0.54F, p.z);

    const float left_line = center - 0.58F - (p.z + 0.10F) * 0.36F;
    const float right_line = center + 0.54F + (p.z - 0.04F) * 0.32F;
    const float left_hanging =
        (1.0F - smoothstep(0.035F, 0.17F, std::abs(p.x - left_line))) *
        smoothstep(-0.72F, -0.22F, p.z) * (1.0F - smoothstep(0.02F, 0.42F, p.z));
    const float right_hanging =
        (1.0F - smoothstep(0.035F, 0.16F, std::abs(p.x - right_line))) *
        smoothstep(-0.42F, 0.18F, p.z) * (1.0F - smoothstep(0.46F, 0.82F, p.z));
    const float hanging = saturate(left_hanging + right_hanging * 0.86F);
    const float moraine =
        floor * smoothstep(-0.12F, 0.36F, p.z) *
            (1.0F - smoothstep(0.58F, 0.92F, p.z)) +
        wall * smoothstep(0.42F, 0.86F, downstream) * 0.30F;
    const float basin = saturate(floor * 0.58F + smoothstep(0.24F, 0.90F, downstream) * 0.18F);
    const float channel = saturate(floor * 0.58F + hanging * 0.22F);
    return {
        .glacier_floor = saturate(floor),
        .valley_wall = saturate(wall),
        .hanging_valley = hanging,
        .moraine_influence = saturate(moraine),
        .cirque_influence = saturate(cirque),
        .peak_influence = peak,
        .ridge_influence = saturate(ridge + wall * 0.24F + cirque * 0.18F),
        .valley_influence = saturate(floor * 0.78F + hanging * 0.30F),
        .basin_influence = basin,
        .divide_influence =
            saturate(ridge * 0.54F + peak * 0.24F + wall * 0.14F + (1.0F - floor) * 0.06F),
        .channel_influence = channel,
        .channel_distance_m =
            std::max(distance * std::max(half_extent_x_m(desc), half_extent_z_m(desc)), 0.0F),
    };
}

void compute_slope_and_curvature(const TerrainLabGridDesc& desc, const std::vector<float>& height,
                                 std::vector<float>& slope, std::vector<float>& curvature,
                                 float& max_slope, float& max_abs_curvature) {
    max_slope = 0.0F;
    max_abs_curvature = 0.0F;
    slope.assign(height.size(), 0.0F);
    curvature.assign(height.size(), 0.0F);

    for (std::uint32_t y = 0; y < desc.height; ++y) {
        for (std::uint32_t x = 0; x < desc.width; ++x) {
            const std::uint32_t x0 = x == 0U ? x : x - 1U;
            const std::uint32_t x1 = x + 1U >= desc.width ? x : x + 1U;
            const std::uint32_t y0 = y == 0U ? y : y - 1U;
            const std::uint32_t y1 = y + 1U >= desc.height ? y : y + 1U;
            const float span_x = static_cast<float>(x1 - x0) * desc.cell_size_m;
            const float span_z = static_cast<float>(y1 - y0) * desc.cell_size_m;
            const float dhdx = span_x == 0.0F ? 0.0F
                                              : (height[grid_index(x1, y, desc.width)] -
                                                 height[grid_index(x0, y, desc.width)]) /
                                                    span_x;
            const float dhdz = span_z == 0.0F ? 0.0F
                                              : (height[grid_index(x, y1, desc.width)] -
                                                 height[grid_index(x, y0, desc.width)]) /
                                                    span_z;
            const std::size_t sample = grid_index(x, y, desc.width);
            slope[sample] = std::sqrt((dhdx * dhdx) + (dhdz * dhdz));

            const float center = height[sample];
            float neighbor_sum = 0.0F;
            float neighbor_count = 0.0F;
            if (x > 0U) {
                neighbor_sum += height[grid_index(x - 1U, y, desc.width)];
                neighbor_count += 1.0F;
            }
            if (x + 1U < desc.width) {
                neighbor_sum += height[grid_index(x + 1U, y, desc.width)];
                neighbor_count += 1.0F;
            }
            if (y > 0U) {
                neighbor_sum += height[grid_index(x, y - 1U, desc.width)];
                neighbor_count += 1.0F;
            }
            if (y + 1U < desc.height) {
                neighbor_sum += height[grid_index(x, y + 1U, desc.width)];
                neighbor_count += 1.0F;
            }
            curvature[sample] =
                neighbor_count == 0.0F ? 0.0F : ((neighbor_sum / neighbor_count) - center);
            max_slope = std::max(max_slope, slope[sample]);
            max_abs_curvature = std::max(max_abs_curvature, std::abs(curvature[sample]));
        }
    }
}

void compute_flow_fields(const TerrainLabGridDesc& desc, const std::vector<float>& height,
                         const std::vector<float>& slope, std::vector<std::uint8_t>& flow_direction,
                         std::vector<float>& flow_accumulation, std::vector<float>& stream_power,
                         float& max_flow_accumulation, float& max_stream_power) {
    const std::size_t count = terrain_lab_sample_count(desc);
    flow_direction.assign(count, kFlowSinkDirection);
    flow_accumulation.assign(count, 1.0F);
    stream_power.assign(count, 0.0F);

    for (std::uint32_t y = 0; y < desc.height; ++y) {
        for (std::uint32_t x = 0; x < desc.width; ++x) {
            const std::size_t sample = grid_index(x, y, desc.width);
            float best_drop = 0.0F;
            std::uint8_t best_direction = kFlowSinkDirection;
            for (std::uint8_t direction = 0U; direction < kFlowSinkDirection; ++direction) {
                const auto nx = static_cast<std::int32_t>(x) + kFlowDx[direction];
                const auto ny = static_cast<std::int32_t>(y) + kFlowDy[direction];
                if (nx < 0 || ny < 0 || nx >= static_cast<std::int32_t>(desc.width) ||
                    ny >= static_cast<std::int32_t>(desc.height)) {
                    continue;
                }
                const float distance =
                    (kFlowDx[direction] != 0 && kFlowDy[direction] != 0) ? 1.41421356F : 1.0F;
                const float drop =
                    (height[sample] -
                     height[grid_index(static_cast<std::uint32_t>(nx),
                                       static_cast<std::uint32_t>(ny), desc.width)]) /
                    distance;
                if (drop > best_drop) {
                    best_drop = drop;
                    best_direction = direction;
                }
            }
            flow_direction[sample] = best_direction;
        }
    }

    std::vector<std::size_t> order(count);
    std::iota(order.begin(), order.end(), 0U);
    std::sort(order.begin(), order.end(),
              [&height](std::size_t lhs, std::size_t rhs) { return height[lhs] > height[rhs]; });

    for (const std::size_t sample : order) {
        const std::uint8_t direction = flow_direction[sample];
        if (direction >= kFlowSinkDirection) {
            continue;
        }
        const std::uint32_t x = static_cast<std::uint32_t>(sample % desc.width);
        const std::uint32_t y = static_cast<std::uint32_t>(sample / desc.width);
        const auto nx = static_cast<std::int32_t>(x) + kFlowDx[direction];
        const auto ny = static_cast<std::int32_t>(y) + kFlowDy[direction];
        if (nx < 0 || ny < 0 || nx >= static_cast<std::int32_t>(desc.width) ||
            ny >= static_cast<std::int32_t>(desc.height)) {
            continue;
        }
        flow_accumulation[grid_index(static_cast<std::uint32_t>(nx), static_cast<std::uint32_t>(ny),
                                     desc.width)] += flow_accumulation[sample];
    }

    max_flow_accumulation = 0.0F;
    max_stream_power = 0.0F;
    const float inv_log_count =
        1.0F / std::log1p(static_cast<float>(std::max<std::size_t>(count, 1U)));
    for (std::size_t sample = 0; sample < count; ++sample) {
        max_flow_accumulation = std::max(max_flow_accumulation, flow_accumulation[sample]);
        const float flow_t = std::log1p(flow_accumulation[sample]) * inv_log_count;
        stream_power[sample] = flow_t * slope[sample];
        max_stream_power = std::max(max_stream_power, stream_power[sample]);
    }
}

void derive_flow_aligned_channels(const TerrainLabConfig& config, TerrainLabFieldData& fields,
                                  const std::vector<float>& flow_accumulation,
                                  const std::vector<float>& slope) {
    const std::size_t count = terrain_lab_sample_count(fields.desc);
    const float inv_log_count =
        1.0F / std::log1p(static_cast<float>(std::max<std::size_t>(count, 1U)));
    for (std::uint32_t y = 0; y < fields.desc.height; ++y) {
        for (std::uint32_t x = 0; x < fields.desc.width; ++x) {
            const std::size_t sample = fields.index(x, y);
            const Point2 p = normalized_sample(fields.desc, x, y);
            const float guide = fields.channel_influence[sample];
            const float flow_t = std::log1p(flow_accumulation[sample]) * inv_log_count;
            const float drainage = smoothstep(0.50F, 0.84F, flow_t);
            const float slope_t = smoothstep(0.025F, 0.32F, slope[sample]);
            const float guide_bias = smoothstep(0.06F, 0.50F, guide);
            const float downstream = smoothstep(-0.92F, 0.78F, p.z);
            const float meander_noise =
                fbm((p.x * 7.0F) + 2.0F, (p.z * 7.0F) - 4.0F, config.seed + 1913U, 4) * 0.5F + 0.5F;
            const float drainage_channel = drainage *
                                           (0.22F + guide_bias * 0.50F + downstream * 0.10F) *
                                           (0.70F + slope_t * 0.22F);
            const float guided_channel =
                guide_bias * (0.30F + drainage * 0.45F + meander_noise * 0.08F);
            const float divide_suppression = 1.0F - fields.divide_influence[sample] * 0.54F;
            const float channel =
                saturate((drainage_channel + guided_channel) * divide_suppression * 1.60F);
            fields.channel_influence[sample] = channel;
            fields.valley_influence[sample] =
                saturate(std::max(fields.valley_influence[sample] * 0.82F, channel * 0.70F));
        }
    }

    std::vector<float> smoothed = fields.channel_influence;
    for (std::uint32_t iteration = 0; iteration < 1U; ++iteration) {
        std::vector<float> next = smoothed;
        for (std::uint32_t y = 0; y < fields.desc.height; ++y) {
            for (std::uint32_t x = 0; x < fields.desc.width; ++x) {
                const std::size_t sample = fields.index(x, y);
                float neighbor_sum = smoothed[sample];
                float neighbor_count = 1.0F;
                if (x > 0U) {
                    neighbor_sum += smoothed[fields.index(x - 1U, y)];
                    neighbor_count += 1.0F;
                }
                if (x + 1U < fields.desc.width) {
                    neighbor_sum += smoothed[fields.index(x + 1U, y)];
                    neighbor_count += 1.0F;
                }
                if (y > 0U) {
                    neighbor_sum += smoothed[fields.index(x, y - 1U)];
                    neighbor_count += 1.0F;
                }
                if (y + 1U < fields.desc.height) {
                    neighbor_sum += smoothed[fields.index(x, y + 1U)];
                    neighbor_count += 1.0F;
                }
                next[sample] =
                    saturate(lerp(smoothed[sample], neighbor_sum / neighbor_count, 0.22F));
            }
        }
        smoothed.swap(next);
    }
    fields.channel_influence = std::move(smoothed);
}

void relax_steep_process_slopes(const TerrainLabConfig& config, TerrainLabFieldData& fields) {
    if (config.process_strength <= 0.0F || fields.desc.width < 3U || fields.desc.height < 3U) {
        return;
    }

    std::vector<float> relaxed = fields.height_m;
    std::vector<float> next = relaxed;
    std::vector<float> slope;
    std::vector<float> curvature;
    float max_slope = 0.0F;
    float max_abs_curvature = 0.0F;

    for (std::uint32_t iteration = 0; iteration < 3U; ++iteration) {
        compute_slope_and_curvature(fields.desc, relaxed, slope, curvature, max_slope,
                                    max_abs_curvature);
        next = relaxed;
        for (std::uint32_t y = 1U; y + 1U < fields.desc.height; ++y) {
            for (std::uint32_t x = 1U; x + 1U < fields.desc.width; ++x) {
                const std::size_t sample = fields.index(x, y);
                const float neighbor_average =
                    (relaxed[fields.index(x - 1U, y)] + relaxed[fields.index(x + 1U, y)] +
                     relaxed[fields.index(x, y - 1U)] + relaxed[fields.index(x, y + 1U)]) *
                    0.25F;
                const float slope_relax = smoothstep(0.12F, 0.58F, slope[sample]);
                const float channel_bank =
                    smoothstep(0.12F, 0.52F, fields.channel_influence[sample]) *
                    (1.0F - smoothstep(0.66F, 0.96F, fields.channel_influence[sample]));
                const float structure_hold = saturate((fields.ridge_influence[sample] * 0.18F) +
                                                      (fields.divide_influence[sample] * 0.16F));
                const float relax_weight =
                    saturate(((slope_relax * 0.22F) + (channel_bank * 0.10F)) *
                             config.process_strength * (1.0F - structure_hold));
                next[sample] = lerp(relaxed[sample], neighbor_average, relax_weight);
            }
        }
        relaxed.swap(next);
    }

    for (std::size_t sample = 0; sample < fields.sample_count(); ++sample) {
        const float relaxation_delta = relaxed[sample] - fields.height_m[sample];
        fields.process_delta_m[sample] += relaxation_delta;
        fields.height_m[sample] = relaxed[sample];
    }
}

void update_height_range(TerrainLabFieldData& fields) {
    if (fields.height_m.empty()) {
        fields.min_height_m = 0.0F;
        fields.max_height_m = 0.0F;
        return;
    }
    fields.min_height_m = fields.height_m.front();
    fields.max_height_m = fields.height_m.front();
    for (const float height : fields.height_m) {
        fields.min_height_m = std::min(fields.min_height_m, height);
        fields.max_height_m = std::max(fields.max_height_m, height);
    }
}

void validate_finite(float value, const char* message) {
    if (!std::isfinite(value)) {
        throw std::runtime_error(message);
    }
}

void validate_normalized(float value, const char* message) {
    validate_finite(value, message);
    if (value < 0.0F || value > 1.0F) {
        throw std::runtime_error(message);
    }
}

void validate_material_mask(const TerrainLabMaterialMask& mask) {
    validate_normalized(mask.rock, "terrain lab material masks must be normalized");
    validate_normalized(mask.soil, "terrain lab material masks must be normalized");
    validate_normalized(mask.scree, "terrain lab material masks must be normalized");
    validate_normalized(mask.meadow, "terrain lab material masks must be normalized");
    validate_normalized(mask.forest, "terrain lab material masks must be normalized");
    validate_normalized(mask.snow, "terrain lab material masks must be normalized");
    validate_normalized(mask.sand, "terrain lab material masks must be normalized");
    const float sum =
        mask.rock + mask.soil + mask.scree + mask.meadow + mask.forest + mask.snow + mask.sand;
    if (std::abs(sum - 1.0F) > kMaterialMaskTolerance) {
        throw std::runtime_error("terrain lab material masks must sum to one");
    }
}

} // namespace

std::size_t TerrainLabFieldData::sample_count() const {
    return terrain_lab_sample_count(desc);
}

std::size_t TerrainLabFieldData::index(std::uint32_t x, std::uint32_t y) const {
    if (x >= desc.width || y >= desc.height) {
        throw std::runtime_error("terrain lab field sample index is out of bounds");
    }
    return grid_index(x, y, desc.width);
}

std::size_t terrain_lab_sample_count(const TerrainLabGridDesc& desc) {
    return static_cast<std::size_t>(desc.width) * static_cast<std::size_t>(desc.height);
}

float terrain_lab_grid_sample_x_m(const TerrainLabGridDesc& desc, std::uint32_t x) {
    if (x >= desc.width) {
        throw std::runtime_error("terrain lab X sample index is out of bounds");
    }
    return desc.origin_x_m +
           (static_cast<float>(x) - (static_cast<float>(desc.width - 1U) * 0.5F)) *
               desc.cell_size_m;
}

float terrain_lab_grid_sample_z_m(const TerrainLabGridDesc& desc, std::uint32_t y) {
    if (y >= desc.height) {
        throw std::runtime_error("terrain lab Z sample index is out of bounds");
    }
    return desc.origin_z_m +
           (static_cast<float>(y) - (static_cast<float>(desc.height - 1U) * 0.5F)) *
               desc.cell_size_m;
}

void validate_terrain_lab_fields(const TerrainLabFieldData& fields) {
    if (fields.desc.width == 0U || fields.desc.height == 0U) {
        throw std::runtime_error("terrain lab fields require nonzero dimensions");
    }
    validate_finite(fields.desc.cell_size_m, "terrain lab fields require finite grid metadata");
    validate_finite(fields.desc.origin_x_m, "terrain lab fields require finite grid metadata");
    validate_finite(fields.desc.origin_z_m, "terrain lab fields require finite grid metadata");
    if (fields.desc.cell_size_m <= 0.0F) {
        throw std::runtime_error("terrain lab fields require positive cell size");
    }

    const std::size_t count = terrain_lab_sample_count(fields.desc);
    const auto require_size = [count](std::size_t size, const char* message) {
        if (size != count) {
            throw std::runtime_error(message);
        }
    };
    require_size(fields.height_m.size(), "terrain lab height field size mismatch");
    require_size(fields.structure_height_m.size(), "terrain lab structure field size mismatch");
    require_size(fields.process_delta_m.size(), "terrain lab process field size mismatch");
    require_size(fields.detail_height_m.size(), "terrain lab detail field size mismatch");
    require_size(fields.slope.size(), "terrain lab slope field size mismatch");
    require_size(fields.curvature.size(), "terrain lab curvature field size mismatch");
    require_size(fields.flow_direction.size(), "terrain lab flow direction field size mismatch");
    require_size(fields.flow_accumulation.size(),
                 "terrain lab flow accumulation field size mismatch");
    require_size(fields.stream_power.size(), "terrain lab stream power field size mismatch");
    require_size(fields.wetness.size(), "terrain lab wetness field size mismatch");
    require_size(fields.deposition.size(), "terrain lab deposition field size mismatch");
    require_size(fields.material_masks.size(), "terrain lab material field size mismatch");
    require_size(fields.grass_density.size(), "terrain lab grass field size mismatch");
    require_size(fields.shrub_density.size(), "terrain lab shrub field size mismatch");
    require_size(fields.tree_density.size(), "terrain lab tree field size mismatch");
    require_size(fields.canopy_height_m.size(), "terrain lab canopy field size mismatch");
    require_size(fields.ridge_influence.size(), "terrain lab ridge field size mismatch");
    require_size(fields.valley_influence.size(), "terrain lab valley field size mismatch");
    require_size(fields.basin_influence.size(), "terrain lab basin field size mismatch");
    require_size(fields.watershed_id.size(), "terrain lab watershed field size mismatch");
    require_size(fields.divide_influence.size(), "terrain lab divide field size mismatch");
    require_size(fields.channel_influence.size(), "terrain lab channel field size mismatch");
    require_size(fields.channel_distance_m.size(),
                 "terrain lab channel distance field size mismatch");
    if (fields.watershed_count == 0U) {
        throw std::runtime_error("terrain lab fields require at least one watershed");
    }
    validate_finite(fields.max_channel_distance_m,
                    "terrain lab max channel distance must be finite");
    if (fields.max_channel_distance_m < 0.0F) {
        throw std::runtime_error("terrain lab max channel distance must be nonnegative");
    }

    for (std::size_t sample = 0; sample < count; ++sample) {
        validate_finite(fields.height_m[sample], "terrain lab height must be finite");
        validate_finite(fields.structure_height_m[sample],
                        "terrain lab structure height must be finite");
        validate_finite(fields.process_delta_m[sample], "terrain lab process delta must be finite");
        validate_finite(fields.detail_height_m[sample], "terrain lab detail height must be finite");
        validate_finite(fields.slope[sample], "terrain lab slope must be finite");
        validate_finite(fields.curvature[sample], "terrain lab curvature must be finite");
        validate_finite(fields.flow_accumulation[sample],
                        "terrain lab flow accumulation must be finite");
        validate_finite(fields.stream_power[sample], "terrain lab stream power must be finite");
        validate_normalized(fields.wetness[sample], "terrain lab wetness must be normalized");
        validate_normalized(fields.deposition[sample], "terrain lab deposition must be normalized");
        validate_normalized(fields.grass_density[sample],
                            "terrain lab grass density must be normalized");
        validate_normalized(fields.shrub_density[sample],
                            "terrain lab shrub density must be normalized");
        validate_normalized(fields.tree_density[sample],
                            "terrain lab tree density must be normalized");
        validate_finite(fields.canopy_height_m[sample], "terrain lab canopy must be finite");
        validate_normalized(fields.ridge_influence[sample],
                            "terrain lab ridge influence must be normalized");
        validate_normalized(fields.valley_influence[sample],
                            "terrain lab valley influence must be normalized");
        validate_normalized(fields.basin_influence[sample],
                            "terrain lab basin influence must be normalized");
        validate_normalized(fields.divide_influence[sample],
                            "terrain lab divide influence must be normalized");
        validate_normalized(fields.channel_influence[sample],
                            "terrain lab channel influence must be normalized");
        validate_finite(fields.channel_distance_m[sample],
                        "terrain lab channel distance must be finite");
        if (fields.channel_distance_m[sample] < 0.0F) {
            throw std::runtime_error("terrain lab channel distance must be nonnegative");
        }
        if (fields.watershed_id[sample] >= fields.watershed_count) {
            throw std::runtime_error("terrain lab watershed id must be valid");
        }
        if (fields.flow_accumulation[sample] < 0.0F) {
            throw std::runtime_error("terrain lab flow accumulation must be nonnegative");
        }
        if (fields.stream_power[sample] < 0.0F) {
            throw std::runtime_error("terrain lab stream power must be nonnegative");
        }
        if (fields.flow_direction[sample] > kFlowSinkDirection) {
            throw std::runtime_error("terrain lab flow direction must be valid");
        }
        if (fields.canopy_height_m[sample] < 0.0F) {
            throw std::runtime_error("terrain lab canopy must be nonnegative");
        }
        validate_material_mask(fields.material_masks[sample]);
    }
}

TerrainLabFieldSummary summarize_terrain_lab_fields(const TerrainLabFieldData& fields) {
    validate_terrain_lab_fields(fields);
    TerrainLabFieldSummary summary{
        .sample_count = fields.sample_count(),
        .watershed_count = fields.watershed_count,
        .min_height_m = fields.min_height_m,
        .max_height_m = fields.max_height_m,
        .height_span_m = fields.max_height_m - fields.min_height_m,
        .max_flow_accumulation = fields.max_flow_accumulation,
        .max_channel_distance_m = fields.max_channel_distance_m,
    };
    if (summary.sample_count == 0U) {
        return summary;
    }
    double height_sum = 0.0;
    double slope_sum = 0.0;
    double wetness_sum = 0.0;
    double tree_sum = 0.0;
    double material_entropy_sum = 0.0;
    double divide_sum = 0.0;
    double channel_sum = 0.0;
    double channel_height_sum = 0.0;
    double channel_flow_sum = 0.0;
    double channel_stream_power_sum = 0.0;
    double non_channel_flow_sum = 0.0;
    double non_channel_stream_power_sum = 0.0;
    double divide_height_sum = 0.0;
    double edge_step_sum = 0.0;
    std::size_t edge_step_count = 0;
    for (std::size_t sample = 0; sample < summary.sample_count; ++sample) {
        height_sum += fields.height_m[sample];
        slope_sum += fields.slope[sample];
        wetness_sum += fields.wetness[sample];
        tree_sum += fields.tree_density[sample];
        material_entropy_sum += material_entropy(fields.material_masks[sample]);
        divide_sum += fields.divide_influence[sample];
        channel_sum += fields.channel_influence[sample];
        if (fields.channel_influence[sample] > 0.45F) {
            channel_height_sum += fields.height_m[sample];
            channel_flow_sum += fields.flow_accumulation[sample];
            channel_stream_power_sum += fields.stream_power[sample];
            ++summary.channel_sample_count;
        }
        if (fields.channel_influence[sample] < 0.05F && fields.divide_influence[sample] < 0.25F) {
            non_channel_flow_sum += fields.flow_accumulation[sample];
            non_channel_stream_power_sum += fields.stream_power[sample];
            ++summary.non_channel_sample_count;
        }
        if (fields.divide_influence[sample] > 0.55F && fields.channel_influence[sample] < 0.20F) {
            divide_height_sum += fields.height_m[sample];
            ++summary.divide_sample_count;
        }
        if (fields.flow_direction[sample] == kFlowSinkDirection) {
            ++summary.sink_sample_count;
        }
    }
    for (std::uint32_t y = 0; y < fields.desc.height; ++y) {
        for (std::uint32_t x = 0; x < fields.desc.width; ++x) {
            const bool left = x == 0U;
            const bool right = x + 1U == fields.desc.width;
            const bool top = y == 0U;
            const bool bottom = y + 1U == fields.desc.height;
            if (!left && !right && !top && !bottom) {
                continue;
            }
            const std::size_t sample = fields.index(x, y);
            if (left && fields.desc.width > 1U) {
                edge_step_sum +=
                    std::abs(fields.height_m[sample] - fields.height_m[fields.index(1U, y)]);
                ++edge_step_count;
            }
            if (right && fields.desc.width > 1U) {
                edge_step_sum += std::abs(fields.height_m[sample] -
                                          fields.height_m[fields.index(fields.desc.width - 2U, y)]);
                ++edge_step_count;
            }
            if (top && fields.desc.height > 1U) {
                edge_step_sum +=
                    std::abs(fields.height_m[sample] - fields.height_m[fields.index(x, 1U)]);
                ++edge_step_count;
            }
            if (bottom && fields.desc.height > 1U) {
                edge_step_sum +=
                    std::abs(fields.height_m[sample] -
                             fields.height_m[fields.index(x, fields.desc.height - 2U)]);
                ++edge_step_count;
            }
        }
    }
    const double inv_count = 1.0 / static_cast<double>(summary.sample_count);
    summary.mean_height_m = static_cast<float>(height_sum * inv_count);
    summary.mean_slope = static_cast<float>(slope_sum * inv_count);
    summary.mean_wetness = static_cast<float>(wetness_sum * inv_count);
    summary.mean_tree_density = static_cast<float>(tree_sum * inv_count);
    summary.mean_material_entropy = static_cast<float>(material_entropy_sum * inv_count);
    summary.mean_divide_influence = static_cast<float>(divide_sum * inv_count);
    summary.mean_channel_influence = static_cast<float>(channel_sum * inv_count);
    if (summary.channel_sample_count > 0U) {
        const double inv_channel = 1.0 / static_cast<double>(summary.channel_sample_count);
        summary.mean_channel_height_m = static_cast<float>(channel_height_sum * inv_channel);
        summary.mean_channel_flow_accumulation = static_cast<float>(channel_flow_sum * inv_channel);
        summary.mean_channel_stream_power =
            static_cast<float>(channel_stream_power_sum * inv_channel);
    }
    if (summary.non_channel_sample_count > 0U) {
        const double inv_non_channel =
            1.0 / static_cast<double>(summary.non_channel_sample_count);
        summary.mean_non_channel_flow_accumulation =
            static_cast<float>(non_channel_flow_sum * inv_non_channel);
        summary.mean_non_channel_stream_power =
            static_cast<float>(non_channel_stream_power_sum * inv_non_channel);
    }
    if (summary.divide_sample_count > 0U) {
        summary.mean_divide_height_m = static_cast<float>(
            divide_height_sum / static_cast<double>(summary.divide_sample_count));
    }
    summary.divide_channel_height_gap_m =
        summary.mean_divide_height_m - summary.mean_channel_height_m;
    if (edge_step_count > 0U) {
        summary.mean_edge_step_m =
            static_cast<float>(edge_step_sum / static_cast<double>(edge_step_count));
    }
    summary.sink_sample_ratio =
        static_cast<float>(static_cast<double>(summary.sink_sample_count) * inv_count);
    return summary;
}

TerrainLabFieldData generate_temperate_mountain_watershed_fields(const TerrainLabConfig& config) {
    validate_terrain_lab_config(config);
    TerrainLabFieldData fields = make_empty_terrain_lab_fields(config);
    const std::size_t count = fields.sample_count();

    rasterize_watershed_features(config, fields);

    for (std::uint32_t y = 0; y < fields.desc.height; ++y) {
        for (std::uint32_t x = 0; x < fields.desc.width; ++x) {
            const std::size_t sample = fields.index(x, y);
            const Point2 p = normalized_sample(fields.desc, x, y);
            const float divide = fields.divide_influence[sample];
            const float channel = fields.channel_influence[sample];
            const float soft_divide = smoothstep(0.18F, 0.82F, divide);
            const float ridge =
                saturate((ridge_influence(p, config) * 0.56F) + (soft_divide * 0.14F));
            const float basin =
                saturate((basin_influence(p, config) * 0.58F) + (1.0F - soft_divide) * 0.10F);
            const float valley =
                saturate((valley_influence(p, config) * 0.36F) + (channel * 0.32F));
            const float headwater = saturate((1.0F - p.z) * 0.5F);
            const float broad = fbm(p.x * 1.3F - 3.0F, p.z * 1.3F + 5.0F, config.seed + 101U, 5);
            const float structure =
                ((headwater * 0.48F) + (ridge * 0.38F) + (soft_divide * 0.04F) + (broad * 0.16F) -
                 (basin * 0.16F) - (valley * 0.18F) - (channel * 0.04F) - 0.12F) *
                config.elevation_scale_m * config.structure_strength;

            fields.ridge_influence[sample] = ridge;
            fields.valley_influence[sample] = valley;
            fields.basin_influence[sample] = basin;
            fields.structure_height_m[sample] = structure;
            fields.height_m[sample] = structure;
        }
    }

    std::vector<float> temp_slope;
    std::vector<float> temp_curvature;
    std::vector<std::uint8_t> temp_direction;
    std::vector<float> temp_accumulation;
    std::vector<float> temp_stream_power;
    float temp_max_slope = 0.0F;
    float temp_max_curvature = 0.0F;
    float temp_max_accumulation = 0.0F;
    float temp_max_stream_power = 0.0F;
    compute_slope_and_curvature(fields.desc, fields.structure_height_m, temp_slope, temp_curvature,
                                temp_max_slope, temp_max_curvature);
    compute_flow_fields(fields.desc, fields.structure_height_m, temp_slope, temp_direction,
                        temp_accumulation, temp_stream_power, temp_max_accumulation,
                        temp_max_stream_power);
    derive_flow_aligned_channels(config, fields, temp_accumulation, temp_slope);

    const float inv_log_count =
        1.0F / std::log1p(static_cast<float>(std::max<std::size_t>(count, 1U)));
    for (std::size_t sample = 0; sample < count; ++sample) {
        const float flow_t = std::log1p(temp_accumulation[sample]) * inv_log_count;
        const float slope_t = smoothstep(0.045F, 0.38F, temp_slope[sample]);
        const float channel = fields.channel_influence[sample];
        const float divide = fields.divide_influence[sample];
        const float process_channel = saturate((flow_t * 0.44F) + (channel * 0.48F));
        const float valley =
            saturate((fields.valley_influence[sample] * 0.34F) + (channel * 0.58F));
        const float valley_cut = valley * smoothstep(0.08F, 0.76F, process_channel) *
                                 config.elevation_scale_m * 0.17F * config.process_strength;
        const float valley_fill = channel * process_channel * (1.0F - slope_t) *
                                  config.elevation_scale_m * 0.052F * config.process_strength;
        const float talus_relax = smoothstep(0.62F, 0.95F, slope_t) *
                                  fields.ridge_influence[sample] * (1.0F + divide * 0.26F) *
                                  config.elevation_scale_m * 0.052F * config.process_strength;
        fields.process_delta_m[sample] = -valley_cut + valley_fill - talus_relax;
        fields.height_m[sample] =
            fields.structure_height_m[sample] + fields.process_delta_m[sample];
    }
    relax_steep_process_slopes(config, fields);

    for (std::uint32_t y = 0; y < fields.desc.height; ++y) {
        for (std::uint32_t x = 0; x < fields.desc.width; ++x) {
            const std::size_t sample = fields.index(x, y);
            const Point2 p = normalized_sample(fields.desc, x, y);
            const float exposed = saturate((fields.ridge_influence[sample] * 0.7F) +
                                           (fields.divide_influence[sample] * 0.28F) +
                                           (1.0F - fields.valley_influence[sample]) * 0.3F);
            const float channel_floor = smoothstep(0.48F, 0.90F, fields.channel_influence[sample]);
            const float detail_gate = exposed * (1.0F - fields.valley_influence[sample] * 0.30F) *
                                      (1.0F - channel_floor * 0.78F);
            const float ridge_detail =
                fbm(p.x * 18.0F + 11.0F, p.z * 18.0F - 2.0F, config.seed + 401U, 4) *
                fields.ridge_influence[sample] * 0.016F;
            const float slope_detail =
                fbm(p.x * 32.0F - 17.0F, p.z * 32.0F + 9.0F, config.seed + 409U, 3) *
                smoothstep(0.16F, 0.64F, exposed) * 0.008F;
            const float broad_residual =
                fbm(p.x * 7.5F + 4.0F, p.z * 7.5F - 3.0F, config.seed + 419U, 3) * 0.010F;
            const float detail = (ridge_detail + slope_detail + broad_residual) *
                                 config.elevation_scale_m * config.detail_strength * detail_gate;
            fields.detail_height_m[sample] = detail;
            fields.height_m[sample] += detail;
        }
    }

    update_height_range(fields);
    compute_slope_and_curvature(fields.desc, fields.height_m, fields.slope, fields.curvature,
                                fields.max_slope, fields.max_abs_curvature);
    compute_flow_fields(fields.desc, fields.height_m, fields.slope, fields.flow_direction,
                        fields.flow_accumulation, fields.stream_power, fields.max_flow_accumulation,
                        fields.max_stream_power);

    const float height_span = std::max(fields.max_height_m - fields.min_height_m, 1.0F);
    fields.max_wetness = 0.0F;
    fields.max_deposition = 0.0F;
    for (std::uint32_t y = 0; y < fields.desc.height; ++y) {
        for (std::uint32_t x = 0; x < fields.desc.width; ++x) {
            const std::size_t sample = fields.index(x, y);
            const Point2 p = normalized_sample(fields.desc, x, y);
            const float elevation_t =
                saturate((fields.height_m[sample] - fields.min_height_m) / height_span);
            const float slope_t = smoothstep(0.05F, 0.46F, fields.slope[sample]);
            const float flow_t = std::log1p(fields.flow_accumulation[sample]) * inv_log_count;
            const float channel = fields.channel_influence[sample];
            const float divide = fields.divide_influence[sample];
            const float valley =
                saturate((fields.valley_influence[sample] * 0.32F) + (channel * 0.78F));
            const float wetness = saturate((flow_t * 0.50F) + (valley * 0.30F) + (channel * 0.20F) +
                                           ((1.0F - slope_t) * 0.14F) - (divide * 0.08F));
            const float deposition =
                saturate(wetness * (1.0F - slope_t) *
                         (0.14F + fields.basin_influence[sample] * 0.24F + channel * 0.72F));
            fields.wetness[sample] = wetness;
            fields.deposition[sample] = deposition;
            fields.max_wetness = std::max(fields.max_wetness, wetness);
            fields.max_deposition = std::max(fields.max_deposition, deposition);

            const float material_noise =
                fbm(p.x * 9.0F - 2.0F, p.z * 9.0F + 7.0F, config.seed + 601U, 4) * 0.5F + 0.5F;
            const float vegetation_patch =
                fbm(p.x * 5.8F + 13.0F, p.z * 5.8F - 5.0F, config.seed + 607U, 4) * 0.5F + 0.5F;
            const float scree_patch =
                fbm(p.x * 14.0F - 19.0F, p.z * 14.0F + 23.0F, config.seed + 613U, 3) * 0.5F + 0.5F;
            const float high = smoothstep(0.78F, 0.96F, elevation_t);
            const float snow = high * (1.0F - smoothstep(0.52F, 0.88F, slope_t)) *
                               lerp(0.74F, 1.12F, material_noise);
            const float rock = ((slope_t * lerp(0.66F, 0.88F, scree_patch)) +
                                fields.ridge_influence[sample] * 0.18F + divide * 0.12F) *
                               (1.0F - snow) * (1.0F - channel * 0.28F);
            const float scree = smoothstep(0.30F, 0.78F, slope_t) * (1.0F - wetness * 0.55F) *
                                (1.0F - channel * 0.26F) * (1.0F - snow) *
                                lerp(0.70F, 1.28F, scree_patch);
            const float meadow = wetness * (1.0F - slope_t) * (1.0F - high * 0.55F) *
                                 (0.58F + channel * 0.26F + material_noise * 0.34F);
            const float forest = smoothstep(0.26F, 0.72F, wetness) * (1.0F - slope_t) *
                                 (1.0F - high) * (1.0F - deposition * 0.35F) *
                                 (1.0F - channel * 0.28F) * lerp(0.48F, 1.34F, vegetation_patch);
            const float soil = (0.34F + deposition * 0.45F + (1.0F - slope_t) * 0.24F) *
                               (1.0F - snow * 0.75F) * lerp(0.82F, 1.16F, material_noise);
            const TerrainLabMaterialMask material =
                normalized_material_mask(rock, soil, scree, meadow, forest, snow, 0.0F);
            fields.material_masks[sample] = material;

            const float grass =
                saturate((material.meadow * 0.85F + material.soil * 0.30F) * (1.0F - snow));
            const float shrub = saturate((material.soil * 0.32F + material.scree * 0.18F) *
                                         (1.0F - slope_t) * (1.0F - high * 0.65F));
            const float tree = saturate(material.forest * 0.95F *
                                        (1.0F - fields.valley_influence[sample] * 0.18F));
            fields.grass_density[sample] = grass;
            fields.shrub_density[sample] = shrub;
            fields.tree_density[sample] = tree;
            fields.canopy_height_m[sample] =
                tree <= 0.0F ? 0.0F : tree * lerp(6.0F, 28.0F, wetness);
        }
    }

    validate_terrain_lab_fields(fields);
    return fields;
}

TerrainLabFieldData generate_arid_mesa_canyon_fields(const TerrainLabConfig& config) {
    validate_terrain_lab_config(config);
    TerrainLabFieldData fields = make_empty_terrain_lab_fields(config);
    const std::size_t count = fields.sample_count();
    const float arid_elevation_scale_m = config.elevation_scale_m * 0.62F;
    fields.watershed_count = 1U;
    fields.max_channel_distance_m = 0.0F;

    for (std::uint32_t y = 0; y < fields.desc.height; ++y) {
        for (std::uint32_t x = 0; x < fields.desc.width; ++x) {
            const std::size_t sample = fields.index(x, y);
            const Point2 p = normalized_sample(fields.desc, x, y);
            const AridMesaSampleFeatures features = arid_mesa_features_at(p, fields.desc, config);
            const float broad =
                fbm((p.x * 1.18F) - 6.0F, (p.z * 1.18F) + 2.0F, config.seed + 2701U, 5);
            const float bench_noise =
                fbm((p.x * 3.4F) + 11.0F, (p.z * 3.4F) - 5.0F, config.seed + 2707U, 4);
            const float high_desert_tilt = (1.0F - p.z) * 0.5F;
            const float mesa_bench =
                smoothstep(-0.18F, 0.78F, broad + features.plateau_influence * 0.62F);
            const float bench_step = (features.rim_influence * 0.090F) +
                                     (features.bench_influence * 0.055F) -
                                     (features.talus_influence * 0.030F);
            const float canyon_cut = (features.valley_influence * 0.16F) +
                                     (features.canyon_floor * 0.24F) +
                                     (features.wash_influence * 0.025F);
            const float structure =
                ((high_desert_tilt * 0.24F) + (features.plateau_influence * 0.30F) +
                 (features.ridge_influence * 0.08F) + (mesa_bench * 0.08F) + bench_step +
                 (broad * 0.11F) + (bench_noise * 0.035F) - canyon_cut - 0.08F) *
                arid_elevation_scale_m * config.structure_strength;

            fields.ridge_influence[sample] = features.ridge_influence;
            fields.valley_influence[sample] = features.valley_influence;
            fields.basin_influence[sample] = features.basin_influence;
            fields.divide_influence[sample] = features.divide_influence;
            fields.channel_influence[sample] = features.channel_influence;
            fields.channel_distance_m[sample] = features.channel_distance_m;
            fields.max_channel_distance_m =
                std::max(fields.max_channel_distance_m, features.channel_distance_m);
            fields.structure_height_m[sample] = structure;
            fields.height_m[sample] = structure;
        }
    }

    std::vector<float> temp_slope;
    std::vector<float> temp_curvature;
    std::vector<std::uint8_t> temp_direction;
    std::vector<float> temp_accumulation;
    std::vector<float> temp_stream_power;
    float temp_max_slope = 0.0F;
    float temp_max_curvature = 0.0F;
    float temp_max_accumulation = 0.0F;
    float temp_max_stream_power = 0.0F;
    compute_slope_and_curvature(fields.desc, fields.structure_height_m, temp_slope, temp_curvature,
                                temp_max_slope, temp_max_curvature);
    compute_flow_fields(fields.desc, fields.structure_height_m, temp_slope, temp_direction,
                        temp_accumulation, temp_stream_power, temp_max_accumulation,
                        temp_max_stream_power);
    derive_flow_aligned_channels(config, fields, temp_accumulation, temp_slope);

    const float inv_log_count =
        1.0F / std::log1p(static_cast<float>(std::max<std::size_t>(count, 1U)));
    for (std::size_t sample = 0; sample < count; ++sample) {
        const float flow_t = std::log1p(temp_accumulation[sample]) * inv_log_count;
        const float slope_t = smoothstep(0.045F, 0.46F, temp_slope[sample]);
        const float channel = fields.channel_influence[sample];
        const float wall = fields.ridge_influence[sample];
        const float valley = fields.valley_influence[sample];
        const float wash_cut = channel * smoothstep(0.06F, 0.76F, flow_t + channel * 0.22F) *
                               arid_elevation_scale_m * 0.050F * config.process_strength;
        const float sheet_erosion =
            valley * slope_t * arid_elevation_scale_m * 0.020F * config.process_strength;
        const float cliff_weathering = wall * smoothstep(0.55F, 0.96F, slope_t) *
                                       arid_elevation_scale_m * 0.016F * config.process_strength;
        const float talus_deposit = wall * smoothstep(0.14F, 0.52F, slope_t) *
                                    (1.0F - smoothstep(0.54F, 0.94F, slope_t)) *
                                    arid_elevation_scale_m * 0.018F * config.process_strength;
        const float wash_fill = fields.basin_influence[sample] * (1.0F - slope_t) *
                                arid_elevation_scale_m * 0.016F * config.process_strength;
        fields.process_delta_m[sample] =
            -wash_cut - sheet_erosion - cliff_weathering + talus_deposit + wash_fill;
        fields.height_m[sample] =
            fields.structure_height_m[sample] + fields.process_delta_m[sample];
    }
    relax_steep_process_slopes(config, fields);
    update_height_range(fields);

    const float process_height_span = std::max(fields.max_height_m - fields.min_height_m, 1.0F);
    for (std::uint32_t y = 0; y < fields.desc.height; ++y) {
        for (std::uint32_t x = 0; x < fields.desc.width; ++x) {
            const std::size_t sample = fields.index(x, y);
            const Point2 p = normalized_sample(fields.desc, x, y);
            const AridMesaSampleFeatures features = arid_mesa_features_at(p, fields.desc, config);
            const float elevation_t =
                saturate((fields.height_m[sample] - fields.min_height_m) / process_height_span);
            const float exposed = saturate(
                (features.canyon_wall * 0.70F) + (features.rim_influence * 0.42F) +
                (fields.ridge_influence[sample] * 0.26F) + (features.plateau_influence * 0.10F));
            const float channel_floor = smoothstep(0.46F, 0.90F, fields.channel_influence[sample]);
            const float strata =
                std::sin((elevation_t * 54.0F) +
                         fbm((p.x * 9.0F) + 2.0F, (p.z * 9.0F) - 7.0F, config.seed + 2711U, 3) *
                             3.0F) *
                features.canyon_wall * 0.006F;
            const float rock_roughness =
                fbm((p.x * 24.0F) - 13.0F, (p.z * 24.0F) + 17.0F, config.seed + 2719U, 4) *
                exposed * 0.008F;
            const float desert_pavement =
                fbm((p.x * 15.0F) + 19.0F, (p.z * 15.0F) - 23.0F, config.seed + 2729U, 3) *
                features.plateau_influence * 0.006F;
            const float bench_breakup =
                fbm((p.x * 11.0F) - 29.0F, (p.z * 11.0F) + 31.0F, config.seed + 2731U, 3) *
                features.bench_influence * 0.004F;
            const float detail = (strata + rock_roughness + desert_pavement + bench_breakup) *
                                 arid_elevation_scale_m * config.detail_strength *
                                 (1.0F - channel_floor * 0.72F);
            fields.detail_height_m[sample] = detail;
            fields.height_m[sample] += detail;
        }
    }

    update_height_range(fields);
    compute_slope_and_curvature(fields.desc, fields.height_m, fields.slope, fields.curvature,
                                fields.max_slope, fields.max_abs_curvature);
    compute_flow_fields(fields.desc, fields.height_m, fields.slope, fields.flow_direction,
                        fields.flow_accumulation, fields.stream_power, fields.max_flow_accumulation,
                        fields.max_stream_power);

    const float height_span = std::max(fields.max_height_m - fields.min_height_m, 1.0F);
    fields.max_wetness = 0.0F;
    fields.max_deposition = 0.0F;
    for (std::uint32_t y = 0; y < fields.desc.height; ++y) {
        for (std::uint32_t x = 0; x < fields.desc.width; ++x) {
            const std::size_t sample = fields.index(x, y);
            const Point2 p = normalized_sample(fields.desc, x, y);
            const AridMesaSampleFeatures features = arid_mesa_features_at(p, fields.desc, config);
            const float elevation_t =
                saturate((fields.height_m[sample] - fields.min_height_m) / height_span);
            const float slope_t = smoothstep(0.05F, 0.52F, fields.slope[sample]);
            const float flow_t = std::log1p(fields.flow_accumulation[sample]) * inv_log_count;
            const float channel = fields.channel_influence[sample];
            const float dry_wash = saturate((channel * 0.76F) + (features.wash_influence * 0.24F));
            const float wetness = saturate(
                (flow_t * 0.16F) + (dry_wash * 0.11F) + (fields.basin_influence[sample] * 0.055F) +
                ((1.0F - slope_t) * 0.025F) - (fields.divide_influence[sample] * 0.05F));
            const float deposition =
                saturate((dry_wash * (1.0F - slope_t) * 0.38F) +
                         (fields.basin_influence[sample] * 0.17F) + (flow_t * 0.035F));
            fields.wetness[sample] = wetness;
            fields.deposition[sample] = deposition;
            fields.max_wetness = std::max(fields.max_wetness, wetness);
            fields.max_deposition = std::max(fields.max_deposition, deposition);

            const float material_noise =
                fbm((p.x * 9.0F) - 2.0F, (p.z * 9.0F) + 7.0F, config.seed + 2801U, 4) * 0.5F + 0.5F;
            const float scree_patch =
                fbm((p.x * 14.0F) - 19.0F, (p.z * 14.0F) + 23.0F, config.seed + 2803U, 3) * 0.5F +
                0.5F;
            const float vegetation_patch =
                fbm((p.x * 5.4F) + 13.0F, (p.z * 5.4F) - 5.0F, config.seed + 2809U, 4) * 0.5F +
                0.5F;
            const float high = smoothstep(0.80F, 0.98F, elevation_t);
            const float rock =
                ((slope_t * 0.82F) + (features.canyon_wall * 0.72F) +
                 (features.rim_influence * 0.38F) + (fields.ridge_influence[sample] * 0.22F)) *
                (1.0F - dry_wash * 0.62F) * lerp(0.84F, 1.20F, material_noise);
            const float scree = smoothstep(0.18F, 0.72F, slope_t) *
                                (1.0F - smoothstep(0.78F, 1.0F, slope_t)) *
                                (0.30F + features.talus_influence * 0.58F +
                                 features.canyon_wall * 0.36F + dry_wash * 0.08F) *
                                (1.0F - dry_wash * 0.50F) * lerp(0.76F, 1.28F, scree_patch);
            const float soil = (0.42F + deposition * 0.54F + (1.0F - slope_t) * 0.22F +
                                features.plateau_influence * 0.14F +
                                features.bench_influence * 0.14F + dry_wash * 0.70F) *
                               lerp(0.86F, 1.18F, material_noise);
            const float meadow =
                wetness * (1.0F - slope_t) * (0.40F + dry_wash * 0.18F) * (1.0F - high * 0.40F);
            const float forest = wetness * deposition * (1.0F - slope_t) * 0.020F *
                                 lerp(0.50F, 1.20F, vegetation_patch);
            const TerrainLabMaterialMask material =
                normalized_material_mask(rock, soil, scree, meadow, forest, 0.0F, 0.0F);
            fields.material_masks[sample] = material;

            const float grass =
                saturate((material.meadow * 0.36F + material.soil * 0.08F) *
                         (0.22F + wetness * 0.62F) * lerp(0.72F, 1.18F, vegetation_patch));
            const float shrub =
                saturate((material.soil * 0.20F + material.scree * 0.05F) * (1.0F - slope_t) *
                         (0.20F + wetness * 0.58F) * lerp(0.64F, 1.26F, vegetation_patch));
            const float tree = saturate(material.forest * 0.50F);
            fields.grass_density[sample] = grass;
            fields.shrub_density[sample] = shrub;
            fields.tree_density[sample] = tree;
            fields.canopy_height_m[sample] = tree <= 0.0F ? 0.0F : tree * lerp(1.5F, 5.0F, wetness);
        }
    }

    validate_terrain_lab_fields(fields);
    return fields;
}

TerrainLabFieldData generate_desert_dunes_fields(const TerrainLabConfig& config) {
    validate_terrain_lab_config(config);
    TerrainLabFieldData fields = make_empty_terrain_lab_fields(config);
    const std::size_t count = fields.sample_count();
    const float dune_elevation_scale_m = config.elevation_scale_m * 0.30F;
    fields.watershed_count = 1U;
    fields.max_channel_distance_m = 0.0F;

    for (std::uint32_t y = 0; y < fields.desc.height; ++y) {
        for (std::uint32_t x = 0; x < fields.desc.width; ++x) {
            const std::size_t sample = fields.index(x, y);
            const Point2 p = normalized_sample(fields.desc, x, y);
            const DesertDuneSampleFeatures features =
                desert_dune_features_at(p, fields.desc, config);
            const float broad =
                fbm((p.x * 1.1F) - 4.0F, (p.z * 1.1F) + 6.0F, config.seed + 3201U, 5);
            const float dune_body =
                saturate(features.ridge_influence * 0.72F + features.divide_influence * 0.34F);
            const float structure =
                ((dune_body * 0.56F) + (features.wind_shadow * 0.10F) + (broad * 0.07F) -
                 (features.interdune_flat * 0.18F) + (p.z * 0.035F) - 0.08F) *
                dune_elevation_scale_m * config.structure_strength;

            fields.ridge_influence[sample] = features.ridge_influence;
            fields.valley_influence[sample] = features.valley_influence;
            fields.basin_influence[sample] = features.basin_influence;
            fields.divide_influence[sample] = features.divide_influence;
            fields.channel_influence[sample] = features.channel_influence;
            fields.channel_distance_m[sample] = features.channel_distance_m;
            fields.max_channel_distance_m =
                std::max(fields.max_channel_distance_m, features.channel_distance_m);
            fields.structure_height_m[sample] = structure;
            fields.height_m[sample] = structure;
        }
    }

    std::vector<float> temp_slope;
    std::vector<float> temp_curvature;
    float temp_max_slope = 0.0F;
    float temp_max_curvature = 0.0F;
    compute_slope_and_curvature(fields.desc, fields.structure_height_m, temp_slope, temp_curvature,
                                temp_max_slope, temp_max_curvature);

    for (std::size_t sample = 0; sample < count; ++sample) {
        const float slope_t = smoothstep(0.015F, 0.22F, temp_slope[sample]);
        const float lee_deposit = fields.ridge_influence[sample] * (1.0F - slope_t) *
                                  dune_elevation_scale_m * 0.030F * config.process_strength;
        const float interdune_fill = fields.basin_influence[sample] * (1.0F - slope_t) *
                                     dune_elevation_scale_m * 0.018F * config.process_strength;
        const float wind_scour = fields.valley_influence[sample] *
                                 smoothstep(0.02F, 0.28F, slope_t) * dune_elevation_scale_m *
                                 0.022F * config.process_strength;
        fields.process_delta_m[sample] = lee_deposit + interdune_fill - wind_scour;
        fields.height_m[sample] =
            fields.structure_height_m[sample] + fields.process_delta_m[sample];
    }
    relax_steep_process_slopes(config, fields);
    update_height_range(fields);

    const float process_height_span = std::max(fields.max_height_m - fields.min_height_m, 1.0F);
    for (std::uint32_t y = 0; y < fields.desc.height; ++y) {
        for (std::uint32_t x = 0; x < fields.desc.width; ++x) {
            const std::size_t sample = fields.index(x, y);
            const Point2 p = normalized_sample(fields.desc, x, y);
            const DesertDuneSampleFeatures features =
                desert_dune_features_at(p, fields.desc, config);
            const float elevation_t =
                saturate((fields.height_m[sample] - fields.min_height_m) / process_height_span);
            const float ripple_noise =
                fbm((p.x * 15.0F) + 7.0F, (p.z * 15.0F) - 3.0F, config.seed + 3211U, 3) *
                    0.5F +
                0.5F;
            const float ripple_gate =
                smoothstep(0.12F, 0.64F, features.dune_crest + features.slip_face * 0.82F) *
                lerp(0.40F, 1.0F, ripple_noise);
            const float ripple =
                std::sin(((p.x * 24.0F) + (p.z * 17.0F)) +
                         fbm((p.x * 11.0F) + 9.0F, (p.z * 11.0F) - 5.0F, config.seed + 3213U, 3) *
                             3.0F) *
                0.0009F * ripple_gate;
            const float crest_break =
                fbm((p.x * 23.0F) - 11.0F, (p.z * 23.0F) + 13.0F, config.seed + 3217U, 3) *
                features.dune_crest * 0.0045F;
            const float interdune_crust =
                fbm((p.x * 12.0F) + 19.0F, (p.z * 12.0F) - 17.0F, config.seed + 3221U, 3) *
                features.interdune_flat * 0.0030F;
            const float detail = (ripple * (0.50F + elevation_t * 0.50F) + crest_break +
                                  interdune_crust) *
                                 dune_elevation_scale_m * config.detail_strength;
            fields.detail_height_m[sample] = detail;
            fields.height_m[sample] += detail;
        }
    }

    update_height_range(fields);
    compute_slope_and_curvature(fields.desc, fields.height_m, fields.slope, fields.curvature,
                                fields.max_slope, fields.max_abs_curvature);
    compute_flow_fields(fields.desc, fields.height_m, fields.slope, fields.flow_direction,
                        fields.flow_accumulation, fields.stream_power, fields.max_flow_accumulation,
                        fields.max_stream_power);

    const float inv_log_count =
        1.0F / std::log1p(static_cast<float>(std::max<std::size_t>(count, 1U)));
    fields.max_wetness = 0.0F;
    fields.max_deposition = 0.0F;
    for (std::uint32_t y = 0; y < fields.desc.height; ++y) {
        for (std::uint32_t x = 0; x < fields.desc.width; ++x) {
            const std::size_t sample = fields.index(x, y);
            const Point2 p = normalized_sample(fields.desc, x, y);
            const DesertDuneSampleFeatures features =
                desert_dune_features_at(p, fields.desc, config);
            const float slope_t = smoothstep(0.018F, 0.26F, fields.slope[sample]);
            const float flow_t = std::log1p(fields.flow_accumulation[sample]) * inv_log_count;
            const float wetness = saturate((features.interdune_flat * 0.028F) +
                                           (fields.basin_influence[sample] * 0.018F) +
                                           (flow_t * 0.012F));
            const float deposition =
                saturate((features.slip_face * 0.30F) + (features.interdune_flat * 0.36F) +
                         (features.wind_shadow * 0.20F)) *
                (1.0F - slope_t * 0.38F);
            fields.wetness[sample] = wetness;
            fields.deposition[sample] = deposition;
            fields.max_wetness = std::max(fields.max_wetness, wetness);
            fields.max_deposition = std::max(fields.max_deposition, deposition);

            const float material_noise =
                fbm((p.x * 7.0F) + 5.0F, (p.z * 7.0F) - 9.0F, config.seed + 3231U, 4) * 0.5F +
                0.5F;
            const float crust_patch =
                fbm((p.x * 10.0F) - 15.0F, (p.z * 10.0F) + 21.0F, config.seed + 3233U, 3) *
                    0.5F +
                0.5F;
            const float sand = (1.15F + deposition * 0.60F + features.dune_crest * 0.22F) *
                               lerp(0.86F, 1.16F, material_noise);
            const float soil = (features.interdune_flat * 0.18F + wetness * 0.42F) *
                               lerp(0.78F, 1.20F, crust_patch);
            const float scree = slope_t * features.slip_face * 0.10F;
            const float rock = slope_t * features.wind_shadow * 0.035F;
            const float meadow = wetness * features.interdune_flat * 0.05F;
            const TerrainLabMaterialMask material =
                normalized_material_mask(rock, soil, scree, meadow, 0.0F, 0.0F, sand);
            fields.material_masks[sample] = material;

            const float vegetation_patch =
                fbm((p.x * 5.8F) + 31.0F, (p.z * 5.8F) - 25.0F, config.seed + 3241U, 4) * 0.5F +
                0.5F;
            const float grass =
                saturate((material.soil * 0.08F + material.meadow * 0.22F) * wetness);
            const float shrub =
                saturate((features.interdune_flat * 0.045F + material.soil * 0.035F) *
                         (1.0F - slope_t) * lerp(0.35F, 1.15F, vegetation_patch));
            fields.grass_density[sample] = grass;
            fields.shrub_density[sample] = shrub;
            fields.tree_density[sample] = 0.0F;
            fields.canopy_height_m[sample] = 0.0F;
        }
    }

    validate_terrain_lab_fields(fields);
    return fields;
}

TerrainLabFieldData generate_alpine_glacial_valley_fields(const TerrainLabConfig& config) {
    validate_terrain_lab_config(config);
    TerrainLabFieldData fields = make_empty_terrain_lab_fields(config);
    const std::size_t count = fields.sample_count();
    const float alpine_elevation_scale_m = config.elevation_scale_m * 0.86F;
    fields.watershed_count = 1U;
    fields.max_channel_distance_m = 0.0F;

    for (std::uint32_t y = 0; y < fields.desc.height; ++y) {
        for (std::uint32_t x = 0; x < fields.desc.width; ++x) {
            const std::size_t sample = fields.index(x, y);
            const Point2 p = normalized_sample(fields.desc, x, y);
            const AlpineGlacialSampleFeatures features =
                alpine_glacial_features_at(p, fields.desc, config);
            const float headwater = saturate((1.0F - p.z) * 0.5F);
            const float broad =
                fbm((p.x * 1.2F) + 2.0F, (p.z * 1.2F) - 4.0F, config.seed + 3401U, 5);
            const float structure =
                ((headwater * 0.44F) + (features.ridge_influence * 0.36F) +
                 (features.peak_influence * 0.64F) + (features.cirque_influence * 0.18F) +
                 (features.valley_wall * 0.15F) + (broad * 0.12F) -
                 (features.glacier_floor * 0.32F) - (features.hanging_valley * 0.10F) -
                 (features.basin_influence * 0.08F) - 0.06F) *
                alpine_elevation_scale_m * config.structure_strength;

            fields.ridge_influence[sample] = features.ridge_influence;
            fields.valley_influence[sample] = features.valley_influence;
            fields.basin_influence[sample] = features.basin_influence;
            fields.divide_influence[sample] = features.divide_influence;
            fields.channel_influence[sample] = features.channel_influence;
            fields.channel_distance_m[sample] = features.channel_distance_m;
            fields.max_channel_distance_m =
                std::max(fields.max_channel_distance_m, features.channel_distance_m);
            fields.structure_height_m[sample] = structure;
            fields.height_m[sample] = structure;
        }
    }

    std::vector<float> temp_slope;
    std::vector<float> temp_curvature;
    float temp_max_slope = 0.0F;
    float temp_max_curvature = 0.0F;
    compute_slope_and_curvature(fields.desc, fields.structure_height_m, temp_slope, temp_curvature,
                                temp_max_slope, temp_max_curvature);

    for (std::uint32_t y = 0; y < fields.desc.height; ++y) {
        for (std::uint32_t x = 0; x < fields.desc.width; ++x) {
            const std::size_t sample = fields.index(x, y);
            const Point2 p = normalized_sample(fields.desc, x, y);
            const AlpineGlacialSampleFeatures features =
                alpine_glacial_features_at(p, fields.desc, config);
            const float slope_t = smoothstep(0.06F, 0.42F, temp_slope[sample]);
            const float glacial_carve =
                (features.glacier_floor * 0.085F + features.hanging_valley * 0.026F) *
                alpine_elevation_scale_m * config.process_strength;
            const float wall_polish = features.valley_wall * smoothstep(0.15F, 0.58F, slope_t) *
                                      alpine_elevation_scale_m * 0.032F *
                                      config.process_strength;
            const float moraine_deposit =
                features.moraine_influence * (1.0F - slope_t * 0.55F) *
                alpine_elevation_scale_m * 0.060F * config.process_strength;
            const float cirque_scour = features.cirque_influence * alpine_elevation_scale_m *
                                       0.030F * config.process_strength;
            fields.process_delta_m[sample] =
                -glacial_carve - wall_polish - cirque_scour + moraine_deposit;
            fields.height_m[sample] =
                fields.structure_height_m[sample] + fields.process_delta_m[sample];
        }
    }
    relax_steep_process_slopes(config, fields);
    update_height_range(fields);

    const float process_height_span = std::max(fields.max_height_m - fields.min_height_m, 1.0F);
    for (std::uint32_t y = 0; y < fields.desc.height; ++y) {
        for (std::uint32_t x = 0; x < fields.desc.width; ++x) {
            const std::size_t sample = fields.index(x, y);
            const Point2 p = normalized_sample(fields.desc, x, y);
            const AlpineGlacialSampleFeatures features =
                alpine_glacial_features_at(p, fields.desc, config);
            const float elevation_t =
                saturate((fields.height_m[sample] - fields.min_height_m) / process_height_span);
            const float ridge_detail =
                fbm((p.x * 19.0F) - 8.0F, (p.z * 19.0F) + 10.0F, config.seed + 3411U, 4) *
                fields.ridge_influence[sample] * 0.014F;
            const float peak_detail =
                fbm((p.x * 24.0F) + 5.0F, (p.z * 24.0F) - 9.0F, config.seed + 3413U, 4) *
                features.peak_influence * 0.018F;
            const float wall_groove =
                fbm((p.x * 28.0F) + 13.0F, (p.z * 28.0F) - 17.0F, config.seed + 3417U, 3) *
                features.valley_wall * 0.007F;
            const float moraine_roughness =
                fbm((p.x * 15.0F) - 21.0F, (p.z * 15.0F) + 25.0F, config.seed + 3421U, 3) *
                features.moraine_influence * 0.006F;
            const float snow_smoothing =
                (features.glacier_floor * 0.82F + features.peak_influence * 0.28F) *
                smoothstep(0.46F, 0.86F, elevation_t);
            const float detail = (ridge_detail + peak_detail + wall_groove + moraine_roughness) *
                                 alpine_elevation_scale_m * config.detail_strength *
                                 (1.0F - snow_smoothing * 0.50F);
            fields.detail_height_m[sample] = detail;
            fields.height_m[sample] += detail;
        }
    }

    update_height_range(fields);
    compute_slope_and_curvature(fields.desc, fields.height_m, fields.slope, fields.curvature,
                                fields.max_slope, fields.max_abs_curvature);
    compute_flow_fields(fields.desc, fields.height_m, fields.slope, fields.flow_direction,
                        fields.flow_accumulation, fields.stream_power, fields.max_flow_accumulation,
                        fields.max_stream_power);

    const float height_span = std::max(fields.max_height_m - fields.min_height_m, 1.0F);
    const float inv_log_count =
        1.0F / std::log1p(static_cast<float>(std::max<std::size_t>(count, 1U)));
    fields.max_wetness = 0.0F;
    fields.max_deposition = 0.0F;
    for (std::uint32_t y = 0; y < fields.desc.height; ++y) {
        for (std::uint32_t x = 0; x < fields.desc.width; ++x) {
            const std::size_t sample = fields.index(x, y);
            const Point2 p = normalized_sample(fields.desc, x, y);
            const AlpineGlacialSampleFeatures features =
                alpine_glacial_features_at(p, fields.desc, config);
            const float elevation_t =
                saturate((fields.height_m[sample] - fields.min_height_m) / height_span);
            const float slope_t = smoothstep(0.06F, 0.48F, fields.slope[sample]);
            const float flow_t = std::log1p(fields.flow_accumulation[sample]) * inv_log_count;
            const float wetness = saturate((fields.channel_influence[sample] * 0.18F) +
                                           (features.glacier_floor * 0.10F) +
                                           (features.basin_influence * 0.06F) +
                                           (flow_t * 0.08F));
            const float deposition =
                saturate((features.moraine_influence * 0.58F) +
                         (features.glacier_floor * (1.0F - slope_t) * 0.20F) +
                         (fields.channel_influence[sample] * 0.12F));
            fields.wetness[sample] = wetness;
            fields.deposition[sample] = deposition;
            fields.max_wetness = std::max(fields.max_wetness, wetness);
            fields.max_deposition = std::max(fields.max_deposition, deposition);

            const float material_noise =
                fbm((p.x * 7.0F) - 4.0F, (p.z * 7.0F) + 6.0F, config.seed + 3431U, 4) * 0.5F +
                0.5F;
            const float scree_patch =
                fbm((p.x * 13.0F) + 18.0F, (p.z * 13.0F) - 12.0F, config.seed + 3433U, 3) *
                    0.5F +
                0.5F;
            const float snow =
                (smoothstep(0.54F, 0.88F, elevation_t) * (1.0F - slope_t * 0.45F) +
                 features.glacier_floor * smoothstep(0.30F, 0.76F, elevation_t) * 0.46F +
                 features.cirque_influence * 0.36F + features.peak_influence * 0.42F) *
                lerp(0.78F, 1.18F, material_noise);
            const float rock = (slope_t * 0.82F + fields.ridge_influence[sample] * 0.30F +
                                features.valley_wall * 0.24F + features.peak_influence * 0.18F) *
                               (1.0F - snow * 0.58F) * lerp(0.82F, 1.22F, material_noise);
            const float scree = smoothstep(0.28F, 0.78F, slope_t) *
                                (1.0F - smoothstep(0.78F, 1.0F, slope_t)) *
                                (features.valley_wall * 0.46F + fields.ridge_influence[sample] * 0.22F +
                                 features.moraine_influence * 0.30F +
                                 features.peak_influence * 0.14F) *
                                (1.0F - snow * 0.45F) * lerp(0.72F, 1.30F, scree_patch);
            const float soil =
                (0.20F + deposition * 0.52F + (1.0F - slope_t) * 0.14F) *
                (1.0F - snow * 0.78F);
            const float meadow = wetness * (1.0F - slope_t) * (1.0F - snow * 0.86F) *
                                 (0.28F + features.glacier_floor * 0.20F);
            const float forest = smoothstep(0.18F, 0.46F, wetness) * (1.0F - slope_t) *
                                 (1.0F - smoothstep(0.42F, 0.72F, elevation_t)) *
                                 (1.0F - snow) * 0.26F;
            const TerrainLabMaterialMask material =
                normalized_material_mask(rock, soil, scree, meadow, forest, snow, 0.0F);
            fields.material_masks[sample] = material;

            const float grass =
                saturate((material.meadow * 0.55F + material.soil * 0.18F) * (1.0F - snow));
            const float shrub = saturate((material.soil * 0.18F + material.scree * 0.10F) *
                                         (1.0F - slope_t) * (1.0F - elevation_t * 0.70F));
            const float tree = saturate(material.forest * 0.60F);
            fields.grass_density[sample] = grass;
            fields.shrub_density[sample] = shrub;
            fields.tree_density[sample] = tree;
            fields.canopy_height_m[sample] =
                tree <= 0.0F ? 0.0F : tree * lerp(2.0F, 10.0F, wetness);
        }
    }

    validate_terrain_lab_fields(fields);
    return fields;
}

TerrainLabFieldData generate_terrain_lab_fields(const TerrainLabConfig& config) {
    validate_terrain_lab_config(config);
    switch (config.slice_preset) {
    case TerrainLabSlicePreset::AridMesaCanyon:
        return generate_arid_mesa_canyon_fields(config);
    case TerrainLabSlicePreset::TemperateMountainWatershed:
        return generate_temperate_mountain_watershed_fields(config);
    case TerrainLabSlicePreset::DesertDunes:
        return generate_desert_dunes_fields(config);
    case TerrainLabSlicePreset::AlpineGlacialValley:
        return generate_alpine_glacial_valley_fields(config);
    }
    return generate_arid_mesa_canyon_fields(config);
}

} // namespace cubey::projects::terrain_lab
