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

[[nodiscard]] float length(Point2 p) {
    return std::sqrt((p.x * p.x) + (p.z * p.z));
}

[[nodiscard]] float dot(Point2 a, Point2 b) {
    return (a.x * b.x) + (a.z * b.z);
}

[[nodiscard]] Point2 normalize(Point2 p) {
    const float p_length = length(p);
    if (p_length <= 0.0F) {
        return {};
    }
    return {.x = p.x / p_length, .z = p.z / p_length};
}

[[nodiscard]] float distance_to_line(Point2 p, Point2 origin, Point2 direction) {
    const Point2 axis = normalize(direction);
    if (axis.x == 0.0F && axis.z == 0.0F) {
        return length({.x = p.x - origin.x, .z = p.z - origin.z});
    }
    const Point2 normal{.x = -axis.z, .z = axis.x};
    return std::abs(dot({.x = p.x - origin.x, .z = p.z - origin.z}, normal));
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
                                                              float snow) {
    rock = std::max(rock, 0.0F);
    soil = std::max(soil, 0.0F);
    scree = std::max(scree, 0.0F);
    meadow = std::max(meadow, 0.0F);
    forest = std::max(forest, 0.0F);
    snow = std::max(snow, 0.0F);
    const float sum = rock + soil + scree + meadow + forest + snow;
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
    };
}

