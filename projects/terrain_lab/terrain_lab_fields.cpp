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
constexpr std::size_t kNoFlowReceiver = std::numeric_limits<std::size_t>::max();

constexpr std::array<std::int32_t, 8> kFlowDx{-1, 0, 1, -1, 1, -1, 0, 1};
constexpr std::array<std::int32_t, 8> kFlowDy{-1, -1, -1, 0, 0, 1, 1, 1};
constexpr std::array<std::uint8_t, 8> kFlowFacetRing{4U, 2U, 1U, 0U, 3U, 5U, 6U, 7U};

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

struct AridMesaDriver {
    float base_potential = 0.0F;
    float relief_potential = 0.0F;
    float process_potential = 0.0F;
    float selection_mask = 1.0F;
    float canyon_floor_source = 0.0F;
    float canyon_broad_source = 0.0F;
    float canyon_wall_source = 0.0F;
    float wash_source = 0.0F;
    float plateau_source = 0.0F;
    float rim_source = 0.0F;
    float bench_source = 0.0F;
    float talus_source = 0.0F;
    float plateau_noise_source = 0.0F;
    float broad_source = 0.0F;
    float bench_noise_source = 0.0F;
    float channel_distance_norm = 0.0F;
};

struct AridRegionalCanyonFields {
    TerrainLabGridDesc desc{};
    std::vector<float> macro_height_m{};
    std::vector<float> base_potential{};
    std::vector<float> relief_potential{};
    std::vector<float> process_potential{};
    std::vector<float> runoff{};
    std::vector<float> resistance{};
    std::vector<float> plateau{};
    std::vector<float> canyon_floor{};
    std::vector<float> canyon_broad{};
    std::vector<float> canyon_wall{};
    std::vector<float> wash{};
    std::vector<float> rim{};
    std::vector<float> bench{};
    std::vector<float> talus{};
    std::vector<float> broad{};
    std::vector<float> bench_noise{};
    std::vector<float> channel_distance_m{};
    std::vector<float> incision{};
};

struct FlowReceiver {
    std::size_t first = kNoFlowReceiver;
    std::size_t second = kNoFlowReceiver;
    float first_weight = 0.0F;
    float second_weight = 0.0F;
    std::uint8_t first_direction = kFlowSinkDirection;
    std::uint8_t second_direction = kFlowSinkDirection;
    float vector_x = 0.0F;
    float vector_y = 0.0F;
};

struct FlowRoutingData {
    std::vector<FlowReceiver> receivers{};
};

struct FlowCandidate {
    FlowReceiver receiver{};
    float slope = 0.0F;
};

struct AridDrainageTracePoint {
    float x = 0.0F;
    float y = 0.0F;
    float strength = 0.0F;
    float incision = 0.0F;
};

struct AridDrainageTrace {
    std::vector<AridDrainageTracePoint> points{};
    float strength = 0.0F;
};

struct AridTraceSeedCandidate {
    std::size_t sample = 0;
    float score = 0.0F;
};

struct RiverDerivationParams {
    float runoff_scale = 1.0F;
    float water_presence_scale = 1.0F;
    float min_width_m = 4.0F;
    float max_width_m = 48.0F;
    float valley_width_multiplier = 5.0F;
    float stream_threshold = 0.28F;
    float local_active_scale = 0.12F;
    bool extract_visible_trunks = false;
    bool prune_disconnected_fragments = false;
};

struct RiverBranchCandidate {
    std::size_t sample = 0;
    std::size_t anchor_index = 0;
    float score = 0.0F;
};

struct AridCanyonCrop {
    std::uint32_t offset_x = 0;
    std::uint32_t offset_y = 0;
};

struct AridMesaSliceFields {
    std::vector<AridMesaDriver> drivers{};
    std::vector<AridMesaSampleFeatures> features{};
};

struct DesertDuneSampleFeatures {
    float dune_body = 0.0F;
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

struct DesertDuneDriver {
    float base_potential = 0.0F;
    float relief_potential = 0.0F;
    float process_potential = 0.0F;
    float selection_mask = 0.0F;
    float crest_source = 0.0F;
    float interdune_source = 0.0F;
    float dune_u = 0.0F;
    float dune_v = 0.0F;
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

struct AlpineGlacialDriver {
    float base_potential = 0.0F;
    float relief_potential = 0.0F;
    float process_potential = 0.0F;
    float selection_mask = 1.0F;
    float ice_accumulation = 0.0F;
    float valley_source = 0.0F;
    float ridge_source = 0.0F;
    float cliff_source = 0.0F;
    float peak_source = 0.0F;
    float moraine_source = 0.0F;
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

[[nodiscard]] float ridge_profile(float value, float sharpness) {
    return std::pow(std::max(1.0F - std::abs(value), 0.0F), sharpness);
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
    fields.driver_base_potential.assign(count, 0.0F);
    fields.driver_relief_potential.assign(count, 0.0F);
    fields.driver_process_potential.assign(count, 0.0F);
    fields.driver_selection_mask.assign(count, 0.0F);
    fields.structure_height_m.assign(count, 0.0F);
    fields.process_delta_m.assign(count, 0.0F);
    fields.detail_height_m.assign(count, 0.0F);
    fields.slope.assign(count, 0.0F);
    fields.curvature.assign(count, 0.0F);
    fields.flow_direction.assign(count, kFlowSinkDirection);
    fields.flow_accumulation.assign(count, 1.0F);
    fields.stream_power.assign(count, 0.0F);
    fields.river_discharge.assign(count, 0.0F);
    fields.stream_order.assign(count, 0U);
    fields.river_width_m.assign(count, 0.0F);
    fields.valley_width_m.assign(count, 0.0F);
    fields.water_presence.assign(count, 0.0F);
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
    fields.drainage_region_id.assign(count, 0U);
    fields.divide_influence.assign(count, 0.0F);
    fields.channel_influence.assign(count, 0.0F);
    fields.channel_distance_m.assign(count, 0.0F);
    return fields;
}

[[nodiscard]] DesertDuneDriver desert_dune_driver_at(Point2 p, const TerrainLabConfig& config) {
    const float angle = lerp(0.32F, 0.68F, random01(config.seed, 21U, 1201U));
    const Point2 wind{std::cos(angle), std::sin(angle)};
    const Point2 cross{-wind.z, wind.x};
    const float downwind = dot(p, wind);
    const float lateral = dot(p, cross);
    const float warp_down =
        fbm((p.x * 1.15F) - 3.0F, (p.z * 1.15F) + 4.0F, config.seed + 3101U, 4) * 0.22F;
    const float warp_lateral =
        fbm((p.x * 1.30F) + 6.0F, (p.z * 1.30F) - 7.0F, config.seed + 3103U, 4) * 0.18F;
    const float dune_u = downwind + warp_down;
    const float dune_v = lateral + warp_lateral;

    const float supply_noise =
        fbm((dune_u * 0.92F) + 1.7F, (dune_v * 0.84F) - 3.2F, config.seed + 3107U, 5) * 0.5F + 0.5F;
    const float base_noise =
        fbm((dune_u * 0.54F) - 2.1F, (dune_v * 0.62F) + 4.8F, config.seed + 3109U, 4) * 0.5F + 0.5F;
    const float selection = 1.0F;
    const float base = saturate(0.34F + (supply_noise * 0.36F) + (base_noise * 0.30F));

    const float mound_a =
        fbm((dune_u * 1.26F) + 5.7F, (dune_v * 1.18F) - 6.1F, config.seed + 3127U, 5) * 0.5F + 0.5F;
    const float mound_b =
        fbm((dune_u * 1.72F) - 3.4F, (dune_v * 1.58F) + 7.6F, config.seed + 3129U, 4) * 0.5F + 0.5F;
    const float billow =
        smoothstep(0.20F, 0.84F, mound_a) * 0.62F + smoothstep(0.28F, 0.86F, mound_b) * 0.38F;
    const float roll_source =
        fbm((dune_u * 1.52F) - 4.0F, (dune_v * 1.16F) + 8.5F, config.seed + 3119U, 5);
    const float roll_secondary =
        fbm((dune_u * 2.18F) + 2.1F, (dune_v * 1.74F) - 8.2F, config.seed + 3121U, 4);
    const float soft_ridge = std::max(ridge_profile(roll_source * 1.05F, 1.72F),
                                      ridge_profile(roll_secondary * 1.16F, 1.95F) * 0.26F);
    const float relief = saturate(
        ((billow * 0.58F) + (soft_ridge * 0.27F) + (base * 0.18F) + (supply_noise * 0.10F)) *
        selection);

    const float lee_source =
        fbm((dune_u * 2.04F) - 7.0F, (dune_v * 1.84F) + 9.0F, config.seed + 3131U, 4) * 0.5F + 0.5F;
    const float transport = smoothstep(
        0.18F, 0.86F,
        fbm((dune_u * 0.92F) + 8.0F, (dune_v * 1.08F) - 10.0F, config.seed + 3135U, 4) * 0.5F +
            0.5F);
    const float crest_source =
        smoothstep(0.54F, 0.86F, soft_ridge) * smoothstep(0.44F, 0.82F, relief) * selection;
    const float interdune_source =
        saturate((1.0F - smoothstep(0.22F, 0.58F, relief)) * lerp(0.50F, 1.0F, selection));
    const float process =
        saturate((crest_source * 0.28F) + (lee_source * smoothstep(0.36F, 0.82F, relief) * 0.34F) +
                 (transport * 0.22F) + (interdune_source * 0.10F)) *
        selection;

    return {
        .base_potential = base,
        .relief_potential = relief,
        .process_potential = process,
        .selection_mask = selection,
        .crest_source = crest_source,
        .interdune_source = interdune_source,
        .dune_u = dune_u,
        .dune_v = dune_v,
    };
}

[[nodiscard]] DesertDuneSampleFeatures
desert_dune_features_at(Point2 p, const TerrainLabGridDesc& desc, const TerrainLabConfig& config) {
    const DesertDuneDriver driver = desert_dune_driver_at(p, config);
    const float dune_body =
        smoothstep(0.24F, 0.76F, driver.relief_potential) * driver.selection_mask;
    const float crest = driver.crest_source;
    const float slip_face = smoothstep(0.48F, 0.84F, driver.relief_potential) *
                            smoothstep(0.36F, 0.82F, driver.process_potential) *
                            driver.selection_mask * 0.78F;
    const float interdune = driver.interdune_source;
    const float wind_shadow = saturate(slip_face * 0.68F + crest * 0.18F) *
                              smoothstep(0.20F, 0.82F, driver.process_potential);
    const float broad_low =
        saturate((1.0F - driver.base_potential) * 0.74F + (1.0F - driver.selection_mask) * 0.26F);
    const float channel =
        saturate(interdune * 0.12F + broad_low * 0.20F +
                 smoothstep(0.56F, 0.86F, interdune) * 0.12F + wind_shadow * 0.03F);
    const float channel_distance_m =
        (1.0F - interdune) * std::max(half_extent_x_m(desc), half_extent_z_m(desc));
    return {
        .dune_body = saturate(dune_body),
        .dune_crest = saturate(crest),
        .slip_face = saturate(slip_face),
        .interdune_flat = saturate(interdune),
        .wind_shadow = saturate(wind_shadow),
        .ridge_influence = saturate(crest * 0.50F + slip_face * 0.34F + dune_body * 0.20F),
        .valley_influence = saturate(interdune * 0.72F + broad_low * 0.18F),
        .basin_influence = saturate(broad_low * 0.70F + interdune * 0.16F),
        .divide_influence = saturate(crest * 0.34F + dune_body * 0.10F),
        .channel_influence = channel,
        .channel_distance_m = std::max(channel_distance_m, 0.0F),
    };
}

[[nodiscard]] AlpineGlacialDriver alpine_glacial_driver_at(Point2 p,
                                                           const TerrainLabConfig& config) {
    const float warp_x =
        fbm((p.x * 1.35F) - 6.0F, (p.z * 1.35F) + 8.0F, config.seed + 3301U, 4) * 0.18F;
    const float warp_z =
        fbm((p.x * 1.20F) + 5.0F, (p.z * 1.20F) - 7.0F, config.seed + 3303U, 4) * 0.14F;
    const Point2 q{p.x + warp_x, p.z + warp_z};
    const float headwall = saturate((1.0F - p.z) * 0.5F);
    const float base_noise =
        fbm((q.x * 0.94F) + 2.0F, (q.z * 0.94F) - 4.0F, config.seed + 3401U, 5) * 0.5F + 0.5F;
    const float base = saturate(0.24F + headwall * 0.42F + base_noise * 0.34F);
    const float side_lift = smoothstep(
        0.12F, 0.82F,
        std::abs(q.x) +
            fbm((q.x * 1.7F) + 3.0F, (q.z * 1.7F) - 5.0F, config.seed + 3305U, 4) * 0.16F);
    const float ridge_noise_a =
        fbm((q.x * 2.15F) - 9.0F, (q.z * 1.75F) + 5.0F, config.seed + 3307U, 5);
    const float ridge_noise_b =
        fbm((q.x * 3.55F) + 11.0F, (q.z * 2.80F) - 13.0F, config.seed + 3309U, 4);
    const float ridged = std::max(ridge_profile(ridge_noise_a * 1.12F, 1.82F),
                                  ridge_profile(ridge_noise_b * 1.20F, 2.18F) * 0.42F);
    const float relief =
        saturate(base * 0.18F + side_lift * 0.32F + ridged * 0.42F + headwall * 0.10F);
    const float low_relief = 1.0F - smoothstep(0.32F, 0.76F, relief);
    const float ice_noise =
        fbm((q.x * 1.62F) + 13.0F, (q.z * 1.62F) - 17.0F, config.seed + 3311U, 4) * 0.5F + 0.5F;
    const float ice = saturate(headwall * 0.34F + low_relief * 0.42F + base * 0.16F +
                               smoothstep(0.34F, 0.78F, ice_noise) * 0.16F);
    const float valley = saturate(ice * 0.58F + low_relief * 0.38F - side_lift * 0.08F);
    const float ridge = saturate(relief * (1.0F - valley * 0.52F) + ridged * 0.30F);
    const float cliff =
        saturate(smoothstep(0.44F, 0.82F, relief) * (1.0F - smoothstep(0.56F, 0.92F, valley)) *
                 (0.52F + ridged * 0.48F));
    const float peak = saturate(smoothstep(0.62F, 0.90F, relief) * (0.45F + ridged * 0.55F) *
                                (1.0F - valley * 0.72F));
    const float moraine_band =
        smoothstep(-0.16F, 0.34F, p.z) * (1.0F - smoothstep(0.58F, 0.92F, p.z));
    const float moraine = saturate(valley * moraine_band * (0.46F + ice * 0.34F) +
                                   cliff * smoothstep(0.42F, 0.86F, p.z) * 0.14F);
    const float process = saturate(ice * 0.48F + moraine * 0.30F + cliff * 0.14F + valley * 0.10F);
    return {
        .base_potential = base,
        .relief_potential = relief,
        .process_potential = process,
        .selection_mask = 1.0F,
        .ice_accumulation = ice,
        .valley_source = valley,
        .ridge_source = ridge,
        .cliff_source = cliff,
        .peak_source = peak,
        .moraine_source = moraine,
    };
}

[[nodiscard]] AlpineGlacialSampleFeatures
alpine_glacial_features_at(Point2 p, const TerrainLabGridDesc& desc,
                           const TerrainLabConfig& config) {
    const AlpineGlacialDriver driver = alpine_glacial_driver_at(p, config);
    const float floor = smoothstep(0.50F, 0.86F, driver.valley_source);
    const float cliff = driver.cliff_source;
    const float peak = driver.peak_source;
    const float ridge = driver.ridge_source;
    const float cirque =
        saturate(driver.ice_accumulation * smoothstep(0.58F, 0.92F, driver.base_potential) *
                 (1.0F - smoothstep(0.62F, 0.92F, driver.relief_potential)));
    const float hanging = saturate(
        driver.ice_accumulation * driver.relief_potential *
        smoothstep(0.42F, 0.78F,
                   fbm((p.x * 4.8F) + 17.0F, (p.z * 4.8F) - 19.0F, config.seed + 3313U, 4) * 0.5F +
                       0.5F) *
        0.42F);
    const float moraine = driver.moraine_source;
    const float basin =
        saturate(floor * 0.58F + moraine * 0.18F + driver.process_potential * 0.08F);
    const float channel = saturate(floor * 0.50F + hanging * 0.14F);
    const float channel_distance_m =
        (1.0F - floor) * std::max(half_extent_x_m(desc), half_extent_z_m(desc));
    return {
        .glacier_floor = saturate(floor),
        .valley_wall = saturate(cliff),
        .hanging_valley = hanging,
        .moraine_influence = saturate(moraine),
        .cirque_influence = saturate(cirque),
        .peak_influence = peak,
        .ridge_influence = saturate((ridge + cirque * 0.16F) * (1.0F - floor * 0.30F)),
        .valley_influence = saturate(floor * 0.78F + hanging * 0.30F),
        .basin_influence = basin,
        .divide_influence =
            saturate(smoothstep(0.30F, 0.74F, ridge * 0.50F + peak * 0.30F + cliff * 0.18F) *
                     (1.0F - floor * 0.34F)),
        .channel_influence = channel,
        .channel_distance_m = std::max(channel_distance_m, 0.0F),
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

[[nodiscard]] bool flow_neighbor(const TerrainLabGridDesc& desc, std::uint32_t x, std::uint32_t y,
                                 std::uint8_t direction, std::uint32_t& nx,
                                 std::uint32_t& ny) {
    const auto ix = static_cast<std::int32_t>(x) + kFlowDx[direction];
    const auto iy = static_cast<std::int32_t>(y) + kFlowDy[direction];
    if (ix < 0 || iy < 0 || ix >= static_cast<std::int32_t>(desc.width) ||
        iy >= static_cast<std::int32_t>(desc.height)) {
        return false;
    }
    nx = static_cast<std::uint32_t>(ix);
    ny = static_cast<std::uint32_t>(iy);
    return true;
}

[[nodiscard]] float direction_distance_cells(std::uint8_t direction) {
    return (kFlowDx[direction] != 0 && kFlowDy[direction] != 0) ? 1.41421356F : 1.0F;
}

[[nodiscard]] FlowCandidate single_flow_candidate(const TerrainLabGridDesc& desc,
                                                  const std::vector<float>& height,
                                                  std::uint32_t x, std::uint32_t y,
                                                  std::uint8_t direction) {
    std::uint32_t nx = 0;
    std::uint32_t ny = 0;
    if (!flow_neighbor(desc, x, y, direction, nx, ny)) {
        return {};
    }

    const std::size_t sample = grid_index(x, y, desc.width);
    const std::size_t receiver = grid_index(nx, ny, desc.width);
    const float distance = direction_distance_cells(direction);
    const float slope = (height[sample] - height[receiver]) / (distance * desc.cell_size_m);
    if (slope <= 0.0F) {
        return {};
    }

    FlowCandidate candidate;
    candidate.slope = slope;
    candidate.receiver.first = receiver;
    candidate.receiver.first_weight = 1.0F;
    candidate.receiver.first_direction = direction;
    candidate.receiver.vector_x = static_cast<float>(kFlowDx[direction]) / distance;
    candidate.receiver.vector_y = static_cast<float>(kFlowDy[direction]) / distance;
    return candidate;
}

[[nodiscard]] FlowCandidate triangular_flow_candidate(const TerrainLabGridDesc& desc,
                                                      const std::vector<float>& height,
                                                      std::uint32_t x, std::uint32_t y,
                                                      std::uint8_t first_direction,
                                                      std::uint8_t second_direction) {
    std::uint32_t ax = 0;
    std::uint32_t ay = 0;
    std::uint32_t bx = 0;
    std::uint32_t by = 0;
    const bool has_a = flow_neighbor(desc, x, y, first_direction, ax, ay);
    const bool has_b = flow_neighbor(desc, x, y, second_direction, bx, by);
    if (!has_a && !has_b) {
        return {};
    }
    if (!has_a) {
        return single_flow_candidate(desc, height, x, y, second_direction);
    }
    if (!has_b) {
        return single_flow_candidate(desc, height, x, y, first_direction);
    }

    const std::size_t sample = grid_index(x, y, desc.width);
    const std::size_t receiver_a = grid_index(ax, ay, desc.width);
    const std::size_t receiver_b = grid_index(bx, by, desc.width);
    const float ax_m = static_cast<float>(kFlowDx[first_direction]) * desc.cell_size_m;
    const float ay_m = static_cast<float>(kFlowDy[first_direction]) * desc.cell_size_m;
    const float bx_m = static_cast<float>(kFlowDx[second_direction]) * desc.cell_size_m;
    const float by_m = static_cast<float>(kFlowDy[second_direction]) * desc.cell_size_m;
    const float det = (ax_m * by_m) - (bx_m * ay_m);
    if (std::abs(det) < 0.000001F) {
        const FlowCandidate a = single_flow_candidate(desc, height, x, y, first_direction);
        const FlowCandidate b = single_flow_candidate(desc, height, x, y, second_direction);
        return a.slope >= b.slope ? a : b;
    }

    const float da = height[receiver_a] - height[sample];
    const float db = height[receiver_b] - height[sample];
    const float gradient_x = ((da * by_m) - (db * ay_m)) / det;
    const float gradient_y = ((ax_m * db) - (bx_m * da)) / det;
    const float down_x = -gradient_x;
    const float down_y = -gradient_y;
    const float bary_a = ((down_x * by_m) - (bx_m * down_y)) / det;
    const float bary_b = ((ax_m * down_y) - (down_x * ay_m)) / det;
    const float plane_slope = std::sqrt((down_x * down_x) + (down_y * down_y));

    const bool inside_facet = bary_a >= -0.0001F && bary_b >= -0.0001F &&
                              plane_slope > 0.0F;
    if (!inside_facet) {
        const FlowCandidate a = single_flow_candidate(desc, height, x, y, first_direction);
        const FlowCandidate b = single_flow_candidate(desc, height, x, y, second_direction);
        return a.slope >= b.slope ? a : b;
    }

    const bool a_descends = height[receiver_a] < height[sample];
    const bool b_descends = height[receiver_b] < height[sample];
    if (!a_descends && !b_descends) {
        return {};
    }
    if (!a_descends) {
        return single_flow_candidate(desc, height, x, y, second_direction);
    }
    if (!b_descends) {
        return single_flow_candidate(desc, height, x, y, first_direction);
    }

    const float bary_sum = std::max(bary_a + bary_b, 0.000001F);
    const float weight_a = saturate(bary_a / bary_sum);
    const float weight_b = 1.0F - weight_a;
    FlowCandidate candidate;
    candidate.slope = plane_slope;
    candidate.receiver.first = receiver_a;
    candidate.receiver.second = receiver_b;
    candidate.receiver.first_weight = weight_a;
    candidate.receiver.second_weight = weight_b;
    candidate.receiver.first_direction = first_direction;
    candidate.receiver.second_direction = second_direction;
    if (weight_b > weight_a) {
        std::swap(candidate.receiver.first, candidate.receiver.second);
        std::swap(candidate.receiver.first_weight, candidate.receiver.second_weight);
        std::swap(candidate.receiver.first_direction, candidate.receiver.second_direction);
    }
    const float vector_x =
        (static_cast<float>(kFlowDx[candidate.receiver.first_direction]) *
         candidate.receiver.first_weight) +
        (static_cast<float>(kFlowDx[candidate.receiver.second_direction]) *
         candidate.receiver.second_weight);
    const float vector_y =
        (static_cast<float>(kFlowDy[candidate.receiver.first_direction]) *
         candidate.receiver.first_weight) +
        (static_cast<float>(kFlowDy[candidate.receiver.second_direction]) *
         candidate.receiver.second_weight);
    const float vector_length = std::sqrt((vector_x * vector_x) + (vector_y * vector_y));
    if (vector_length > 0.0F) {
        candidate.receiver.vector_x = vector_x / vector_length;
        candidate.receiver.vector_y = vector_y / vector_length;
    }
    return candidate;
}

[[nodiscard]] FlowRoutingData compute_flow_routing(const TerrainLabGridDesc& desc,
                                                   const std::vector<float>& height,
                                                   std::vector<std::uint8_t>& flow_direction) {
    const std::size_t count = terrain_lab_sample_count(desc);
    FlowRoutingData routing;
    routing.receivers.assign(count, {});
    flow_direction.assign(count, kFlowSinkDirection);

    for (std::uint32_t y = 0; y < desc.height; ++y) {
        for (std::uint32_t x = 0; x < desc.width; ++x) {
            FlowCandidate best;
            for (std::size_t facet = 0; facet < kFlowFacetRing.size(); ++facet) {
                const std::uint8_t first_direction = kFlowFacetRing[facet];
                const std::uint8_t second_direction =
                    kFlowFacetRing[(facet + 1U) % kFlowFacetRing.size()];
                const FlowCandidate candidate =
                    triangular_flow_candidate(desc, height, x, y, first_direction, second_direction);
                if (candidate.slope > best.slope) {
                    best = candidate;
                }
            }
            const std::size_t sample = grid_index(x, y, desc.width);
            routing.receivers[sample] = best.receiver;
            flow_direction[sample] = best.receiver.first_direction;
        }
    }

    return routing;
}

void compute_flow_fields(const TerrainLabGridDesc& desc, const std::vector<float>& height,
                         const std::vector<float>& slope, std::vector<std::uint8_t>& flow_direction,
                         std::vector<float>& flow_accumulation, std::vector<float>& stream_power,
                         float& max_flow_accumulation, float& max_stream_power,
                         FlowRoutingData* routing_output = nullptr) {
    const std::size_t count = terrain_lab_sample_count(desc);
    FlowRoutingData routing = compute_flow_routing(desc, height, flow_direction);
    flow_accumulation.assign(count, 1.0F);
    stream_power.assign(count, 0.0F);

    std::vector<std::size_t> order(count);
    std::iota(order.begin(), order.end(), 0U);
    std::sort(order.begin(), order.end(),
              [&height](std::size_t lhs, std::size_t rhs) { return height[lhs] > height[rhs]; });

    for (const std::size_t sample : order) {
        const FlowReceiver& receiver = routing.receivers[sample];
        if (receiver.first != kNoFlowReceiver && receiver.first_weight > 0.0F) {
            flow_accumulation[receiver.first] += flow_accumulation[sample] * receiver.first_weight;
        }
        if (receiver.second != kNoFlowReceiver && receiver.second_weight > 0.0F) {
            flow_accumulation[receiver.second] +=
                flow_accumulation[sample] * receiver.second_weight;
        }
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
    if (routing_output != nullptr) {
        *routing_output = std::move(routing);
    }
}

[[nodiscard]] float squared_distance_cells(std::size_t lhs, std::size_t rhs,
                                           const TerrainLabGridDesc& desc) {
    const float lx = static_cast<float>(lhs % desc.width);
    const float ly = static_cast<float>(lhs / desc.width);
    const float rx = static_cast<float>(rhs % desc.width);
    const float ry = static_cast<float>(rhs / desc.width);
    const float dx = lx - rx;
    const float dy = ly - ry;
    return (dx * dx) + (dy * dy);
}

[[nodiscard]] std::vector<std::size_t>
select_arid_trace_seeds(const TerrainLabGridDesc& desc, const std::vector<float>& network_source,
                        const std::vector<float>& incision_source) {
    std::vector<AridTraceSeedCandidate> candidates;
    const std::uint32_t margin = std::min<std::uint32_t>(
        4U, std::min(desc.width > 1U ? desc.width - 1U : 0U, desc.height > 1U ? desc.height - 1U : 0U));
    for (std::uint32_t y = margin; y + margin < desc.height; ++y) {
        for (std::uint32_t x = margin; x + margin < desc.width; ++x) {
            const std::size_t sample = grid_index(x, y, desc.width);
            const float source = network_source[sample];
            if (source < 0.26F) {
                continue;
            }
            bool local_peak = true;
            for (std::uint8_t direction = 0U; direction < kFlowSinkDirection; ++direction) {
                std::uint32_t nx = 0;
                std::uint32_t ny = 0;
                if (!flow_neighbor(desc, x, y, direction, nx, ny)) {
                    continue;
                }
                if (network_source[grid_index(nx, ny, desc.width)] > source + 0.018F) {
                    local_peak = false;
                    break;
                }
            }
            if (!local_peak && source < 0.46F) {
                continue;
            }
            candidates.push_back({
                .sample = sample,
                .score = source * 0.80F + incision_source[sample] * 0.20F,
            });
        }
    }

    if (candidates.empty()) {
        for (std::size_t sample = 0; sample < network_source.size(); ++sample) {
            if (network_source[sample] > 0.18F) {
                candidates.push_back({
                    .sample = sample,
                    .score = network_source[sample] * 0.80F + incision_source[sample] * 0.20F,
                });
            }
        }
    }

    std::sort(candidates.begin(), candidates.end(),
              [](const AridTraceSeedCandidate& lhs, const AridTraceSeedCandidate& rhs) {
                  return lhs.score > rhs.score;
              });

    std::vector<std::size_t> seeds;
    const float min_distance = static_cast<float>(
        std::max<std::uint32_t>(10U, std::min(desc.width, desc.height) / 18U));
    const float min_distance_sq = min_distance * min_distance;
    constexpr std::size_t kMaxAridTraceSeeds = 28U;
    for (const AridTraceSeedCandidate& candidate : candidates) {
        bool too_close = false;
        for (const std::size_t seed : seeds) {
            if (squared_distance_cells(candidate.sample, seed, desc) < min_distance_sq) {
                too_close = true;
                break;
            }
        }
        if (too_close) {
            continue;
        }
        seeds.push_back(candidate.sample);
        if (seeds.size() >= kMaxAridTraceSeeds) {
            break;
        }
    }
    return seeds;
}

void merge_stream_order(std::uint8_t incoming_order, std::size_t receiver,
                        std::vector<std::uint8_t>& max_order,
                        std::vector<std::uint8_t>& equal_order_count) {
    if (receiver == kNoFlowReceiver || incoming_order == 0U) {
        return;
    }
    if (incoming_order > max_order[receiver]) {
        max_order[receiver] = incoming_order;
        equal_order_count[receiver] = 1U;
        return;
    }
    if (incoming_order == max_order[receiver] &&
        equal_order_count[receiver] < std::numeric_limits<std::uint8_t>::max()) {
        ++equal_order_count[receiver];
    }
}

[[nodiscard]] std::vector<float> make_river_routing_height(const TerrainLabFieldData& fields) {
    std::vector<float> routing_height(fields.sample_count(), 0.0F);
    constexpr float kBaseLevelGrade = 0.140F;
    for (std::uint32_t y = 0; y < fields.desc.height; ++y) {
        const float downstream_z_m = terrain_lab_grid_sample_z_m(fields.desc, y);
        for (std::uint32_t x = 0; x < fields.desc.width; ++x) {
            const std::size_t sample = fields.index(x, y);
            const float macro_height = fields.structure_height_m[sample] + fields.process_delta_m[sample];
            const float base_height =
                std::isfinite(macro_height) ? macro_height : fields.height_m[sample];
            routing_height[sample] = base_height - downstream_z_m * kBaseLevelGrade;
        }
    }
    return routing_height;
}

[[nodiscard]] float river_stream_score(const TerrainLabFieldData& fields,
                                       std::size_t sample,
                                       float discharge_t,
                                       float stream_t) {
    return saturate(discharge_t * 0.70F + fields.channel_influence[sample] * 0.34F +
                    stream_t * 0.16F - fields.divide_influence[sample] * 0.12F);
}

[[nodiscard]] std::vector<float>
derive_connected_river_activation(const TerrainLabFieldData& fields,
                                  const FlowRoutingData& routing,
                                  RiverDerivationParams params,
                                  const std::vector<float>& stream_score,
                                  const std::vector<float>& discharge_t,
                                  const std::vector<float>& order_t) {
    const std::size_t count = fields.sample_count();
    std::vector<float> activation(count, 0.0F);
    const std::uint32_t max_steps = std::max<std::uint32_t>(
        1U, std::min<std::uint32_t>(fields.desc.width + fields.desc.height, 768U));

    for (std::size_t seed = 0; seed < count; ++seed) {
        const float strong_score =
            smoothstep(params.stream_threshold + 0.12F, params.stream_threshold + 0.32F,
                       stream_score[seed]);
        const float discharge_seed =
            smoothstep(0.52F, 0.78F, discharge_t[seed]) * (0.42F + order_t[seed] * 0.44F);
        const float ordered_seed =
            fields.stream_order[seed] >= 2U ? (0.18F + order_t[seed] * 0.42F) : 0.0F;
        float carry = std::max({strong_score, discharge_seed, ordered_seed});
        if (carry <= 0.08F) {
            continue;
        }

        std::size_t current = seed;
        for (std::uint32_t step = 0; step < max_steps && current != kNoFlowReceiver; ++step) {
            activation[current] = std::max(activation[current], carry);

            const FlowReceiver& receiver = routing.receivers[current];
            if (receiver.first == kNoFlowReceiver || receiver.first_weight <= 0.0F ||
                receiver.first == current) {
                break;
            }

            const std::size_t next = receiver.first;
            const float weak_score =
                smoothstep(params.stream_threshold - 0.16F, params.stream_threshold + 0.10F,
                           stream_score[next]);
            const float ordered =
                fields.stream_order[next] >= 2U ? (0.24F + order_t[next] * 0.36F) : 0.0F;
            const float downstream_discharge = smoothstep(0.34F, 0.68F, discharge_t[next]) * 0.72F;
            carry = std::max({carry * 0.992F, weak_score * 0.82F, ordered, downstream_discharge});
            if (carry <= 0.035F && stream_score[next] < params.stream_threshold - 0.18F) {
                break;
            }
            current = next;
        }
    }

    return activation;
}

[[nodiscard]] std::vector<std::vector<std::size_t>>
build_upstream_contributors(const FlowRoutingData& routing, std::size_t count) {
    std::vector<std::vector<std::size_t>> upstream(count);
    for (std::size_t sample = 0; sample < count; ++sample) {
        const FlowReceiver& receiver = routing.receivers[sample];
        if (receiver.first != kNoFlowReceiver && receiver.first < count &&
            receiver.first_weight > 0.0F && receiver.first != sample) {
            upstream[receiver.first].push_back(sample);
        }
        if (receiver.second != kNoFlowReceiver && receiver.second < count &&
            receiver.second != receiver.first && receiver.second_weight > 0.28F &&
            receiver.second != sample) {
            upstream[receiver.second].push_back(sample);
        }
    }
    return upstream;
}

[[nodiscard]] float river_skeleton_score(const std::vector<float>& stream_score,
                                         const std::vector<float>& discharge_t,
                                         const std::vector<float>& order_t,
                                         std::size_t sample) {
    return saturate(discharge_t[sample] * 0.66F + stream_score[sample] * 0.22F +
                    order_t[sample] * 0.30F);
}

[[nodiscard]] std::vector<std::size_t> trace_best_upstream_path(
    const std::vector<std::vector<std::size_t>>& upstream,
    const std::vector<float>& stream_score,
    const std::vector<float>& discharge_t,
    const std::vector<float>& order_t,
    std::size_t start,
    float min_score,
    std::uint32_t max_steps,
    const std::vector<std::uint8_t>* blocked = nullptr) {
    std::vector<std::size_t> path;
    if (start == kNoFlowReceiver || start >= upstream.size()) {
        return path;
    }

    std::vector<std::uint8_t> visited(upstream.size(), 0U);
    std::size_t current = start;
    for (std::uint32_t step = 0; step < max_steps && current != kNoFlowReceiver; ++step) {
        path.push_back(current);
        visited[current] = 1U;

        std::size_t best = kNoFlowReceiver;
        float best_score = -1.0F;
        for (const std::size_t donor : upstream[current]) {
            if (donor >= upstream.size() || visited[donor] != 0U) {
                continue;
            }
            if (blocked != nullptr && (*blocked)[donor] != 0U) {
                continue;
            }
            const float score = river_skeleton_score(stream_score, discharge_t, order_t, donor);
            if (score > best_score) {
                best_score = score;
                best = donor;
            }
        }

        if (best == kNoFlowReceiver || best_score < min_score) {
            break;
        }
        current = best;
    }

    return path;
}

[[nodiscard]] std::vector<std::size_t> trace_downstream_path(const FlowRoutingData& routing,
                                                             std::size_t start,
                                                             std::uint32_t max_steps) {
    std::vector<std::size_t> path;
    if (start == kNoFlowReceiver || start >= routing.receivers.size()) {
        return path;
    }

    std::vector<std::uint8_t> visited(routing.receivers.size(), 0U);
    std::size_t current = start;
    for (std::uint32_t step = 0; step < max_steps && current != kNoFlowReceiver; ++step) {
        path.push_back(current);
        visited[current] = 1U;

        const FlowReceiver& receiver = routing.receivers[current];
        if (receiver.first == kNoFlowReceiver || receiver.first_weight <= 0.0F ||
            receiver.first == current || visited[receiver.first] != 0U) {
            break;
        }
        current = receiver.first;
    }

    return path;
}

[[nodiscard]] std::vector<std::size_t> select_primary_downstream_trunk(
    const TerrainLabGridDesc& desc,
    const FlowRoutingData& routing,
    const std::vector<float>& stream_score,
    const std::vector<float>& discharge_t,
    const std::vector<float>& order_t) {
    std::vector<RiverBranchCandidate> candidates;
    const float inv_height =
        desc.height <= 1U ? 0.0F : 1.0F / static_cast<float>(desc.height - 1U);
    const std::uint32_t edge_margin =
        std::max<std::uint32_t>(3U, std::min(desc.width, desc.height) / 18U);
    for (std::size_t sample = 0; sample < discharge_t.size(); ++sample) {
        const auto x = static_cast<std::uint32_t>(sample % desc.width);
        const auto y = static_cast<std::uint32_t>(sample / desc.width);
        if (x < edge_margin || x + edge_margin >= desc.width) {
            continue;
        }
        const float upstream_t = 1.0F - static_cast<float>(y) * inv_height;
        const float score = river_skeleton_score(stream_score, discharge_t, order_t, sample) +
                            upstream_t * 0.55F;
        if (score < 0.16F) {
            continue;
        }
        candidates.push_back({
            .sample = sample,
            .score = score,
        });
    }

    if (candidates.empty()) {
        for (std::size_t sample = 0; sample < discharge_t.size(); ++sample) {
            candidates.push_back({
                .sample = sample,
                .score = river_skeleton_score(stream_score, discharge_t, order_t, sample),
            });
        }
    }

    std::sort(candidates.begin(), candidates.end(),
              [](const RiverBranchCandidate& lhs, const RiverBranchCandidate& rhs) {
                  return lhs.score > rhs.score;
              });
    if (candidates.size() > 2048U) {
        candidates.resize(2048U);
    }

    const std::uint32_t max_steps =
        std::max<std::uint32_t>(1U, std::min<std::uint32_t>(desc.width + desc.height, 768U));
    const float inv_min_extent =
        1.0F / static_cast<float>(std::max<std::uint32_t>(1U, std::min(desc.width, desc.height)));
    std::vector<std::size_t> best_path;
    float best_score = -1.0F;
    for (const RiverBranchCandidate& candidate : candidates) {
        std::vector<std::size_t> path = trace_downstream_path(routing, candidate.sample, max_steps);
        if (path.size() < 8U) {
            continue;
        }
        const std::size_t outlet = path.back();
        std::size_t edge_count = 0;
        for (const std::size_t sample : path) {
            const auto x = static_cast<std::uint32_t>(sample % desc.width);
            const auto y = static_cast<std::uint32_t>(sample / desc.width);
            if (x < edge_margin || x + edge_margin >= desc.width ||
                (y < edge_margin && sample != path.front())) {
                ++edge_count;
            }
        }
        const float edge_t = static_cast<float>(edge_count) / static_cast<float>(path.size());
        const float length_t = saturate(static_cast<float>(path.size()) * inv_min_extent);
        const float score = length_t * 1.20F + discharge_t[outlet] * 0.48F +
                            order_t[outlet] * 0.24F + candidate.score * 0.12F -
                            edge_t * 0.90F;
        if (score > best_score) {
            best_score = score;
            best_path = std::move(path);
        }
    }

    std::reverse(best_path.begin(), best_path.end());
    return best_path;
}

void paint_river_path(const std::vector<std::size_t>& path,
                      std::vector<float>& activation,
                      float base_strength,
                      float headwater_scale) {
    if (path.empty()) {
        return;
    }
    const float denominator = path.size() <= 1U ? 1.0F : static_cast<float>(path.size() - 1U);
    for (std::size_t index = 0; index < path.size(); ++index) {
        const float t = static_cast<float>(index) / denominator;
        const float strength = base_strength * lerp(1.0F, headwater_scale, t);
        activation[path[index]] = std::max(activation[path[index]], saturate(strength));
    }
}

[[nodiscard]] std::vector<float> derive_trunk_river_activation(
    const TerrainLabFieldData& fields,
    const FlowRoutingData& routing,
    const std::vector<float>& stream_score,
    const std::vector<float>& discharge_t,
    const std::vector<float>& order_t) {
    const std::size_t count = fields.sample_count();
    std::vector<float> activation(count, 0.0F);
    const std::vector<std::vector<std::size_t>> upstream =
        build_upstream_contributors(routing, count);
    std::vector<std::size_t> main_path =
        select_primary_downstream_trunk(fields.desc, routing, stream_score, discharge_t, order_t);
    const std::size_t min_main_length =
        std::max<std::size_t>(
            16U,
            static_cast<std::size_t>(std::min(fields.desc.width, fields.desc.height)) / 5U);
    if (main_path.size() < min_main_length) {
        return activation;
    }

    paint_river_path(main_path, activation, 1.0F, 0.88F);

    std::vector<std::uint8_t> on_trunk(count, 0U);
    for (const std::size_t sample : main_path) {
        on_trunk[sample] = 1U;
    }

    std::vector<RiverBranchCandidate> branch_candidates;
    const std::size_t branch_start_margin =
        std::max<std::size_t>(2U, main_path.size() / 12U);
    for (std::size_t anchor_index = branch_start_margin;
         anchor_index + branch_start_margin < main_path.size(); ++anchor_index) {
        const std::size_t anchor = main_path[anchor_index];
        for (const std::size_t donor : upstream[anchor]) {
            if (donor >= count || on_trunk[donor] != 0U) {
                continue;
            }
            const float score = river_skeleton_score(stream_score, discharge_t, order_t, donor);
            if (score < 0.48F) {
                continue;
            }
            branch_candidates.push_back({
                .sample = donor,
                .anchor_index = anchor_index,
                .score = score,
            });
        }
    }

    std::sort(branch_candidates.begin(), branch_candidates.end(),
              [](const RiverBranchCandidate& lhs, const RiverBranchCandidate& rhs) {
                  return lhs.score > rhs.score;
              });

    const std::size_t max_branches = std::max<std::size_t>(
        2U, static_cast<std::size_t>(std::min(fields.desc.width, fields.desc.height)) / 64U +
                1U);
    const std::size_t min_anchor_spacing = std::max<std::size_t>(8U, main_path.size() / 5U);
    const std::size_t min_branch_length =
        std::max<std::size_t>(
            8U,
            static_cast<std::size_t>(std::min(fields.desc.width, fields.desc.height)) / 14U);
    const std::uint32_t max_branch_steps =
        std::max<std::uint32_t>(
            8U, std::min<std::uint32_t>((fields.desc.width + fields.desc.height) / 3U, 256U));
    std::vector<std::size_t> accepted_anchors;
    std::size_t accepted_branch_count = 0;
    for (const RiverBranchCandidate& candidate : branch_candidates) {
        bool too_close = false;
        for (const std::size_t anchor : accepted_anchors) {
            const std::size_t distance =
                candidate.anchor_index > anchor ? candidate.anchor_index - anchor
                                                : anchor - candidate.anchor_index;
            if (distance < min_anchor_spacing) {
                too_close = true;
                break;
            }
        }
        if (too_close) {
            continue;
        }

        std::vector<std::size_t> branch_path =
            trace_best_upstream_path(upstream, stream_score, discharge_t, order_t,
                                     candidate.sample, 0.34F, max_branch_steps, &on_trunk);
        if (branch_path.size() < min_branch_length) {
            continue;
        }

        const float branch_strength = lerp(0.48F, 0.66F, candidate.score);
        paint_river_path(branch_path, activation, branch_strength, 0.62F);
        accepted_anchors.push_back(candidate.anchor_index);
        ++accepted_branch_count;
        if (accepted_branch_count >= max_branches) {
            break;
        }
    }

    return activation;
}

void prune_short_river_fragments(const TerrainLabGridDesc& desc, std::vector<float>& activation) {
    const std::size_t count = terrain_lab_sample_count(desc);
    std::vector<std::uint8_t> visited(count, 0U);
    std::vector<std::size_t> stack;
    std::vector<std::size_t> component;
    const std::size_t min_component_size =
        std::max<std::size_t>(24U, static_cast<std::size_t>(std::min(desc.width, desc.height)) / 4U);

    for (std::size_t start = 0; start < count; ++start) {
        if (visited[start] != 0U || activation[start] <= 0.12F) {
            continue;
        }

        stack.clear();
        component.clear();
        stack.push_back(start);
        visited[start] = 1U;
        bool touches_edge = false;

        while (!stack.empty()) {
            const std::size_t sample = stack.back();
            stack.pop_back();
            component.push_back(sample);

            const auto x = static_cast<std::uint32_t>(sample % desc.width);
            const auto y = static_cast<std::uint32_t>(sample / desc.width);
            touches_edge = touches_edge || x == 0U || y == 0U || x + 1U == desc.width ||
                           y + 1U == desc.height;

            for (std::uint8_t direction = 0U; direction < kFlowSinkDirection; ++direction) {
                std::uint32_t nx = 0;
                std::uint32_t ny = 0;
                if (!flow_neighbor(desc, x, y, direction, nx, ny)) {
                    continue;
                }
                const std::size_t neighbor = grid_index(nx, ny, desc.width);
                if (visited[neighbor] != 0U || activation[neighbor] <= 0.12F) {
                    continue;
                }
                visited[neighbor] = 1U;
                stack.push_back(neighbor);
            }
        }

        const bool keep = component.size() >= min_component_size ||
                          (touches_edge && component.size() >= min_component_size / 2U);
        if (!keep) {
            for (const std::size_t sample : component) {
                activation[sample] = 0.0F;
            }
        }
    }

    for (float& value : activation) {
        if (value < 0.12F) {
            value = 0.0F;
        }
    }
}

void widen_connected_river_activation(const TerrainLabGridDesc& desc, std::vector<float>& activation) {
    for (std::uint32_t iteration = 0; iteration < 2U; ++iteration) {
        std::vector<float> next = activation;
        const float neighbor_scale = iteration == 0U ? 0.62F : 0.38F;
        for (std::uint32_t y = 0; y < desc.height; ++y) {
            for (std::uint32_t x = 0; x < desc.width; ++x) {
                const std::size_t sample = grid_index(x, y, desc.width);
                for (std::uint8_t direction = 0U; direction < kFlowSinkDirection; ++direction) {
                    std::uint32_t nx = 0;
                    std::uint32_t ny = 0;
                    if (!flow_neighbor(desc, x, y, direction, nx, ny)) {
                        continue;
                    }
                    const std::size_t neighbor = grid_index(nx, ny, desc.width);
                    next[sample] =
                        std::max(next[sample], activation[neighbor] * neighbor_scale);
                }
            }
        }
        activation.swap(next);
    }
}

void prune_tiny_visible_river_fragments(const TerrainLabGridDesc& desc,
                                        std::vector<float>& activation) {
    const std::size_t count = terrain_lab_sample_count(desc);
    std::vector<std::uint8_t> visited(count, 0U);
    std::vector<std::size_t> stack;
    std::vector<std::size_t> component;

    for (std::size_t start = 0; start < count; ++start) {
        if (visited[start] != 0U || activation[start] <= 0.08F) {
            continue;
        }

        stack.clear();
        component.clear();
        stack.push_back(start);
        visited[start] = 1U;
        while (!stack.empty()) {
            const std::size_t sample = stack.back();
            stack.pop_back();
            component.push_back(sample);

            const auto x = static_cast<std::uint32_t>(sample % desc.width);
            const auto y = static_cast<std::uint32_t>(sample / desc.width);
            for (std::uint8_t direction = 0U; direction < kFlowSinkDirection; ++direction) {
                std::uint32_t nx = 0;
                std::uint32_t ny = 0;
                if (!flow_neighbor(desc, x, y, direction, nx, ny)) {
                    continue;
                }
                const std::size_t neighbor = grid_index(nx, ny, desc.width);
                if (visited[neighbor] != 0U || activation[neighbor] <= 0.08F) {
                    continue;
                }
                visited[neighbor] = 1U;
                stack.push_back(neighbor);
            }
        }

        if (component.size() <= 6U) {
            for (const std::size_t sample : component) {
                activation[sample] = 0.0F;
            }
        }
    }
}

void derive_river_network_fields(TerrainLabFieldData& fields, RiverDerivationParams params) {
    const std::size_t count = fields.sample_count();
    fields.river_discharge.assign(count, 0.0F);
    fields.stream_order.assign(count, 0U);
    fields.river_width_m.assign(count, 0.0F);
    fields.valley_width_m.assign(count, 0.0F);
    fields.water_presence.assign(count, 0.0F);
    fields.max_river_discharge = 0.0F;
    fields.max_stream_order = 0U;
    fields.max_river_width_m = 0.0F;
    fields.max_valley_width_m = 0.0F;
    if (count == 0U) {
        return;
    }

    std::vector<std::uint8_t> river_flow_direction;
    const std::vector<float> river_routing_height = make_river_routing_height(fields);
    FlowRoutingData routing =
        compute_flow_routing(fields.desc, river_routing_height, river_flow_direction);
    for (std::size_t sample = 0; sample < count; ++sample) {
        const float local_runoff =
            saturate(0.36F + fields.channel_influence[sample] * 0.28F +
                     fields.valley_influence[sample] * 0.18F +
                     fields.basin_influence[sample] * 0.12F +
                     fields.driver_process_potential[sample] * 0.10F -
                     fields.divide_influence[sample] * 0.12F);
        fields.river_discharge[sample] = std::max(0.01F, local_runoff * params.runoff_scale);
    }

    std::vector<std::size_t> order(count);
    std::iota(order.begin(), order.end(), 0U);
    std::sort(order.begin(), order.end(),
              [&river_routing_height](std::size_t lhs, std::size_t rhs) {
                  return river_routing_height[lhs] > river_routing_height[rhs];
              });

    for (const std::size_t sample : order) {
        const FlowReceiver& receiver = routing.receivers[sample];
        if (receiver.first != kNoFlowReceiver && receiver.first_weight > 0.0F) {
            fields.river_discharge[receiver.first] +=
                fields.river_discharge[sample] * receiver.first_weight;
        }
        if (receiver.second != kNoFlowReceiver && receiver.second_weight > 0.0F) {
            fields.river_discharge[receiver.second] +=
                fields.river_discharge[sample] * receiver.second_weight;
        }
    }

    for (const float discharge : fields.river_discharge) {
        fields.max_river_discharge = std::max(fields.max_river_discharge, discharge);
    }
    const float inv_log_max_discharge =
        fields.max_river_discharge <= 0.0F ? 0.0F : 1.0F / std::log1p(fields.max_river_discharge);
    const float inv_max_stream =
        fields.max_stream_power <= 0.0F ? 0.0F : 1.0F / fields.max_stream_power;

    std::vector<std::uint8_t> max_upstream_order(count, 0U);
    std::vector<std::uint8_t> equal_upstream_order_count(count, 0U);
    std::vector<float> discharge_t_values(count, 0.0F);
    std::vector<float> stream_score_values(count, 0.0F);
    for (const std::size_t sample : order) {
        const float discharge_t = std::log1p(fields.river_discharge[sample]) * inv_log_max_discharge;
        const float stream_t = saturate(fields.stream_power[sample] * inv_max_stream);
        const float stream_score =
            river_stream_score(fields, sample, discharge_t, stream_t);
        discharge_t_values[sample] = discharge_t;
        stream_score_values[sample] = stream_score;
        std::uint8_t sample_order = max_upstream_order[sample];
        if (sample_order > 0U && equal_upstream_order_count[sample] >= 2U) {
            sample_order = static_cast<std::uint8_t>(std::min<std::uint32_t>(
                static_cast<std::uint32_t>(sample_order) + 1U,
                static_cast<std::uint32_t>(std::numeric_limits<std::uint8_t>::max())));
        }
        if (sample_order == 0U && stream_score > params.stream_threshold) {
            sample_order = 1U;
        }
        fields.stream_order[sample] = sample_order;
        fields.max_stream_order =
            std::max(fields.max_stream_order, static_cast<std::uint32_t>(sample_order));

        const FlowReceiver& receiver = routing.receivers[sample];
        merge_stream_order(sample_order, receiver.first, max_upstream_order,
                           equal_upstream_order_count);
    }

    const float stream_order_denominator =
        fields.max_stream_order <= 1U ? 1.0F : static_cast<float>(fields.max_stream_order);
    std::vector<float> order_t_values(count, 0.0F);
    for (std::size_t sample = 0; sample < count; ++sample) {
        order_t_values[sample] =
            static_cast<float>(fields.stream_order[sample]) / stream_order_denominator;
    }
    std::vector<float> river_activation =
        params.extract_visible_trunks
            ? derive_trunk_river_activation(fields, routing, stream_score_values, discharge_t_values,
                                            order_t_values)
            : derive_connected_river_activation(fields, routing, params, stream_score_values,
                                                discharge_t_values, order_t_values);
    if (params.prune_disconnected_fragments) {
        prune_short_river_fragments(fields.desc, river_activation);
        widen_connected_river_activation(fields.desc, river_activation);
        prune_tiny_visible_river_fragments(fields.desc, river_activation);
    }
    for (std::size_t sample = 0; sample < count; ++sample) {
        const float discharge_t = discharge_t_values[sample];
        const float order_t = order_t_values[sample];
        const float stream_t = saturate(fields.stream_power[sample] * inv_max_stream);
        const float stream_score =
            river_stream_score(fields, sample, discharge_t, stream_t);
        const float local_active =
            smoothstep(params.stream_threshold - 0.08F, params.stream_threshold + 0.18F,
                       stream_score);
        const float active_stream =
            saturate(std::max(river_activation[sample], local_active * params.local_active_scale));
        const float slope_t = smoothstep(0.030F, 0.42F, fields.slope[sample]);
        const float hierarchy_t = saturate(discharge_t * 0.66F + order_t * 0.34F);
        const float river_width =
            active_stream * lerp(params.min_width_m, params.max_width_m, hierarchy_t);
        const float valley_width =
            river_width * lerp(params.valley_width_multiplier * 0.72F,
                               params.valley_width_multiplier * 1.45F, 1.0F - slope_t);
        const float water =
            saturate(params.water_presence_scale * active_stream *
                     (discharge_t * 0.72F + order_t * 0.28F) * (1.0F - slope_t * 0.28F));

        fields.river_width_m[sample] = river_width;
        fields.valley_width_m[sample] = valley_width;
        fields.water_presence[sample] = water;
        fields.max_river_width_m = std::max(fields.max_river_width_m, river_width);
        fields.max_valley_width_m = std::max(fields.max_valley_width_m, valley_width);
    }
}

[[nodiscard]] RiverDerivationParams arid_river_derivation_params(const TerrainLabGridDesc& desc) {
    return {
        .runoff_scale = 0.42F,
        .water_presence_scale = 0.0F,
        .min_width_m = desc.cell_size_m * 0.28F,
        .max_width_m = desc.cell_size_m * 2.45F,
        .valley_width_multiplier = 7.2F,
        .stream_threshold = 0.30F,
    };
}

void apply_arid_river_hierarchy_carving(const TerrainLabConfig& config, TerrainLabFieldData& fields,
                                        float arid_elevation_scale_m) {
    if (fields.max_river_width_m <= 0.0F || fields.max_valley_width_m <= 0.0F) {
        return;
    }
    const float stream_order_denominator =
        fields.max_stream_order <= 1U ? 1.0F : static_cast<float>(fields.max_stream_order);
    for (std::size_t sample = 0; sample < fields.sample_count(); ++sample) {
        const float width_t = saturate(fields.river_width_m[sample] / fields.max_river_width_m);
        const float valley_width_t =
            saturate(fields.valley_width_m[sample] / fields.max_valley_width_m);
        const float order_t =
            static_cast<float>(fields.stream_order[sample]) / stream_order_denominator;
        const float dry_network =
            saturate(fields.channel_influence[sample] * 0.54F +
                     smoothstep(0.05F, 0.46F, width_t) * 0.42F + order_t * 0.18F);
        if (dry_network <= 0.0F) {
            continue;
        }

        const float slope_t = smoothstep(0.05F, 0.52F, fields.slope[sample]);
        const float width_cut =
            dry_network * smoothstep(0.06F, 0.64F, width_t) * (1.0F - slope_t * 0.24F) *
            arid_elevation_scale_m * lerp(0.012F, 0.052F, width_t) * config.process_strength;
        fields.process_delta_m[sample] -= width_cut;
        fields.height_m[sample] -= width_cut;

        const float wall_distance_m = fields.channel_distance_m[sample];
        const float wall_width_m =
            std::max(fields.desc.cell_size_m * 1.2F, fields.river_width_m[sample] * 2.8F);
        const float wall_band =
            smoothstep(wall_width_m * 0.70F, wall_width_m * 1.82F, wall_distance_m) *
            (1.0F - smoothstep(wall_width_m * 1.92F, wall_width_m * 4.60F, wall_distance_m)) *
            smoothstep(0.16F, 0.76F, dry_network);

        fields.channel_influence[sample] =
            saturate(std::max(fields.channel_influence[sample], dry_network * 0.86F));
        fields.valley_influence[sample] =
            saturate(std::max(fields.valley_influence[sample],
                              dry_network * 0.62F + valley_width_t * 0.16F));
        fields.ridge_influence[sample] =
            saturate(std::max(fields.ridge_influence[sample], wall_band * (0.48F + order_t * 0.34F)));
        fields.divide_influence[sample] =
            saturate(std::max(fields.divide_influence[sample], wall_band * 0.26F));
    }
}

[[nodiscard]] AridDrainageTrace
trace_arid_drainage_corridor(const TerrainLabGridDesc& desc, const FlowRoutingData& routing,
                             const std::vector<float>& network_source,
                             const std::vector<float>& incision_source, std::uint64_t seed,
                             std::size_t start) {
    AridDrainageTrace trace;
    std::vector<std::size_t> visited;
    std::size_t current = start;
    const std::uint32_t max_steps = std::min<std::uint32_t>(360U, desc.width + desc.height);
    float strength_sum = 0.0F;

    for (std::uint32_t step = 0; step < max_steps; ++step) {
        if (current == kNoFlowReceiver ||
            std::find(visited.begin(), visited.end(), current) != visited.end()) {
            break;
        }
        visited.push_back(current);

        const auto x = static_cast<std::uint32_t>(current % desc.width);
        const auto y = static_cast<std::uint32_t>(current / desc.width);
        const FlowReceiver& receiver = routing.receivers[current];
        const float strength = network_source[current];
        const float incision = incision_source[current];
        const float rough =
            fbm((static_cast<float>(x) * 0.037F) + 31.0F,
                (static_cast<float>(y) * 0.037F) - 17.0F, seed + 4901U, 3);
        const float lateral = rough * 0.46F * smoothstep(0.24F, 0.78F, strength);
        const float px = static_cast<float>(x) - receiver.vector_y * lateral;
        const float py = static_cast<float>(y) + receiver.vector_x * lateral;
        trace.points.push_back({
            .x = px,
            .y = py,
            .strength = strength,
            .incision = incision,
        });
        strength_sum += strength;

        if (receiver.first == kNoFlowReceiver) {
            break;
        }
        const std::size_t next = receiver.first;
        if (step > 6U && network_source[next] < 0.13F && strength < 0.36F) {
            break;
        }
        current = next;
    }

    if (!trace.points.empty()) {
        trace.strength = strength_sum / static_cast<float>(trace.points.size());
    }
    return trace;
}

void smooth_arid_drainage_trace(AridDrainageTrace& trace) {
    if (trace.points.size() < 4U) {
        return;
    }
    for (std::uint32_t iteration = 0; iteration < 3U; ++iteration) {
        std::vector<AridDrainageTracePoint> next = trace.points;
        for (std::size_t index = 1; index + 1U < trace.points.size(); ++index) {
            const AridDrainageTracePoint& prev = trace.points[index - 1U];
            const AridDrainageTracePoint& cur = trace.points[index];
            const AridDrainageTracePoint& post = trace.points[index + 1U];
            next[index].x = prev.x * 0.23F + cur.x * 0.54F + post.x * 0.23F;
            next[index].y = prev.y * 0.23F + cur.y * 0.54F + post.y * 0.23F;
            next[index].strength =
                saturate(prev.strength * 0.18F + cur.strength * 0.64F + post.strength * 0.18F);
            next[index].incision =
                saturate(prev.incision * 0.18F + cur.incision * 0.64F + post.incision * 0.18F);
        }
        trace.points.swap(next);
    }
}

[[nodiscard]] float point_segment_distance_cells(float px, float py, const AridDrainageTracePoint& a,
                                                 const AridDrainageTracePoint& b,
                                                 float& segment_t) {
    const float abx = b.x - a.x;
    const float aby = b.y - a.y;
    const float apx = px - a.x;
    const float apy = py - a.y;
    const float ab_len_sq = (abx * abx) + (aby * aby);
    segment_t = ab_len_sq <= 0.000001F ? 0.0F : saturate(((apx * abx) + (apy * aby)) / ab_len_sq);
    const float cx = a.x + abx * segment_t;
    const float cy = a.y + aby * segment_t;
    const float dx = px - cx;
    const float dy = py - cy;
    return std::sqrt((dx * dx) + (dy * dy));
}

[[nodiscard]] std::vector<AridDrainageTrace>
trace_arid_drainage_corridors(const TerrainLabGridDesc& desc, const FlowRoutingData& routing,
                              const std::vector<float>& network_source,
                              const std::vector<float>& incision_source, std::uint64_t seed) {
    std::vector<AridDrainageTrace> traces;
    std::vector<std::uint8_t> covered(terrain_lab_sample_count(desc), 0U);
    const std::vector<std::size_t> seeds =
        select_arid_trace_seeds(desc, network_source, incision_source);
    for (const std::size_t start : seeds) {
        if (covered[start] > 1U && network_source[start] < 0.58F) {
            continue;
        }
        AridDrainageTrace trace =
            trace_arid_drainage_corridor(desc, routing, network_source, incision_source, seed, start);
        if (trace.points.size() < 7U || trace.strength < 0.20F) {
            continue;
        }
        smooth_arid_drainage_trace(trace);
        std::size_t covered_steps = 0;
        for (const AridDrainageTracePoint& point : trace.points) {
            const auto x = static_cast<std::int32_t>(std::round(point.x));
            const auto y = static_cast<std::int32_t>(std::round(point.y));
            if (x < 0 || y < 0 || x >= static_cast<std::int32_t>(desc.width) ||
                y >= static_cast<std::int32_t>(desc.height)) {
                continue;
            }
            if (covered[grid_index(static_cast<std::uint32_t>(x),
                                   static_cast<std::uint32_t>(y), desc.width)] > 0U) {
                ++covered_steps;
            }
        }
        if (covered_steps + 4U >= trace.points.size() && trace.strength < 0.62F) {
            continue;
        }
        for (const AridDrainageTracePoint& point : trace.points) {
            const auto x = static_cast<std::int32_t>(std::round(point.x));
            const auto y = static_cast<std::int32_t>(std::round(point.y));
            if (x < 0 || y < 0 || x >= static_cast<std::int32_t>(desc.width) ||
                y >= static_cast<std::int32_t>(desc.height)) {
                continue;
            }
            std::uint8_t& value =
                covered[grid_index(static_cast<std::uint32_t>(x),
                                   static_cast<std::uint32_t>(y), desc.width)];
            value = static_cast<std::uint8_t>(std::min<std::uint32_t>(value + 1U, 255U));
        }
        traces.push_back(std::move(trace));
    }
    return traces;
}

[[nodiscard]] bool rasterize_arid_drainage_corridors(const TerrainLabGridDesc& desc,
                                                     const std::vector<AridDrainageTrace>& traces,
                                                     std::vector<float>& wash,
                                                     std::vector<float>& incision,
                                                     std::vector<float>& channel_distance_m) {
    if (traces.empty()) {
        return false;
    }

    const std::size_t count = terrain_lab_sample_count(desc);
    const float fallback_distance = std::max(half_extent_x_m(desc), half_extent_z_m(desc)) * 2.0F;
    wash.assign(count, 0.0F);
    incision.assign(count, 0.0F);
    channel_distance_m.assign(count, fallback_distance);

    for (const AridDrainageTrace& trace : traces) {
        if (trace.points.size() < 2U) {
            continue;
        }
        for (std::size_t index = 0; index + 1U < trace.points.size(); ++index) {
            const AridDrainageTracePoint& a = trace.points[index];
            const AridDrainageTracePoint& b = trace.points[index + 1U];
            const float segment_strength = saturate(std::max(a.strength, b.strength) * 0.78F +
                                                    trace.strength * 0.22F);
            const float segment_incision = saturate(std::max(a.incision, b.incision) * 0.82F +
                                                    segment_strength * 0.18F);
            const float segment_core_width_m =
                lerp(54.0F, 360.0F, smoothstep(0.20F, 0.86F, segment_strength));
            const float influence_radius_cells =
                std::ceil((segment_core_width_m * 2.85F) / std::max(desc.cell_size_m, 1.0F));
            const float min_x = std::min(a.x, b.x) - influence_radius_cells;
            const float max_x = std::max(a.x, b.x) + influence_radius_cells;
            const float min_y = std::min(a.y, b.y) - influence_radius_cells;
            const float max_y = std::max(a.y, b.y) + influence_radius_cells;
            const std::uint32_t x0 =
                static_cast<std::uint32_t>(std::max(0.0F, std::floor(min_x)));
            const std::uint32_t y0 =
                static_cast<std::uint32_t>(std::max(0.0F, std::floor(min_y)));
            const std::uint32_t x1 = static_cast<std::uint32_t>(
                std::min(static_cast<float>(desc.width - 1U), std::ceil(max_x)));
            const std::uint32_t y1 = static_cast<std::uint32_t>(
                std::min(static_cast<float>(desc.height - 1U), std::ceil(max_y)));

            for (std::uint32_t y = y0; y <= y1; ++y) {
                for (std::uint32_t x = x0; x <= x1; ++x) {
                    float segment_t = 0.0F;
                    const float distance_cells = point_segment_distance_cells(
                        static_cast<float>(x), static_cast<float>(y), a, b, segment_t);
                    const float distance_m = distance_cells * desc.cell_size_m;
                    const float local_strength =
                        saturate(lerp(a.strength, b.strength, segment_t) * 0.70F +
                                 segment_strength * 0.30F);
                    const float local_incision =
                        saturate(lerp(a.incision, b.incision, segment_t) * 0.70F +
                                 segment_incision * 0.30F);
                    const float core_width_m =
                        lerp(48.0F, segment_core_width_m,
                             smoothstep(0.12F, 0.86F,
                                        local_strength * 0.68F + local_incision * 0.32F));
                    const float core =
                        1.0F - smoothstep(core_width_m * 0.12F, core_width_m, distance_m);
                    const float broad =
                        1.0F - smoothstep(core_width_m * 0.75F, core_width_m * 2.55F, distance_m);
                    const float channel =
                        saturate(core * (0.48F + local_strength * 0.58F) +
                                 broad * local_strength * 0.18F);
                    const float incision_value =
                        saturate(core * (0.32F + segment_incision * 0.70F) +
                                 broad * segment_incision * 0.24F);
                    const std::size_t sample = grid_index(x, y, desc.width);
                    wash[sample] = std::max(wash[sample], channel);
                    incision[sample] = std::max(incision[sample], incision_value);
                    channel_distance_m[sample] = std::min(channel_distance_m[sample], distance_m);
                }
            }
        }
    }
    return true;
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
            const float drainage = smoothstep(0.44F, 0.76F, flow_t);
            const float slope_t = smoothstep(0.025F, 0.32F, slope[sample]);
            const float guide_bias = smoothstep(0.06F, 0.50F, guide);
            const float downstream = smoothstep(-0.92F, 0.78F, p.z);
            const float meander_noise =
                fbm((p.x * 7.0F) + 2.0F, (p.z * 7.0F) - 4.0F, config.seed + 1913U, 4) * 0.5F + 0.5F;
            const float unguided_bias = 1.0F - guide_bias;
            const float drainage_channel =
                drainage *
                (0.34F + guide_bias * 0.48F + downstream * 0.12F + unguided_bias * meander_noise * 0.08F) *
                (0.72F + slope_t * 0.24F);
            const float guided_channel =
                guide_bias * (0.30F + drainage * 0.45F + meander_noise * 0.08F);
            const float divide_suppression = 1.0F - fields.divide_influence[sample] * 0.54F;
            const float channel_gain = lerp(3.35F, 1.60F, guide_bias);
            const float channel =
                saturate((drainage_channel + guided_channel) * divide_suppression * channel_gain);
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

[[nodiscard]] std::uint32_t arid_regional_extent_for_visible(std::uint32_t visible_extent) {
    constexpr std::uint32_t kAridRegionalMaxExtent = 769U;
    const std::uint32_t expanded = visible_extent <= ((kAridRegionalMaxExtent - 1U) / 3U) + 1U
                                       ? ((visible_extent - 1U) * 3U) + 1U
                                       : kAridRegionalMaxExtent;
    return std::max(visible_extent, expanded);
}

[[nodiscard]] AridRegionalCanyonFields
make_empty_arid_regional_canyon_fields(const TerrainLabConfig& config,
                                       const TerrainLabGridDesc& visible_desc) {
    AridRegionalCanyonFields region;
    region.desc = {
        .seed = config.seed,
        .width = arid_regional_extent_for_visible(visible_desc.width),
        .height = arid_regional_extent_for_visible(visible_desc.height),
        .cell_size_m = visible_desc.cell_size_m,
        .origin_x_m = 0.0F,
        .origin_z_m = 0.0F,
    };

    const std::size_t count = terrain_lab_sample_count(region.desc);
    region.macro_height_m.assign(count, 0.0F);
    region.base_potential.assign(count, 0.0F);
    region.relief_potential.assign(count, 0.0F);
    region.process_potential.assign(count, 0.0F);
    region.runoff.assign(count, 0.0F);
    region.resistance.assign(count, 0.0F);
    region.plateau.assign(count, 0.0F);
    region.canyon_floor.assign(count, 0.0F);
    region.canyon_broad.assign(count, 0.0F);
    region.canyon_wall.assign(count, 0.0F);
    region.wash.assign(count, 0.0F);
    region.rim.assign(count, 0.0F);
    region.bench.assign(count, 0.0F);
    region.talus.assign(count, 0.0F);
    region.broad.assign(count, 0.0F);
    region.bench_noise.assign(count, 0.0F);
    region.channel_distance_m.assign(count, 0.0F);
    region.incision.assign(count, 0.0F);
    return region;
}

void compute_channel_distance(const TerrainLabGridDesc& desc,
                              const std::vector<float>& channel_strength,
                              std::vector<float>& distance_m) {
    const std::size_t count = terrain_lab_sample_count(desc);
    const float inf = std::numeric_limits<float>::max() * 0.25F;
    distance_m.assign(count, inf);
    for (std::size_t sample = 0; sample < count; ++sample) {
        if (channel_strength[sample] > 0.32F) {
            distance_m[sample] = 0.0F;
        }
    }

    const float axial = desc.cell_size_m;
    const float diagonal = desc.cell_size_m * 1.41421356F;
    for (std::uint32_t y = 0; y < desc.height; ++y) {
        for (std::uint32_t x = 0; x < desc.width; ++x) {
            const std::size_t sample = grid_index(x, y, desc.width);
            float best = distance_m[sample];
            if (x > 0U) {
                best = std::min(best, distance_m[grid_index(x - 1U, y, desc.width)] + axial);
            }
            if (y > 0U) {
                best = std::min(best, distance_m[grid_index(x, y - 1U, desc.width)] + axial);
                if (x > 0U) {
                    best = std::min(best,
                                    distance_m[grid_index(x - 1U, y - 1U, desc.width)] + diagonal);
                }
                if (x + 1U < desc.width) {
                    best = std::min(best,
                                    distance_m[grid_index(x + 1U, y - 1U, desc.width)] + diagonal);
                }
            }
            distance_m[sample] = best;
        }
    }

    for (std::uint32_t y = desc.height; y-- > 0U;) {
        for (std::uint32_t x = desc.width; x-- > 0U;) {
            const std::size_t sample = grid_index(x, y, desc.width);
            float best = distance_m[sample];
            if (x + 1U < desc.width) {
                best = std::min(best, distance_m[grid_index(x + 1U, y, desc.width)] + axial);
            }
            if (y + 1U < desc.height) {
                best = std::min(best, distance_m[grid_index(x, y + 1U, desc.width)] + axial);
                if (x > 0U) {
                    best = std::min(best,
                                    distance_m[grid_index(x - 1U, y + 1U, desc.width)] + diagonal);
                }
                if (x + 1U < desc.width) {
                    best = std::min(best,
                                    distance_m[grid_index(x + 1U, y + 1U, desc.width)] + diagonal);
                }
            }
            distance_m[sample] = best;
        }
    }

    const float fallback_distance = std::max(half_extent_x_m(desc), half_extent_z_m(desc)) * 2.0F;
    for (float& distance : distance_m) {
        if (!std::isfinite(distance) || distance >= inf * 0.5F) {
            distance = fallback_distance;
        }
    }
}

void smooth_arid_channel_network(const TerrainLabGridDesc& desc, std::vector<float>& channel,
                                 std::vector<float>& incision) {
    std::vector<float> next_channel = channel;
    std::vector<float> next_incision = incision;
    for (std::uint32_t iteration = 0; iteration < 2U; ++iteration) {
        for (std::uint32_t y = 0; y < desc.height; ++y) {
            for (std::uint32_t x = 0; x < desc.width; ++x) {
                const std::size_t sample = grid_index(x, y, desc.width);
                float channel_sum = channel[sample] * 2.0F;
                float incision_sum = incision[sample] * 2.0F;
                float neighbor_count = 2.0F;
                float neighbor_channel_max = channel[sample];
                float neighbor_incision_max = incision[sample];
                for (std::uint8_t direction = 0U; direction < kFlowSinkDirection; ++direction) {
                    const auto nx = static_cast<std::int32_t>(x) + kFlowDx[direction];
                    const auto ny = static_cast<std::int32_t>(y) + kFlowDy[direction];
                    if (nx < 0 || ny < 0 || nx >= static_cast<std::int32_t>(desc.width) ||
                        ny >= static_cast<std::int32_t>(desc.height)) {
                        continue;
                    }
                    const std::size_t neighbor = grid_index(
                        static_cast<std::uint32_t>(nx), static_cast<std::uint32_t>(ny), desc.width);
                    channel_sum += channel[neighbor];
                    incision_sum += incision[neighbor];
                    neighbor_count += 1.0F;
                    neighbor_channel_max = std::max(neighbor_channel_max, channel[neighbor]);
                    neighbor_incision_max = std::max(neighbor_incision_max, incision[neighbor]);
                }
                const float channel_blur = channel_sum / neighbor_count;
                const float incision_blur = incision_sum / neighbor_count;
                next_channel[sample] =
                    saturate(std::max(channel[sample] * 0.86F, channel_blur * 0.94F) +
                             neighbor_channel_max * 0.08F);
                next_incision[sample] =
                    saturate(std::max(incision[sample] * 0.88F, incision_blur * 0.90F) +
                             neighbor_incision_max * 0.06F);
            }
        }
        channel.swap(next_channel);
        incision.swap(next_incision);
    }
}

[[nodiscard]] AridRegionalCanyonFields
build_arid_regional_canyon_fields(const TerrainLabConfig& config,
                                  const TerrainLabGridDesc& visible_desc) {
    AridRegionalCanyonFields region = make_empty_arid_regional_canyon_fields(config, visible_desc);
    const float arid_elevation_scale_m = config.elevation_scale_m * 0.62F;

    for (std::uint32_t y = 0; y < region.desc.height; ++y) {
        for (std::uint32_t x = 0; x < region.desc.width; ++x) {
            const std::size_t sample = grid_index(x, y, region.desc.width);
            const Point2 p = normalized_sample(region.desc, x, y);
            const float warp_x =
                fbm((p.x * 1.05F) - 7.0F, (p.z * 1.05F) + 4.0F, config.seed + 2501U, 4) * 0.24F;
            const float warp_z =
                fbm((p.x * 1.10F) + 6.0F, (p.z * 1.10F) - 3.0F, config.seed + 2503U, 4) * 0.20F;
            const Point2 q{p.x + warp_x, p.z + warp_z};
            const float downstream_tilt = saturate((1.0F - q.z) * 0.5F);
            const float cross_tilt = q.x * 0.08F;
            const float broad =
                fbm((q.x * 1.05F) - 6.0F, (q.z * 1.05F) + 2.0F, config.seed + 2701U, 5);
            const float broad_secondary =
                fbm((q.x * 1.80F) + 11.0F, (q.z * 1.45F) - 5.0F, config.seed + 2703U, 4);
            const float lithology =
                fbm((q.x * 2.20F) - 13.0F, (q.z * 1.90F) + 17.0F, config.seed + 2705U, 4) * 0.5F +
                0.5F;
            const float runoff =
                saturate(0.38F + smoothstep(-0.42F, 0.74F, broad) * 0.24F +
                         smoothstep(0.18F, 0.86F, broad_secondary * 0.5F + 0.5F) * 0.16F);
            const float resistance = saturate(0.30F + lithology * 0.44F +
                                              smoothstep(-0.24F, 0.82F, broad_secondary) * 0.14F);
            const float plateau =
                saturate(0.30F + downstream_tilt * 0.24F +
                         smoothstep(-0.38F, 0.86F, broad) * 0.30F + resistance * 0.12F);
            const float macro_height =
                ((downstream_tilt * 0.50F) + (broad * 0.25F) + (broad_secondary * 0.14F) +
                 (resistance * 0.070F) + cross_tilt * 0.70F - 0.08F) *
                arid_elevation_scale_m;

            region.macro_height_m[sample] = macro_height;
            region.base_potential[sample] =
                saturate(plateau * 0.60F + downstream_tilt * 0.18F + (broad * 0.5F + 0.5F) * 0.22F);
            region.runoff[sample] = runoff;
            region.resistance[sample] = resistance;
            region.plateau[sample] = plateau;
            region.broad[sample] = broad;
            region.bench_noise[sample] = broad_secondary;
        }
    }

    std::vector<float> slope;
    std::vector<float> curvature;
    std::vector<std::uint8_t> flow_direction;
    std::vector<float> flow_accumulation;
    std::vector<float> stream_power;
    FlowRoutingData flow_routing;
    float max_slope = 0.0F;
    float max_abs_curvature = 0.0F;
    float max_flow_accumulation = 0.0F;
    float max_stream_power = 0.0F;
    compute_slope_and_curvature(region.desc, region.macro_height_m, slope, curvature, max_slope,
                                max_abs_curvature);
    compute_flow_fields(region.desc, region.macro_height_m, slope, flow_direction,
                        flow_accumulation, stream_power, max_flow_accumulation, max_stream_power,
                        &flow_routing);

    const std::size_t count = terrain_lab_sample_count(region.desc);
    std::vector<float> network_source(count, 0.0F);
    std::vector<float> incision_source(count, 0.0F);
    const float inv_log_count =
        1.0F / std::log1p(static_cast<float>(std::max<std::size_t>(count, 1U)));
    const float inv_max_stream = max_stream_power > 0.0F ? 1.0F / max_stream_power : 0.0F;
    for (std::size_t sample = 0; sample < count; ++sample) {
        const float flow_t = std::log1p(flow_accumulation[sample]) * inv_log_count;
        const float stream_t = saturate(stream_power[sample] * inv_max_stream);
        const float slope_t = smoothstep(0.015F, 0.24F, slope[sample]);
        const float runoff = region.runoff[sample];
        const float resistance = region.resistance[sample];
        const float source = saturate((flow_t * 0.66F) + (stream_t * 0.28F) +
                                      (runoff * 0.12F) - (resistance * 0.08F));
        const float trunk = smoothstep(0.38F, 0.70F, source);
        const float tributary = smoothstep(0.24F, 0.58F, source) *
                                smoothstep(0.030F, 0.22F, slope[sample]) * (0.42F + runoff * 0.34F);
        const float wash = saturate(std::max(trunk, tributary * 0.64F));
        const float incision = saturate((trunk * 0.54F) + (stream_t * 0.30F) + (slope_t * 0.18F) +
                                        (runoff * 0.08F) - (resistance * 0.16F));
        network_source[sample] = source;
        incision_source[sample] = incision;
        region.wash[sample] = wash;
        region.incision[sample] = incision;
    }

    const std::vector<AridDrainageTrace> traces = trace_arid_drainage_corridors(
        region.desc, flow_routing, network_source, incision_source, config.seed);
    if (rasterize_arid_drainage_corridors(region.desc, traces, region.wash, region.incision,
                                          region.channel_distance_m)) {
        smooth_arid_channel_network(region.desc, region.wash, region.incision);
    } else {
        smooth_arid_channel_network(region.desc, region.wash, region.incision);
        compute_channel_distance(region.desc, region.wash, region.channel_distance_m);
    }

    for (std::size_t sample = 0; sample < count; ++sample) {
        const float incision = region.incision[sample];
        const float distance = region.channel_distance_m[sample];
        const float wall_width_m = lerp(120.0F, 520.0F, incision);
        const float rim_width_m = wall_width_m * 1.82F;
        const float channel = region.wash[sample];
        const float canyon_floor = smoothstep(0.34F, 0.78F, channel);
        const float canyon_broad =
            saturate((1.0F - smoothstep(wall_width_m * 0.90F, wall_width_m * 3.20F, distance)) *
                     (0.52F + incision * 0.48F));
        const float canyon_wall =
            smoothstep(wall_width_m * 0.30F, wall_width_m * 1.08F, distance) *
            (1.0F - smoothstep(wall_width_m * 1.12F, wall_width_m * 2.28F, distance)) *
            smoothstep(0.10F, 0.70F, incision);
        const float rim = smoothstep(wall_width_m * 1.08F, rim_width_m, distance) *
                          (1.0F - smoothstep(rim_width_m, rim_width_m * 1.92F, distance)) *
                          smoothstep(0.12F, 0.82F, incision);
        const float bench_band = std::sin((distance / std::max(wall_width_m, 1.0F)) * 5.80F +
                                          region.bench_noise[sample] * 2.40F) *
                                     0.5F +
                                 0.5F;
        const float bench = smoothstep(0.48F, 0.78F, bench_band) * (1.0F - canyon_floor * 0.72F) *
                            smoothstep(wall_width_m * 0.75F, rim_width_m * 1.62F, distance) *
                            smoothstep(0.10F, 0.72F, incision);
        const float talus = canyon_wall * smoothstep(0.20F, 0.74F, incision) *
                            (1.0F - smoothstep(0.76F, 1.0F, canyon_floor));
        const float wall_source = saturate(canyon_wall * 1.45F + rim * 0.16F);
        region.canyon_floor[sample] = saturate(canyon_floor);
        region.canyon_broad[sample] = saturate(canyon_broad);
        region.canyon_wall[sample] = wall_source;
        region.rim[sample] = saturate(rim);
        region.bench[sample] = saturate(bench);
        region.talus[sample] = saturate(talus);
        const float wall_incision = incision * (1.0F - smoothstep(0.30F, 0.82F, channel));
        region.relief_potential[sample] =
            saturate((wall_source * 0.64F) + (rim * 0.44F) + (bench * 0.18F) + (talus * 0.18F) +
                     (wall_incision * 0.34F) + smoothstep(0.05F, 0.30F, wall_incision) * 0.36F);
        region.process_potential[sample] =
            saturate((channel * 0.36F) + (canyon_floor * 0.22F) + (talus * 0.16F) +
                     (canyon_broad * 0.12F) + (incision * 0.18F));
    }

    return region;
}

[[nodiscard]] std::vector<std::uint32_t> arid_crop_candidates(std::uint32_t max_offset,
                                                              std::uint32_t visible_extent) {
    std::vector<std::uint32_t> candidates;
    const std::uint32_t step = std::max<std::uint32_t>(8U, visible_extent / 6U);
    for (std::uint32_t offset = 0; offset < max_offset; offset += step) {
        candidates.push_back(offset);
    }
    candidates.push_back(max_offset);
    std::sort(candidates.begin(), candidates.end());
    candidates.erase(std::unique(candidates.begin(), candidates.end()), candidates.end());
    return candidates;
}

[[nodiscard]] float score_arid_canyon_crop(const AridRegionalCanyonFields& region,
                                           const TerrainLabGridDesc& visible_desc,
                                           AridCanyonCrop crop) {
    const std::uint32_t stride = std::max<std::uint32_t>(1U, visible_desc.width / 48U);
    std::array<bool, 4> saw_quadrant{false, false, false, false};
    double channel_sum = 0.0;
    double strong_channel_sum = 0.0;
    double wall_sum = 0.0;
    double edge_channel_sum = 0.0;
    std::uint32_t channel_count = 0;
    std::uint32_t sample_count = 0;
    std::uint32_t min_channel_x = visible_desc.width;
    std::uint32_t max_channel_x = 0U;
    std::uint32_t min_channel_y = visible_desc.height;
    std::uint32_t max_channel_y = 0U;

    for (std::uint32_t y = 0; y < visible_desc.height; y += stride) {
        for (std::uint32_t x = 0; x < visible_desc.width; x += stride) {
            const std::size_t sample =
                grid_index(crop.offset_x + x, crop.offset_y + y, region.desc.width);
            const float channel = region.wash[sample];
            const float wall = region.canyon_wall[sample] + region.rim[sample];
            const bool edge = x < stride * 2U || y < stride * 2U ||
                              x + stride * 2U >= visible_desc.width ||
                              y + stride * 2U >= visible_desc.height;
            channel_sum += channel;
            strong_channel_sum += smoothstep(0.52F, 0.86F, channel);
            wall_sum += wall;
            if (edge) {
                edge_channel_sum += channel;
            }
            if (channel > 0.30F) {
                ++channel_count;
                min_channel_x = std::min(min_channel_x, x);
                max_channel_x = std::max(max_channel_x, x);
                min_channel_y = std::min(min_channel_y, y);
                max_channel_y = std::max(max_channel_y, y);
                const std::size_t quadrant = (x >= visible_desc.width / 2U ? 1U : 0U) +
                                             (y >= visible_desc.height / 2U ? 2U : 0U);
                saw_quadrant[quadrant] = true;
            }
            ++sample_count;
        }
    }

    const float inv_samples = sample_count == 0U ? 0.0F : 1.0F / static_cast<float>(sample_count);
    const float channel_fraction = static_cast<float>(channel_count) * inv_samples;
    const float x_spread =
        channel_count == 0U
            ? 0.0F
            : static_cast<float>(max_channel_x - min_channel_x) /
                  static_cast<float>(std::max<std::uint32_t>(visible_desc.width - 1U, 1U));
    const float y_spread =
        channel_count == 0U
            ? 0.0F
            : static_cast<float>(max_channel_y - min_channel_y) /
                  static_cast<float>(std::max<std::uint32_t>(visible_desc.height - 1U, 1U));
    const std::uint32_t quadrant_count =
        static_cast<std::uint32_t>(std::count(saw_quadrant.begin(), saw_quadrant.end(), true));

    return static_cast<float>((channel_sum * 0.12) + (strong_channel_sum * 0.90) +
                              (wall_sum * 0.10) - (edge_channel_sum * 0.28)) +
           static_cast<float>(quadrant_count) * 18.0F + x_spread * 18.0F + y_spread * 10.0F -
           std::abs(channel_fraction - 0.12F) * 120.0F;
}

[[nodiscard]] AridCanyonCrop select_arid_canyon_crop(const AridRegionalCanyonFields& region,
                                                     const TerrainLabGridDesc& visible_desc) {
    const std::uint32_t max_x =
        region.desc.width > visible_desc.width ? region.desc.width - visible_desc.width : 0U;
    const std::uint32_t max_y =
        region.desc.height > visible_desc.height ? region.desc.height - visible_desc.height : 0U;
    const std::vector<std::uint32_t> x_candidates = arid_crop_candidates(max_x, visible_desc.width);
    const std::vector<std::uint32_t> y_candidates =
        arid_crop_candidates(max_y, visible_desc.height);

    AridCanyonCrop best{};
    float best_score = -std::numeric_limits<float>::max();
    for (const std::uint32_t y : y_candidates) {
        for (const std::uint32_t x : x_candidates) {
            const AridCanyonCrop crop{.offset_x = x, .offset_y = y};
            const float score = score_arid_canyon_crop(region, visible_desc, crop);
            if (score > best_score) {
                best = crop;
                best_score = score;
            }
        }
    }
    return best;
}

[[nodiscard]] AridMesaSliceFields
generate_arid_mesa_network_slice(const TerrainLabConfig& config,
                                 const TerrainLabGridDesc& visible_desc) {
    const AridRegionalCanyonFields region = build_arid_regional_canyon_fields(config, visible_desc);
    const AridCanyonCrop crop = select_arid_canyon_crop(region, visible_desc);
    AridMesaSliceFields slice;
    const std::size_t visible_count = terrain_lab_sample_count(visible_desc);
    slice.drivers.assign(visible_count, {});
    slice.features.assign(visible_count, {});
    const float distance_scale_m =
        std::max(half_extent_x_m(visible_desc), half_extent_z_m(visible_desc));

    for (std::uint32_t y = 0; y < visible_desc.height; ++y) {
        for (std::uint32_t x = 0; x < visible_desc.width; ++x) {
            const std::size_t visible_sample = grid_index(x, y, visible_desc.width);
            const std::size_t regional_sample =
                grid_index(crop.offset_x + x, crop.offset_y + y, region.desc.width);
            const float canyon_floor = region.canyon_floor[regional_sample];
            const float canyon_broad = region.canyon_broad[regional_sample];
            const float canyon_wall = region.canyon_wall[regional_sample];
            const float wash = region.wash[regional_sample];
            const float plateau = region.plateau[regional_sample];
            const float rim = region.rim[regional_sample];
            const float bench = region.bench[regional_sample];
            const float talus = region.talus[regional_sample];
            const float distance_norm =
                distance_scale_m <= 0.0F
                    ? 0.0F
                    : saturate(region.channel_distance_m[regional_sample] / distance_scale_m);
            const float channel = saturate(std::max(canyon_floor, wash * 0.48F));
            const float valley = saturate(canyon_broad * 0.76F + wash * 0.16F);
            const float basin = saturate(canyon_floor * 0.72F + wash * 0.12F);
            const float plateau_divide =
                smoothstep(0.48F, 0.82F, plateau) * (1.0F - smoothstep(0.18F, 0.70F, valley));
            const float relief_driver = region.relief_potential[regional_sample];
            const float ridge =
                saturate(canyon_wall * 1.02F + rim * 0.70F + bench * 0.22F +
                         plateau_divide * 0.72F + smoothstep(0.10F, 0.36F, relief_driver) * 0.56F +
                         plateau * (0.06F + region.base_potential[regional_sample] * 0.06F));
            const float divide = saturate(rim * 0.90F + canyon_wall * 0.24F + bench * 0.34F +
                                          plateau_divide * 0.72F + (1.0F - valley) * 0.08F);
            const float driver_relief =
                saturate(region.relief_potential[regional_sample] * (1.0F - channel * 0.76F) +
                         ridge * 0.42F + rim * 0.18F);

            slice.drivers[visible_sample] = {
                .base_potential = region.base_potential[regional_sample],
                .relief_potential = driver_relief,
                .process_potential = region.process_potential[regional_sample],
                .selection_mask = 1.0F,
                .canyon_floor_source = canyon_floor,
                .canyon_broad_source = canyon_broad,
                .canyon_wall_source = canyon_wall,
                .wash_source = wash,
                .plateau_source = plateau,
                .rim_source = rim,
                .bench_source = bench,
                .talus_source = talus,
                .plateau_noise_source = region.base_potential[regional_sample],
                .broad_source = region.broad[regional_sample],
                .bench_noise_source = region.bench_noise[regional_sample],
                .channel_distance_norm = distance_norm,
            };
            slice.features[visible_sample] = {
                .canyon_floor = canyon_floor,
                .canyon_wall = canyon_wall,
                .wash_influence = wash,
                .plateau_influence = plateau,
                .rim_influence = rim,
                .bench_influence = bench,
                .talus_influence = talus,
                .ridge_influence = ridge,
                .valley_influence = valley,
                .basin_influence = basin,
                .divide_influence = divide,
                .channel_influence = channel,
                .channel_distance_m = region.channel_distance_m[regional_sample],
            };
        }
    }

    return slice;
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

void populate_fallback_driver_fields(TerrainLabFieldData& fields) {
    const bool has_explicit_driver =
        std::any_of(fields.driver_selection_mask.begin(), fields.driver_selection_mask.end(),
                    [](float value) { return value > 0.0F; });
    if (has_explicit_driver) {
        return;
    }

    const float height_span = std::max(fields.max_height_m - fields.min_height_m, 1.0F);
    for (std::size_t sample = 0; sample < fields.sample_count(); ++sample) {
        const float height_t =
            saturate((fields.height_m[sample] - fields.min_height_m) / height_span);
        fields.driver_base_potential[sample] = height_t;
        fields.driver_relief_potential[sample] = fields.ridge_influence[sample];
        fields.driver_process_potential[sample] =
            saturate((fields.channel_influence[sample] * 0.35F) +
                     (fields.deposition[sample] * 0.40F) + (fields.wetness[sample] * 0.25F));
        fields.driver_selection_mask[sample] = 1.0F;
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
    require_size(fields.driver_base_potential.size(),
                 "terrain lab driver base field size mismatch");
    require_size(fields.driver_relief_potential.size(),
                 "terrain lab driver relief field size mismatch");
    require_size(fields.driver_process_potential.size(),
                 "terrain lab driver process field size mismatch");
    require_size(fields.driver_selection_mask.size(),
                 "terrain lab driver selection field size mismatch");
    require_size(fields.structure_height_m.size(), "terrain lab structure field size mismatch");
    require_size(fields.process_delta_m.size(), "terrain lab process field size mismatch");
    require_size(fields.detail_height_m.size(), "terrain lab detail field size mismatch");
    require_size(fields.slope.size(), "terrain lab slope field size mismatch");
    require_size(fields.curvature.size(), "terrain lab curvature field size mismatch");
    require_size(fields.flow_direction.size(), "terrain lab flow direction field size mismatch");
    require_size(fields.flow_accumulation.size(),
                 "terrain lab flow accumulation field size mismatch");
    require_size(fields.stream_power.size(), "terrain lab stream power field size mismatch");
    require_size(fields.river_discharge.size(), "terrain lab river discharge field size mismatch");
    require_size(fields.stream_order.size(), "terrain lab stream order field size mismatch");
    require_size(fields.river_width_m.size(), "terrain lab river width field size mismatch");
    require_size(fields.valley_width_m.size(), "terrain lab valley width field size mismatch");
    require_size(fields.water_presence.size(),
                 "terrain lab water presence field size mismatch");
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
    require_size(fields.drainage_region_id.size(),
                 "terrain lab drainage region field size mismatch");
    require_size(fields.divide_influence.size(), "terrain lab divide field size mismatch");
    require_size(fields.channel_influence.size(), "terrain lab channel field size mismatch");
    require_size(fields.channel_distance_m.size(),
                 "terrain lab channel distance field size mismatch");
    if (fields.drainage_region_count == 0U) {
        throw std::runtime_error("terrain lab fields require at least one drainage region");
    }
    validate_finite(fields.max_channel_distance_m,
                    "terrain lab max channel distance must be finite");
    if (fields.max_channel_distance_m < 0.0F) {
        throw std::runtime_error("terrain lab max channel distance must be nonnegative");
    }
    validate_finite(fields.max_river_discharge, "terrain lab max river discharge must be finite");
    validate_finite(fields.max_river_width_m, "terrain lab max river width must be finite");
    validate_finite(fields.max_valley_width_m, "terrain lab max valley width must be finite");
    if (fields.max_river_discharge < 0.0F || fields.max_river_width_m < 0.0F ||
        fields.max_valley_width_m < 0.0F) {
        throw std::runtime_error("terrain lab river ranges must be nonnegative");
    }

    for (std::size_t sample = 0; sample < count; ++sample) {
        validate_finite(fields.height_m[sample], "terrain lab height must be finite");
        validate_normalized(fields.driver_base_potential[sample],
                            "terrain lab driver base must be normalized");
        validate_normalized(fields.driver_relief_potential[sample],
                            "terrain lab driver relief must be normalized");
        validate_normalized(fields.driver_process_potential[sample],
                            "terrain lab driver process must be normalized");
        validate_normalized(fields.driver_selection_mask[sample],
                            "terrain lab driver selection must be normalized");
        validate_finite(fields.structure_height_m[sample],
                        "terrain lab structure height must be finite");
        validate_finite(fields.process_delta_m[sample], "terrain lab process delta must be finite");
        validate_finite(fields.detail_height_m[sample], "terrain lab detail height must be finite");
        validate_finite(fields.slope[sample], "terrain lab slope must be finite");
        validate_finite(fields.curvature[sample], "terrain lab curvature must be finite");
        validate_finite(fields.flow_accumulation[sample],
                        "terrain lab flow accumulation must be finite");
        validate_finite(fields.stream_power[sample], "terrain lab stream power must be finite");
        validate_finite(fields.river_discharge[sample],
                        "terrain lab river discharge must be finite");
        validate_finite(fields.river_width_m[sample], "terrain lab river width must be finite");
        validate_finite(fields.valley_width_m[sample], "terrain lab valley width must be finite");
        validate_normalized(fields.water_presence[sample],
                            "terrain lab water presence must be normalized");
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
        if (fields.drainage_region_id[sample] >= fields.drainage_region_count) {
            throw std::runtime_error("terrain lab drainage region id must be valid");
        }
        if (fields.flow_accumulation[sample] < 0.0F) {
            throw std::runtime_error("terrain lab flow accumulation must be nonnegative");
        }
        if (fields.stream_power[sample] < 0.0F) {
            throw std::runtime_error("terrain lab stream power must be nonnegative");
        }
        if (fields.river_discharge[sample] < 0.0F || fields.river_width_m[sample] < 0.0F ||
            fields.valley_width_m[sample] < 0.0F) {
            throw std::runtime_error("terrain lab river fields must be nonnegative");
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
        .drainage_region_count = fields.drainage_region_count,
        .min_height_m = fields.min_height_m,
        .max_height_m = fields.max_height_m,
        .height_span_m = fields.max_height_m - fields.min_height_m,
        .max_flow_accumulation = fields.max_flow_accumulation,
        .max_river_discharge = fields.max_river_discharge,
        .max_stream_order = fields.max_stream_order,
        .max_river_width_m = fields.max_river_width_m,
        .max_valley_width_m = fields.max_valley_width_m,
        .max_channel_distance_m = fields.max_channel_distance_m,
    };
    if (summary.sample_count == 0U) {
        return summary;
    }
    double height_sum = 0.0;
    double slope_sum = 0.0;
    double wetness_sum = 0.0;
    double water_presence_sum = 0.0;
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
        water_presence_sum += fields.water_presence[sample];
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
    summary.mean_water_presence = static_cast<float>(water_presence_sum * inv_count);
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
        const double inv_non_channel = 1.0 / static_cast<double>(summary.non_channel_sample_count);
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

TerrainLabFieldData generate_temperate_mountain_river_fields(const TerrainLabConfig& config) {
    validate_terrain_lab_config(config);
    TerrainLabFieldData fields = make_empty_terrain_lab_fields(config);
    const std::size_t count = fields.sample_count();
    fields.drainage_region_count = 1U;

    for (std::uint32_t y = 0; y < fields.desc.height; ++y) {
        for (std::uint32_t x = 0; x < fields.desc.width; ++x) {
            const std::size_t sample = fields.index(x, y);
            const Point2 p = normalized_sample(fields.desc, x, y);
            const float ridge_source = ridge_influence(p, config);
            const float basin_source = basin_influence(p, config);
            const float valley_source = valley_influence(p, config);
            const float divide_noise =
                fbm((p.x * 3.8F) + 8.0F, (p.z * 3.8F) - 11.0F, config.seed + 1723U, 4) * 0.5F +
                0.5F;
            const float divide =
                saturate((ridge_source * 0.62F) + ((1.0F - basin_source) * 0.10F) +
                         smoothstep(0.56F, 0.90F, divide_noise) * 0.18F);
            const float ridge =
                saturate((ridge_source * 0.68F) + (divide * 0.16F));
            const float basin = saturate((basin_source * 0.58F) + (1.0F - divide) * 0.08F);
            const float valley =
                saturate((valley_source * 0.24F) + (1.0F - ridge) * 0.08F);
            const float headwater = saturate((1.0F - p.z) * 0.5F);
            const float broad = fbm(p.x * 1.3F - 3.0F, p.z * 1.3F + 5.0F, config.seed + 101U, 5);
            const float structure =
                ((headwater * 0.46F) + (ridge * 0.42F) + (divide * 0.05F) + (broad * 0.18F) -
                 (basin * 0.16F) - (valley * 0.16F) - 0.12F) *
                config.elevation_scale_m * config.structure_strength;

            fields.ridge_influence[sample] = ridge;
            fields.valley_influence[sample] = valley;
            fields.basin_influence[sample] = basin;
            fields.divide_influence[sample] = divide;
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
    compute_channel_distance(fields.desc, fields.channel_influence, fields.channel_distance_m);
    fields.max_channel_distance_m =
        *std::max_element(fields.channel_distance_m.begin(), fields.channel_distance_m.end());

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
    derive_river_network_fields(fields,
                                {
                                    .runoff_scale = 1.0F,
                                    .water_presence_scale = 1.55F,
                                    .min_width_m = fields.desc.cell_size_m * 0.20F,
                                    .max_width_m = fields.desc.cell_size_m * 1.90F,
                                    .valley_width_multiplier = 5.8F,
                                    .stream_threshold = 0.28F,
                                    .local_active_scale = 0.0F,
                                    .extract_visible_trunks = true,
                                    .prune_disconnected_fragments = true,
                                });

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
            const float river_t =
                fields.max_river_width_m <= 0.0F
                    ? 0.0F
                    : saturate(fields.river_width_m[sample] / fields.max_river_width_m);
            const float water = fields.water_presence[sample];
            const float divide = fields.divide_influence[sample];
            const float valley =
                saturate((fields.valley_influence[sample] * 0.32F) + (channel * 0.78F));
            const float wetness =
                saturate((flow_t * 0.38F) + (valley * 0.22F) + (channel * 0.16F) +
                         (river_t * 0.22F) + (water * 0.30F) +
                         ((1.0F - slope_t) * 0.12F) - (divide * 0.08F));
            const float deposition =
                saturate(wetness * (1.0F - slope_t) *
                         (0.12F + fields.basin_influence[sample] * 0.22F + channel * 0.42F +
                          river_t * 0.40F + water * 0.26F));
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
                               (1.0F - snow) * (1.0F - channel * 0.22F) *
                               (1.0F - water * 0.56F);
            const float scree = smoothstep(0.30F, 0.78F, slope_t) * (1.0F - wetness * 0.55F) *
                                (1.0F - channel * 0.22F) * (1.0F - water * 0.48F) *
                                (1.0F - snow) *
                                lerp(0.70F, 1.28F, scree_patch);
            const float meadow =
                wetness * (1.0F - slope_t) * (1.0F - high * 0.55F) *
                (0.52F + channel * 0.18F + river_t * 0.26F + material_noise * 0.32F);
            const float forest = smoothstep(0.26F, 0.72F, wetness) * (1.0F - slope_t) *
                                 (1.0F - high) * (1.0F - deposition * 0.35F) *
                                 (1.0F - channel * 0.20F) * (1.0F - water * 0.72F) *
                                 lerp(0.48F, 1.34F, vegetation_patch);
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

    populate_fallback_driver_fields(fields);
    validate_terrain_lab_fields(fields);
    return fields;
}

TerrainLabFieldData generate_arid_mesa_canyon_fields(const TerrainLabConfig& config) {
    validate_terrain_lab_config(config);
    TerrainLabFieldData fields = make_empty_terrain_lab_fields(config);
    const std::size_t count = fields.sample_count();
    const float arid_elevation_scale_m = config.elevation_scale_m * 0.62F;
    fields.drainage_region_count = 1U;
    fields.max_channel_distance_m = 0.0F;
    const AridMesaSliceFields arid_slice = generate_arid_mesa_network_slice(config, fields.desc);

    for (std::uint32_t y = 0; y < fields.desc.height; ++y) {
        for (std::uint32_t x = 0; x < fields.desc.width; ++x) {
            const std::size_t sample = fields.index(x, y);
            const Point2 p = normalized_sample(fields.desc, x, y);
            const AridMesaDriver& driver = arid_slice.drivers[sample];
            const AridMesaSampleFeatures& features = arid_slice.features[sample];
            const float high_desert_tilt = (1.0F - p.z) * 0.5F;
            const float mesa_bench =
                smoothstep(-0.18F, 0.78F, driver.broad_source + features.plateau_influence * 0.62F);
            const float bench_step = (features.rim_influence * 0.090F) +
                                     (features.bench_influence * 0.055F) -
                                     (features.talus_influence * 0.030F);
            const float canyon_cut = (features.valley_influence * 0.16F) +
                                     (features.canyon_floor * 0.24F) +
                                     (features.wash_influence * 0.025F);
            const float structure =
                ((high_desert_tilt * 0.24F) + (features.plateau_influence * 0.30F) +
                 (features.ridge_influence * 0.08F) + (mesa_bench * 0.08F) + bench_step +
                 (driver.broad_source * 0.11F) + (driver.bench_noise_source * 0.035F) - canyon_cut -
                 0.08F) *
                arid_elevation_scale_m * config.structure_strength;

            fields.driver_base_potential[sample] = driver.base_potential;
            fields.driver_relief_potential[sample] = driver.relief_potential;
            fields.driver_process_potential[sample] = driver.process_potential;
            fields.driver_selection_mask[sample] = driver.selection_mask;
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
        const float driver_process = fields.driver_process_potential[sample];
        const float wash_cut = channel * smoothstep(0.06F, 0.76F, flow_t + driver_process * 0.24F) *
                               arid_elevation_scale_m * 0.050F * config.process_strength;
        const float sheet_erosion = valley * slope_t * (0.72F + driver_process * 0.28F) *
                                    arid_elevation_scale_m * 0.020F * config.process_strength;
        const float cliff_weathering = wall * smoothstep(0.55F, 0.96F, slope_t) *
                                       arid_elevation_scale_m * 0.016F * config.process_strength;
        const float talus_deposit = fields.driver_relief_potential[sample] *
                                    smoothstep(0.14F, 0.52F, slope_t) *
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
            const AridMesaSampleFeatures& features = arid_slice.features[sample];
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
    derive_river_network_fields(fields, arid_river_derivation_params(fields.desc));
    apply_arid_river_hierarchy_carving(config, fields, arid_elevation_scale_m);
    update_height_range(fields);
    compute_slope_and_curvature(fields.desc, fields.height_m, fields.slope, fields.curvature,
                                fields.max_slope, fields.max_abs_curvature);
    compute_flow_fields(fields.desc, fields.height_m, fields.slope, fields.flow_direction,
                        fields.flow_accumulation, fields.stream_power, fields.max_flow_accumulation,
                        fields.max_stream_power);
    derive_river_network_fields(fields, arid_river_derivation_params(fields.desc));

    const float height_span = std::max(fields.max_height_m - fields.min_height_m, 1.0F);
    fields.max_wetness = 0.0F;
    fields.max_deposition = 0.0F;
    for (std::uint32_t y = 0; y < fields.desc.height; ++y) {
        for (std::uint32_t x = 0; x < fields.desc.width; ++x) {
            const std::size_t sample = fields.index(x, y);
            const Point2 p = normalized_sample(fields.desc, x, y);
            const AridMesaSampleFeatures& features = arid_slice.features[sample];
            const float elevation_t =
                saturate((fields.height_m[sample] - fields.min_height_m) / height_span);
            const float slope_t = smoothstep(0.05F, 0.52F, fields.slope[sample]);
            const float flow_t = std::log1p(fields.flow_accumulation[sample]) * inv_log_count;
            const float channel = fields.channel_influence[sample];
            const float river_width_t =
                fields.max_river_width_m <= 0.0F
                    ? 0.0F
                    : saturate(fields.river_width_m[sample] / fields.max_river_width_m);
            const float river_order_t =
                fields.max_stream_order <= 1U
                    ? 0.0F
                    : static_cast<float>(fields.stream_order[sample]) /
                          static_cast<float>(fields.max_stream_order);
            const float dry_wash =
                saturate((channel * 0.58F) + (features.wash_influence * 0.18F) +
                         river_width_t * 0.30F + river_order_t * 0.10F);
            const float wetness = saturate(
                (flow_t * 0.13F) + (dry_wash * 0.10F) + (river_width_t * 0.035F) +
                (river_order_t * 0.018F) + (fields.basin_influence[sample] * 0.045F) +
                ((1.0F - slope_t) * 0.025F) - (fields.divide_influence[sample] * 0.05F));
            const float deposition =
                saturate((dry_wash * (1.0F - slope_t) * 0.38F) +
                         river_width_t * 0.075F + river_order_t * 0.035F +
                         (fields.basin_influence[sample] * 0.14F) + (flow_t * 0.030F));
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
                 (features.rim_influence * 0.64F) + (fields.ridge_influence[sample] * 1.05F)) *
                (1.0F - dry_wash * 0.62F) * lerp(0.84F, 1.20F, material_noise);
            const float ridge_exposure =
                fields.ridge_influence[sample] * (1.0F - smoothstep(0.18F, 0.74F, dry_wash));
            const float scree = smoothstep(0.18F, 0.72F, slope_t) *
                                    (1.0F - smoothstep(0.78F, 1.0F, slope_t)) *
                                    (0.30F + features.talus_influence * 0.58F +
                                     features.canyon_wall * 0.36F + dry_wash * 0.08F) *
                                    (1.0F - dry_wash * 0.50F) * lerp(0.76F, 1.28F, scree_patch) +
                                ridge_exposure * 0.26F;
            const float soil = (0.24F + deposition * 0.78F + (1.0F - slope_t) * 0.14F +
                                features.plateau_influence * 0.14F +
                                features.bench_influence * 0.08F + dry_wash * 1.58F) *
                               (1.0F - ridge_exposure * 0.64F) * lerp(0.86F, 1.18F, material_noise);
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
    const float dune_elevation_scale_m = config.elevation_scale_m * 0.36F;
    fields.drainage_region_count = 1U;
    fields.max_channel_distance_m = 0.0F;

    for (std::uint32_t y = 0; y < fields.desc.height; ++y) {
        for (std::uint32_t x = 0; x < fields.desc.width; ++x) {
            const std::size_t sample = fields.index(x, y);
            const Point2 p = normalized_sample(fields.desc, x, y);
            const DesertDuneDriver driver = desert_dune_driver_at(p, config);
            const DesertDuneSampleFeatures features =
                desert_dune_features_at(p, fields.desc, config);
            const float dune_body =
                saturate(features.dune_body * 0.86F + features.slip_face * 0.10F);
            const float structure =
                ((driver.base_potential * 0.18F) + (driver.relief_potential * 0.70F) +
                 (dune_body * 0.16F) + (driver.process_potential * 0.045F) -
                 (features.interdune_flat * 0.075F) + (p.z * 0.018F) - 0.10F) *
                dune_elevation_scale_m * config.structure_strength;

            fields.driver_base_potential[sample] = driver.base_potential;
            fields.driver_relief_potential[sample] = driver.relief_potential;
            fields.driver_process_potential[sample] = driver.process_potential;
            fields.driver_selection_mask[sample] = driver.selection_mask;
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
        const float lee_deposit = fields.driver_process_potential[sample] *
                                  fields.ridge_influence[sample] * (1.0F - slope_t) *
                                  dune_elevation_scale_m * 0.024F * config.process_strength;
        const float interdune_fill = fields.basin_influence[sample] *
                                     fields.driver_selection_mask[sample] * (1.0F - slope_t) *
                                     dune_elevation_scale_m * 0.010F * config.process_strength;
        const float wind_scour = fields.valley_influence[sample] *
                                 fields.driver_process_potential[sample] *
                                 smoothstep(0.02F, 0.28F, slope_t) * dune_elevation_scale_m *
                                 0.012F * config.process_strength;
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
            const DesertDuneDriver driver = desert_dune_driver_at(p, config);
            const DesertDuneSampleFeatures features =
                desert_dune_features_at(p, fields.desc, config);
            const float elevation_t =
                saturate((fields.height_m[sample] - fields.min_height_m) / process_height_span);
            const float ripple_noise =
                fbm((p.x * 15.0F) + 7.0F, (p.z * 15.0F) - 3.0F, config.seed + 3211U, 3) * 0.5F +
                0.5F;
            const float ripple_gate =
                smoothstep(0.16F, 0.72F,
                           driver.relief_potential + driver.process_potential * 0.20F) *
                lerp(0.40F, 1.0F, ripple_noise);
            const float ripple =
                std::sin(((driver.dune_u * 18.0F) + (driver.dune_v * 11.0F)) +
                         fbm((driver.dune_u * 11.0F) + 9.0F, (driver.dune_v * 11.0F) - 5.0F,
                             config.seed + 3213U, 3) *
                             2.4F) *
                0.00045F * ripple_gate;
            const float crest_break = fbm((driver.dune_u * 23.0F) - 11.0F,
                                          (driver.dune_v * 18.0F) + 13.0F, config.seed + 3217U, 3) *
                                      features.dune_crest * 0.0014F;
            const float interdune_crust =
                fbm((driver.dune_u * 12.0F) + 19.0F, (driver.dune_v * 12.0F) - 17.0F,
                    config.seed + 3221U, 3) *
                features.interdune_flat * 0.0016F;
            const float detail =
                (ripple * (0.50F + elevation_t * 0.50F) + crest_break + interdune_crust) *
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
    derive_river_network_fields(fields,
                                {
                                    .runoff_scale = 0.12F,
                                    .water_presence_scale = 0.0F,
                                    .min_width_m = fields.desc.cell_size_m * 0.08F,
                                    .max_width_m = fields.desc.cell_size_m * 0.44F,
                                    .valley_width_multiplier = 2.0F,
                                    .stream_threshold = 0.42F,
                                });

    const float inv_log_count =
        1.0F / std::log1p(static_cast<float>(std::max<std::size_t>(count, 1U)));
    fields.max_wetness = 0.0F;
    fields.max_deposition = 0.0F;
    for (std::uint32_t y = 0; y < fields.desc.height; ++y) {
        for (std::uint32_t x = 0; x < fields.desc.width; ++x) {
            const std::size_t sample = fields.index(x, y);
            const Point2 p = normalized_sample(fields.desc, x, y);
            const DesertDuneDriver driver = desert_dune_driver_at(p, config);
            const DesertDuneSampleFeatures features =
                desert_dune_features_at(p, fields.desc, config);
            const float slope_t = smoothstep(0.018F, 0.26F, fields.slope[sample]);
            const float flow_t = std::log1p(fields.flow_accumulation[sample]) * inv_log_count;
            const float wetness = saturate((features.interdune_flat * 0.028F) +
                                           (fields.basin_influence[sample] * 0.018F) +
                                           (driver.selection_mask * flow_t * 0.012F));
            const float deposition =
                saturate((driver.process_potential * 0.38F) + (features.interdune_flat * 0.28F) +
                         (features.wind_shadow * 0.20F)) *
                (1.0F - slope_t * 0.38F);
            fields.wetness[sample] = wetness;
            fields.deposition[sample] = deposition;
            fields.max_wetness = std::max(fields.max_wetness, wetness);
            fields.max_deposition = std::max(fields.max_deposition, deposition);

            const float material_noise =
                fbm((p.x * 7.0F) + 5.0F, (p.z * 7.0F) - 9.0F, config.seed + 3231U, 4) * 0.5F + 0.5F;
            const float crust_patch =
                fbm((p.x * 10.0F) - 15.0F, (p.z * 10.0F) + 21.0F, config.seed + 3233U, 3) * 0.5F +
                0.5F;
            const float sand = (1.15F + deposition * 0.60F + driver.relief_potential * 0.20F) *
                               lerp(0.86F, 1.16F, material_noise);
            const float soil = (features.interdune_flat * 0.18F + wetness * 0.42F) *
                               lerp(0.78F, 1.20F, crust_patch);
            const float scree = slope_t * driver.process_potential * features.slip_face * 0.12F;
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
            const float shrub_patch = smoothstep(0.28F, 0.58F, vegetation_patch);
            const float shrub = saturate((features.interdune_flat * 0.024F +
                                          material.soil * 0.018F + driver.selection_mask * 0.016F) *
                                         shrub_patch * (1.0F - slope_t));
            fields.grass_density[sample] = grass;
            fields.shrub_density[sample] = shrub;
            fields.tree_density[sample] = 0.0F;
            fields.canopy_height_m[sample] = 0.0F;
        }
    }

    populate_fallback_driver_fields(fields);
    validate_terrain_lab_fields(fields);
    return fields;
}

TerrainLabFieldData generate_alpine_glacial_valley_fields(const TerrainLabConfig& config) {
    validate_terrain_lab_config(config);
    TerrainLabFieldData fields = make_empty_terrain_lab_fields(config);
    const std::size_t count = fields.sample_count();
    const float alpine_elevation_scale_m = config.elevation_scale_m * 0.86F;
    fields.drainage_region_count = 1U;
    fields.max_channel_distance_m = 0.0F;

    for (std::uint32_t y = 0; y < fields.desc.height; ++y) {
        for (std::uint32_t x = 0; x < fields.desc.width; ++x) {
            const std::size_t sample = fields.index(x, y);
            const Point2 p = normalized_sample(fields.desc, x, y);
            const AlpineGlacialDriver driver = alpine_glacial_driver_at(p, config);
            const AlpineGlacialSampleFeatures features =
                alpine_glacial_features_at(p, fields.desc, config);
            const float structure =
                ((driver.base_potential * 0.28F) + (driver.relief_potential * 0.34F) +
                 (features.ridge_influence * 0.38F) + (features.peak_influence * 0.52F) +
                 (features.valley_wall * 0.18F) + (features.cirque_influence * 0.10F) -
                 (features.glacier_floor * 0.18F) - (features.hanging_valley * 0.05F) -
                 (features.basin_influence * 0.04F) - 0.08F) *
                alpine_elevation_scale_m * config.structure_strength;

            fields.driver_base_potential[sample] = driver.base_potential;
            fields.driver_relief_potential[sample] = driver.relief_potential;
            fields.driver_process_potential[sample] = driver.process_potential;
            fields.driver_selection_mask[sample] = driver.selection_mask;
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
            const AlpineGlacialDriver driver = alpine_glacial_driver_at(p, config);
            const AlpineGlacialSampleFeatures features =
                alpine_glacial_features_at(p, fields.desc, config);
            const float slope_t = smoothstep(0.06F, 0.42F, temp_slope[sample]);
            const float glacial_carve =
                (driver.ice_accumulation * 0.026F + features.glacier_floor * 0.022F +
                 features.hanging_valley * 0.012F) *
                alpine_elevation_scale_m * config.process_strength;
            const float wall_polish = driver.cliff_source * smoothstep(0.15F, 0.58F, slope_t) *
                                      alpine_elevation_scale_m * 0.010F * config.process_strength;
            const float moraine_deposit = driver.moraine_source * (1.0F - slope_t * 0.55F) *
                                          alpine_elevation_scale_m * 0.060F *
                                          config.process_strength;
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
    derive_river_network_fields(fields,
                                {
                                    .runoff_scale = 0.70F,
                                    .water_presence_scale = 0.28F,
                                    .min_width_m = fields.desc.cell_size_m * 0.14F,
                                    .max_width_m = fields.desc.cell_size_m * 1.05F,
                                    .valley_width_multiplier = 3.5F,
                                    .stream_threshold = 0.34F,
                                });

    const float height_span = std::max(fields.max_height_m - fields.min_height_m, 1.0F);
    const float inv_log_count =
        1.0F / std::log1p(static_cast<float>(std::max<std::size_t>(count, 1U)));
    fields.max_wetness = 0.0F;
    fields.max_deposition = 0.0F;
    for (std::uint32_t y = 0; y < fields.desc.height; ++y) {
        for (std::uint32_t x = 0; x < fields.desc.width; ++x) {
            const std::size_t sample = fields.index(x, y);
            const Point2 p = normalized_sample(fields.desc, x, y);
            const AlpineGlacialDriver driver = alpine_glacial_driver_at(p, config);
            const AlpineGlacialSampleFeatures features =
                alpine_glacial_features_at(p, fields.desc, config);
            const float elevation_t =
                saturate((fields.height_m[sample] - fields.min_height_m) / height_span);
            const float slope_t = smoothstep(0.06F, 0.48F, fields.slope[sample]);
            const float flow_t = std::log1p(fields.flow_accumulation[sample]) * inv_log_count;
            const float wetness =
                saturate((fields.channel_influence[sample] * 0.16F) +
                         (driver.ice_accumulation * 0.09F) + (features.glacier_floor * 0.06F) +
                         (features.basin_influence * 0.06F) + (flow_t * 0.08F));
            const float deposition = saturate((driver.moraine_source * 0.58F) +
                                              (features.glacier_floor * (1.0F - slope_t) * 0.20F) +
                                              (fields.channel_influence[sample] * 0.12F) +
                                              (driver.process_potential * 0.08F));
            fields.wetness[sample] = wetness;
            fields.deposition[sample] = deposition;
            fields.max_wetness = std::max(fields.max_wetness, wetness);
            fields.max_deposition = std::max(fields.max_deposition, deposition);

            const float material_noise =
                fbm((p.x * 7.0F) - 4.0F, (p.z * 7.0F) + 6.0F, config.seed + 3431U, 4) * 0.5F + 0.5F;
            const float scree_patch =
                fbm((p.x * 13.0F) + 18.0F, (p.z * 13.0F) - 12.0F, config.seed + 3433U, 3) * 0.5F +
                0.5F;
            const float snow =
                (smoothstep(0.54F, 0.88F, elevation_t) * (1.0F - slope_t * 0.45F) +
                 features.glacier_floor * smoothstep(0.30F, 0.76F, elevation_t) * 0.46F +
                 features.cirque_influence * 0.36F + features.peak_influence * 0.42F) *
                lerp(0.78F, 1.18F, material_noise);
            const float rock = (slope_t * 0.82F + fields.ridge_influence[sample] * 0.30F +
                                features.valley_wall * 0.24F + features.peak_influence * 0.18F) *
                               (1.0F - snow * 0.58F) * lerp(0.82F, 1.22F, material_noise);
            const float scree =
                smoothstep(0.28F, 0.78F, slope_t) * (1.0F - smoothstep(0.78F, 1.0F, slope_t)) *
                (features.valley_wall * 0.46F + fields.ridge_influence[sample] * 0.22F +
                 features.moraine_influence * 0.30F + features.peak_influence * 0.14F) *
                (1.0F - snow * 0.45F) * lerp(0.72F, 1.30F, scree_patch);
            const float soil =
                (0.20F + deposition * 0.52F + (1.0F - slope_t) * 0.14F) * (1.0F - snow * 0.78F);
            const float meadow = wetness * (1.0F - slope_t) * (1.0F - snow * 0.86F) *
                                 (0.28F + features.glacier_floor * 0.20F);
            const float forest = smoothstep(0.18F, 0.46F, wetness) * (1.0F - slope_t) *
                                 (1.0F - smoothstep(0.42F, 0.72F, elevation_t)) * (1.0F - snow) *
                                 0.26F;
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
    case TerrainLabSlicePreset::TemperateMountainRivers:
        return generate_temperate_mountain_river_fields(config);
    case TerrainLabSlicePreset::DesertDunes:
        return generate_desert_dunes_fields(config);
    case TerrainLabSlicePreset::AlpineGlacialValley:
        return generate_alpine_glacial_valley_fields(config);
    }
    return generate_temperate_mountain_river_fields(config);
}

} // namespace cubey::projects::terrain_lab