[[nodiscard]] float material_entropy(const TerrainLabMaterialMask& mask) {
    const std::array<float, 6> weights{
        mask.rock, mask.soil, mask.scree, mask.meadow, mask.forest, mask.snow,
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

    const Point2 wash_a_origin{.x = -0.86F, .z = -0.64F};
    const Point2 wash_a_direction{.x = 1.18F, .z = 1.58F};
    const Point2 wash_a_axis = normalize(wash_a_direction);
    const Point2 wash_a_offset{.x = p.x - wash_a_origin.x, .z = p.z - wash_a_origin.z};
    const float wash_a_t = dot(wash_a_offset, wash_a_axis);
    const float wash_a_gate =
        smoothstep(0.04F, 0.42F, wash_a_t) * (1.0F - smoothstep(1.70F, 2.12F, wash_a_t));
    const float wash_a_distance = distance_to_line(p, wash_a_origin, wash_a_direction);
    const float wash_a = (1.0F - smoothstep(0.016F, 0.105F, wash_a_distance)) * wash_a_gate;

    const Point2 wash_b_origin{.x = 0.88F, .z = -0.26F};
    const Point2 wash_b_direction{.x = -1.14F, .z = 1.34F};
    const Point2 wash_b_axis = normalize(wash_b_direction);
    const Point2 wash_b_offset{.x = p.x - wash_b_origin.x, .z = p.z - wash_b_origin.z};
    const float wash_b_t = dot(wash_b_offset, wash_b_axis);
    const float wash_b_gate =
        smoothstep(0.02F, 0.36F, wash_b_t) * (1.0F - smoothstep(1.42F, 1.92F, wash_b_t));
    const float wash_b_distance = distance_to_line(p, wash_b_origin, wash_b_direction);
    const float wash_b = (1.0F - smoothstep(0.014F, 0.092F, wash_b_distance)) * wash_b_gate;

    const float wash_patch = smoothstep(
        0.18F, 0.80F,
        fbm((p.x * 5.2F) + 4.0F, (p.z * 5.2F) - 8.0F, config.seed + 2617U, 4) * 0.5F + 0.5F);
    const float wash_influence =
        saturate(std::max(wash_a, wash_b) * lerp(0.74F, 1.16F, wash_patch));
    const float wash_distance = std::min(wash_a_distance, wash_b_distance);
    const float channel_distance_norm = std::min(canyon_distance, wash_distance);
    const float extent_m = std::max(half_extent_x_m(desc), half_extent_z_m(desc));

    const float plateau_noise =
        fbm((p.x * 1.35F) + 9.0F, (p.z * 1.35F) - 3.0F, config.seed + 2621U, 4) * 0.5F + 0.5F;
    const float plateau = saturate((1.0F - canyon_broad * 0.68F - wash_influence * 0.28F) *
                                   lerp(0.78F, 1.08F, plateau_noise));
    const float mesa_rim = smoothstep(0.16F, 0.32F, canyon_distance) *
                           (1.0F - smoothstep(0.34F, 0.62F, canyon_distance));
    const float channel = saturate(std::max(canyon_floor, wash_influence * 0.50F));
    const float valley = saturate((canyon_broad * 0.80F) + (wash_influence * 0.28F));
    const float basin = saturate((canyon_floor * 0.76F) + (wash_influence * 0.12F));
    const float ridge = saturate((canyon_wall * 0.82F) + (mesa_rim * 0.28F) +
                                 plateau * (0.08F + plateau_noise * 0.06F));
    const float divide = saturate((plateau * 0.24F) + (mesa_rim * 0.40F) + (1.0F - valley) * 0.10F);

    return {
        .canyon_floor = saturate(canyon_floor),
        .canyon_wall = saturate(canyon_wall),
        .wash_influence = wash_influence,
        .plateau_influence = plateau,
        .ridge_influence = ridge,
        .valley_influence = valley,
        .basin_influence = basin,
        .divide_influence = divide,
        .channel_influence = channel,
        .channel_distance_m = std::max(channel_distance_norm * extent_m, 0.0F),
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
    const float sum = mask.rock + mask.soil + mask.scree + mask.meadow + mask.forest + mask.snow;
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
    double non_channel_flow_sum = 0.0;
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
            ++summary.channel_sample_count;
        }
        if (fields.channel_influence[sample] < 0.05F && fields.divide_influence[sample] < 0.25F) {
            non_channel_flow_sum += fields.flow_accumulation[sample];
            ++summary.non_channel_sample_count;
        }
        if (fields.divide_influence[sample] > 0.55F && fields.channel_influence[sample] < 0.20F) {
            divide_height_sum += fields.height_m[sample];
            ++summary.divide_sample_count;
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
    }
    if (summary.non_channel_sample_count > 0U) {
        summary.mean_non_channel_flow_accumulation = static_cast<float>(
            non_channel_flow_sum / static_cast<double>(summary.non_channel_sample_count));
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
                normalized_material_mask(rock, soil, scree, meadow, forest, snow);
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
            const float canyon_cut = (features.valley_influence * 0.20F) +
                                     (features.canyon_floor * 0.24F) +
                                     (features.wash_influence * 0.09F);
            const float structure =
                ((high_desert_tilt * 0.24F) + (features.plateau_influence * 0.30F) +
                 (features.ridge_influence * 0.10F) + (mesa_bench * 0.08F) + (broad * 0.12F) +
                 (bench_noise * 0.045F) - canyon_cut - 0.08F) *
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
            const float exposed =
                saturate((features.canyon_wall * 0.78F) + (fields.ridge_influence[sample] * 0.36F) +
                         (features.plateau_influence * 0.14F));
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
            const float detail = (strata + rock_roughness + desert_pavement) *
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
            const float rock = ((slope_t * 0.82F) + (features.canyon_wall * 0.72F) +
                                (fields.ridge_influence[sample] * 0.30F)) *
                               (1.0F - dry_wash * 0.45F) * lerp(0.84F, 1.20F, material_noise);
            const float scree = smoothstep(0.18F, 0.72F, slope_t) *
                                (1.0F - smoothstep(0.78F, 1.0F, slope_t)) *
                                (0.36F + features.canyon_wall * 0.54F + dry_wash * 0.12F) *
                                (1.0F - dry_wash * 0.35F) * lerp(0.76F, 1.28F, scree_patch);
            const float soil = (0.42F + deposition * 0.54F + (1.0F - slope_t) * 0.22F +
                                features.plateau_influence * 0.14F + dry_wash * 0.35F) *
                               lerp(0.86F, 1.18F, material_noise);
            const float meadow =
                wetness * (1.0F - slope_t) * (0.40F + dry_wash * 0.18F) * (1.0F - high * 0.40F);
            const float forest = wetness * deposition * (1.0F - slope_t) * 0.020F *
                                 lerp(0.50F, 1.20F, vegetation_patch);
            const TerrainLabMaterialMask material =
                normalized_material_mask(rock, soil, scree, meadow, forest, 0.0F);
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

TerrainLabFieldData generate_terrain_lab_fields(const TerrainLabConfig& config) {
    validate_terrain_lab_config(config);
    switch (config.slice_preset) {
    case TerrainLabSlicePreset::AridMesaCanyon:
        return generate_arid_mesa_canyon_fields(config);
    case TerrainLabSlicePreset::TemperateMountainWatershed:
        return generate_temperate_mountain_watershed_fields(config);
    }
    return generate_arid_mesa_canyon_fields(config);
}

} // namespace cubey::projects::terrain_lab
