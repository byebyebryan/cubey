#include "terrain_debug_export.h"

#include <cubey/core/jobs.h>
#include <cubey/procedural/operators.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <deque>
#include <fstream>
#include <filesystem>
#include <iomanip>
#include <stdexcept>
#include <string>
#include <sstream>
#include <utility>
#include <vector>

namespace cubey::projects::terrain {
namespace {

struct Rgb {
    float r = 0.0F;
    float g = 0.0F;
    float b = 0.0F;
};

struct DebugViewName {
    TerrainDebugView view = TerrainDebugView::Final;
    std::string_view name{};
};

struct FieldNormalization {
    float min_value = 0.0F;
    float max_value = 1.0F;
    bool log_scale = false;
};

struct MountainProcessReviewNormalization {
    FieldNormalization profile_height{};
    FieldNormalization ridge_body{};
    FieldNormalization valley_floor{};
    FieldNormalization valley_incision{};
    FieldNormalization height{};
    FieldNormalization post_erosion_height{};
    FieldNormalization slope_instability{};
    FieldNormalization thermal_erosion_delta{};
    FieldNormalization talus_deposition{};
};

struct ReviewPanelSample {
    std::size_t panel_index = 0U;
    std::uint32_t x = 0U;
    std::uint32_t y = 0U;
    bool separator = false;
};

inline constexpr std::size_t kTerrainDebugEncodeWorkerCount = 2U;
inline constexpr std::size_t kTerrainDebugEncodeBacklog = 4U;

inline constexpr std::array<DebugViewName, 53> kDebugViewNames{
    DebugViewName{TerrainDebugView::Final, "final"},
    DebugViewName{TerrainDebugView::MountainRelief, "mountain-relief"},
    DebugViewName{TerrainDebugView::MountainProcessReview, "mountain-process-review"},
    DebugViewName{TerrainDebugView::Height, "height"},
    DebugViewName{TerrainDebugView::PreProcessHeight, "pre-process-height"},
    DebugViewName{TerrainDebugView::MountainProfileHeight, "mountain-profile-height"},
    DebugViewName{TerrainDebugView::Slope, "slope"},
    DebugViewName{TerrainDebugView::ErosionDelta, "erosion-delta"},
    DebugViewName{TerrainDebugView::GullyMask, "gully-mask"},
    DebugViewName{TerrainDebugView::CreaseProxy, "crease-proxy"},
    DebugViewName{TerrainDebugView::PostErosionHeight, "post-erosion-height"},
    DebugViewName{TerrainDebugView::ThermalErosionDelta, "thermal-erosion-delta"},
    DebugViewName{TerrainDebugView::TalusDeposition, "talus-deposition"},
    DebugViewName{TerrainDebugView::SlopeInstability, "slope-instability"},
    DebugViewName{TerrainDebugView::MountainRangeSpine, "mountain-range-spine"},
    DebugViewName{TerrainDebugView::MountainEnvelope, "mountain-envelope"},
    DebugViewName{TerrainDebugView::MountainMass, "mountain-mass"},
    DebugViewName{TerrainDebugView::MountainShoulder, "mountain-shoulder"},
    DebugViewName{TerrainDebugView::MountainSummitCore, "mountain-summit-core"},
    DebugViewName{TerrainDebugView::MountainSaddleGate, "mountain-saddle-gate"},
    DebugViewName{TerrainDebugView::MountainSupport, "mountain-support"},
    DebugViewName{TerrainDebugView::MountainRidgeHierarchy, "mountain-ridge-hierarchy"},
    DebugViewName{TerrainDebugView::RidgeSupport, "ridge-support"},
    DebugViewName{TerrainDebugView::MountainPeakCandidates, "mountain-peak-candidates"},
    DebugViewName{TerrainDebugView::MountainPeakAnchors, "mountain-peak-anchors"},
    DebugViewName{TerrainDebugView::MountainPeakProminence, "mountain-peak-prominence"},
    DebugViewName{TerrainDebugView::PeakSupport, "peak-support"},
    DebugViewName{TerrainDebugView::MountainRidgeSkeleton, "mountain-ridge-skeleton"},
    DebugViewName{TerrainDebugView::MountainRidgeInfluence, "mountain-ridge-influence"},
    DebugViewName{TerrainDebugView::MountainRidgeBody, "mountain-ridge-body"},
    DebugViewName{TerrainDebugView::MountainValleyFloor, "mountain-valley-floor"},
    DebugViewName{TerrainDebugView::MountainValleyIncision, "mountain-valley-incision"},
    DebugViewName{TerrainDebugView::MountainUplift, "mountain-uplift"},
    DebugViewName{TerrainDebugView::RidgeUplift, "ridge-uplift"},
    DebugViewName{TerrainDebugView::PeakUplift, "peak-uplift"},
    DebugViewName{TerrainDebugView::DrainagePotential, "drainage-potential"},
    DebugViewName{TerrainDebugView::RoutingFillDelta, "routing-fill-delta"},
    DebugViewName{TerrainDebugView::FlowDirection, "flow-direction"},
    DebugViewName{TerrainDebugView::FlowAccumulation, "flow-accumulation"},
    DebugViewName{TerrainDebugView::StreamOrder, "stream-order"},
    DebugViewName{TerrainDebugView::RiverMask, "river-mask"},
    DebugViewName{TerrainDebugView::RiverTrunk, "river-trunk"},
    DebugViewName{TerrainDebugView::Tributaries, "tributaries"},
    DebugViewName{TerrainDebugView::RiverGraphPlan, "river-graph-plan"},
    DebugViewName{TerrainDebugView::RiverGraphDischarge, "river-graph-discharge"},
    DebugViewName{TerrainDebugView::SinkMask, "sink-mask"},
    DebugViewName{TerrainDebugView::ChannelWidth, "channel-width"},
    DebugViewName{TerrainDebugView::ChannelIncision, "channel-incision"},
    DebugViewName{TerrainDebugView::ValleyIncision, "valley-incision"},
    DebugViewName{TerrainDebugView::Wetness, "wetness"},
    DebugViewName{TerrainDebugView::Deposition, "deposition"},
    DebugViewName{TerrainDebugView::Material, "material"},
    DebugViewName{TerrainDebugView::Vegetation, "vegetation"},
};

inline constexpr std::array<TerrainDebugView, 53> kTerrainDebugReviewViews{
    TerrainDebugView::Final,
    TerrainDebugView::MountainRelief,
    TerrainDebugView::MountainProcessReview,
    TerrainDebugView::Height,
    TerrainDebugView::PreProcessHeight,
    TerrainDebugView::MountainProfileHeight,
    TerrainDebugView::Slope,
    TerrainDebugView::ErosionDelta,
    TerrainDebugView::GullyMask,
    TerrainDebugView::CreaseProxy,
    TerrainDebugView::PostErosionHeight,
    TerrainDebugView::ThermalErosionDelta,
    TerrainDebugView::TalusDeposition,
    TerrainDebugView::SlopeInstability,
    TerrainDebugView::MountainRangeSpine,
    TerrainDebugView::MountainEnvelope,
    TerrainDebugView::MountainMass,
    TerrainDebugView::MountainShoulder,
    TerrainDebugView::MountainSummitCore,
    TerrainDebugView::MountainSaddleGate,
    TerrainDebugView::MountainSupport,
    TerrainDebugView::MountainRidgeHierarchy,
    TerrainDebugView::RidgeSupport,
    TerrainDebugView::MountainPeakCandidates,
    TerrainDebugView::MountainPeakAnchors,
    TerrainDebugView::MountainPeakProminence,
    TerrainDebugView::PeakSupport,
    TerrainDebugView::MountainRidgeSkeleton,
    TerrainDebugView::MountainRidgeInfluence,
    TerrainDebugView::MountainRidgeBody,
    TerrainDebugView::MountainValleyFloor,
    TerrainDebugView::MountainValleyIncision,
    TerrainDebugView::MountainUplift,
    TerrainDebugView::RidgeUplift,
    TerrainDebugView::PeakUplift,
    TerrainDebugView::DrainagePotential,
    TerrainDebugView::RoutingFillDelta,
    TerrainDebugView::FlowDirection,
    TerrainDebugView::FlowAccumulation,
    TerrainDebugView::StreamOrder,
    TerrainDebugView::RiverMask,
    TerrainDebugView::RiverTrunk,
    TerrainDebugView::Tributaries,
    TerrainDebugView::RiverGraphPlan,
    TerrainDebugView::RiverGraphDischarge,
    TerrainDebugView::SinkMask,
    TerrainDebugView::ChannelWidth,
    TerrainDebugView::ChannelIncision,
    TerrainDebugView::ValleyIncision,
    TerrainDebugView::Wetness,
    TerrainDebugView::Deposition,
    TerrainDebugView::Material,
    TerrainDebugView::Vegetation,
};

[[nodiscard]] bool name_matches(std::string_view value, std::string_view canonical) {
    if (value.size() != canonical.size()) {
        return false;
    }
    for (std::size_t index = 0; index < value.size(); ++index) {
        const char lhs = value[index] == '_' ? '-' : value[index];
        if (lhs != canonical[index]) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] std::uint8_t byte(float value) {
    return static_cast<std::uint8_t>(
        std::round(cubey::procedural::saturate(value) * 255.0F));
}

[[nodiscard]] Rgb lerp_rgb(Rgb a, Rgb b, float t) {
    return {
        .r = cubey::procedural::lerp(a.r, b.r, t),
        .g = cubey::procedural::lerp(a.g, b.g, t),
        .b = cubey::procedural::lerp(a.b, b.b, t),
    };
}

void put_pixel(std::vector<std::uint8_t>& pixels, std::size_t index, Rgb color) {
    pixels[index + 0U] = byte(color.r);
    pixels[index + 1U] = byte(color.g);
    pixels[index + 2U] = byte(color.b);
    pixels[index + 3U] = 255U;
}

[[nodiscard]] FieldNormalization
make_field_normalization(const cubey::procedural::ScalarField2D& field, bool log_scale) {
    const cubey::procedural::ScalarFieldStats stats = field.summarize();
    if (stats.span <= 0.0F) {
        return {.log_scale = log_scale};
    }
    const float min_value = log_scale ? std::log1p(std::max(stats.min, 0.0F)) : stats.min;
    const float max_value = log_scale ? std::log1p(std::max(stats.max, 0.0F)) : stats.max;
    return {
        .min_value = min_value,
        .max_value = max_value,
        .log_scale = log_scale,
    };
}

[[nodiscard]] float normalized_field_value(const cubey::procedural::ScalarField2D& field,
                                           std::uint32_t x, std::uint32_t y,
                                           const FieldNormalization& normalization) {
    if (normalization.min_value == normalization.max_value) {
        return 0.0F;
    }
    const float value = normalization.log_scale ? std::log1p(std::max(field.at(x, y), 0.0F))
                                                : field.at(x, y);
    return cubey::procedural::saturate((value - normalization.min_value) /
                                       (normalization.max_value - normalization.min_value));
}

[[nodiscard]] Rgb terrain_ramp(float value) {
    const Rgb low{0.20F, 0.18F, 0.13F};
    const Rgb mid{0.38F, 0.48F, 0.28F};
    const Rgb high{0.72F, 0.70F, 0.62F};
    if (value < 0.55F) {
        return lerp_rgb(low, mid, value / 0.55F);
    }
    return lerp_rgb(mid, high, (value - 0.55F) / 0.45F);
}

[[nodiscard]] Rgb mountain_relief_ramp(float value) {
    const Rgb low{0.15F, 0.18F, 0.13F};
    const Rgb foothill{0.34F, 0.42F, 0.27F};
    const Rgb highland{0.58F, 0.54F, 0.40F};
    const Rgb summit{0.88F, 0.86F, 0.74F};
    if (value < 0.44F) {
        return lerp_rgb(low, foothill, value / 0.44F);
    }
    if (value < 0.76F) {
        return lerp_rgb(foothill, highland, (value - 0.44F) / 0.32F);
    }
    return lerp_rgb(highland, summit, (value - 0.76F) / 0.24F);
}

[[nodiscard]] Rgb scalar_color(const cubey::procedural::ScalarField2D& field, std::uint32_t x,
                               std::uint32_t y,
                               const FieldNormalization& normalization) {
    const float value = normalized_field_value(field, x, y, normalization);
    return lerp_rgb(Rgb{0.04F, 0.07F, 0.12F}, Rgb{0.95F, 0.86F, 0.45F}, value);
}

[[nodiscard]] ReviewPanelSample review_panel_sample(const cubey::procedural::Grid2DDesc& desc,
                                                    std::uint32_t x, std::uint32_t y) {
    constexpr std::uint32_t kColumns = 3U;
    constexpr std::uint32_t kRows = 3U;
    const std::uint32_t visual_y = desc.height - 1U - y;
    const std::uint32_t column = std::min(kColumns - 1U, (x * kColumns) / desc.width);
    const std::uint32_t row = std::min(kRows - 1U, (visual_y * kRows) / desc.height);
    const std::uint32_t start_x = (column * desc.width) / kColumns;
    const std::uint32_t end_x = ((column + 1U) * desc.width) / kColumns;
    const std::uint32_t start_y = (row * desc.height) / kRows;
    const std::uint32_t end_y = ((row + 1U) * desc.height) / kRows;
    const std::uint32_t panel_width = std::max(1U, end_x - start_x);
    const std::uint32_t panel_height = std::max(1U, end_y - start_y);
    const std::uint32_t local_x = x - start_x;
    const std::uint32_t local_visual_y = visual_y - start_y;
    const std::uint32_t sample_x =
        std::min(desc.width - 1U, (local_x * desc.width) / panel_width);
    const std::uint32_t sample_visual_y =
        std::min(desc.height - 1U, (local_visual_y * desc.height) / panel_height);
    const bool separator = ((column > 0U) && (x == start_x)) ||
                           ((row > 0U) && (visual_y == start_y));
    return {
        .panel_index = static_cast<std::size_t>((row * kColumns) + column),
        .x = sample_x,
        .y = desc.height - 1U - sample_visual_y,
        .separator = separator,
    };
}

[[nodiscard]] float hillshade(const cubey::procedural::ScalarField2D& height, std::uint32_t x,
                              std::uint32_t y);

[[nodiscard]] Rgb height_review_color(const cubey::procedural::ScalarField2D& height,
                                      std::uint32_t x, std::uint32_t y,
                                      const FieldNormalization& normalization) {
    const float value = std::pow(normalized_field_value(height, x, y, normalization), 0.88F);
    Rgb color = mountain_relief_ramp(value);
    const float shade = hillshade(height, x, y);
    color.r *= shade;
    color.g *= shade;
    color.b *= shade;
    return color;
}

[[nodiscard]] Rgb mountain_process_review_color(
    const TerrainRegionProduct& product, std::uint32_t x, std::uint32_t y,
    const MountainProcessReviewNormalization& normalization) {
    const cubey::procedural::Grid2DDesc& desc = product.fields.desc();
    const ReviewPanelSample sample = review_panel_sample(desc, x, y);
    if (sample.separator) {
        return Rgb{0.02F, 0.025F, 0.03F};
    }

    switch (sample.panel_index) {
    case 0U:
        return height_review_color(
            terrain_product_field(product, kTerrainFieldMountainProfileHeightM), sample.x,
            sample.y, normalization.profile_height);
    case 1U:
        return scalar_color(terrain_product_field(product, kTerrainFieldMountainRidgeBody),
                            sample.x, sample.y, normalization.ridge_body);
    case 2U:
        return scalar_color(terrain_product_field(product, kTerrainFieldMountainValleyFloor),
                            sample.x, sample.y, normalization.valley_floor);
    case 3U:
        return scalar_color(
            terrain_product_field(product, kTerrainFieldMountainValleyIncisionM), sample.x,
            sample.y, normalization.valley_incision);
    case 4U:
        return height_review_color(terrain_product_field(product, kTerrainFieldHeightM),
                                   sample.x, sample.y, normalization.height);
    case 5U:
        return height_review_color(
            terrain_product_field(product, kTerrainFieldPostErosionHeightM), sample.x,
            sample.y, normalization.post_erosion_height);
    case 6U:
        return scalar_color(terrain_product_field(product, kTerrainFieldSlopeInstability),
                            sample.x, sample.y, normalization.slope_instability);
    case 7U:
        return scalar_color(terrain_product_field(product, kTerrainFieldThermalErosionDeltaM),
                            sample.x, sample.y, normalization.thermal_erosion_delta);
    case 8U:
        return scalar_color(terrain_product_field(product, kTerrainFieldTalusDepositionM),
                            sample.x, sample.y, normalization.talus_deposition);
    default:
        return Rgb{};
    }
}

[[nodiscard]] Rgb final_color(const TerrainRegionProduct& product, std::uint32_t x,
                              std::uint32_t y,
                              const FieldNormalization& height_normalization) {
    const auto& height = terrain_product_field(product, kTerrainFieldHeightM);
    const auto& slope = terrain_product_field(product, kTerrainFieldSlope);
    const auto& rock = terrain_product_field(product, kTerrainFieldMaterialRock);
    const auto& soil = terrain_product_field(product, kTerrainFieldMaterialSoil);
    const auto& grass = terrain_product_field(product, kTerrainFieldMaterialGrass);
    const auto& river = terrain_product_field(product, kTerrainFieldRiverMask);
    const float h = normalized_field_value(height, x, y, height_normalization);
    const float shade = 1.0F - (cubey::procedural::smoothstep(0.06F, 0.46F, slope.at(x, y)) * 0.34F);
    const Rgb terrain = terrain_ramp(h);
    Rgb color{
        .r = ((rock.at(x, y) * 0.50F) + (soil.at(x, y) * 0.38F) +
              (grass.at(x, y) * 0.20F) + (terrain.r * 0.35F)) *
             shade,
        .g = ((rock.at(x, y) * 0.48F) + (soil.at(x, y) * 0.34F) +
              (grass.at(x, y) * 0.52F) + (terrain.g * 0.35F)) *
             shade,
        .b = ((rock.at(x, y) * 0.44F) + (soil.at(x, y) * 0.22F) +
              (grass.at(x, y) * 0.22F) + (terrain.b * 0.35F)) *
             shade,
    };
    color = lerp_rgb(color, Rgb{0.06F, 0.22F, 0.48F}, river.at(x, y) * 0.82F);
    return color;
}

[[nodiscard]] float field_at_clamped(const cubey::procedural::ScalarField2D& field, int x, int y) {
    const cubey::procedural::Grid2DDesc& desc = field.desc();
    const int clamped_x = std::clamp(x, 0, static_cast<int>(desc.width) - 1);
    const int clamped_y = std::clamp(y, 0, static_cast<int>(desc.height) - 1);
    return field.at(static_cast<std::uint32_t>(clamped_x),
                    static_cast<std::uint32_t>(clamped_y));
}

[[nodiscard]] float hillshade(const cubey::procedural::ScalarField2D& height, std::uint32_t x,
                              std::uint32_t y) {
    const float cell_size = std::max(height.desc().cell_size, 1.0F);
    const int ix = static_cast<int>(x);
    const int iy = static_cast<int>(y);
    const float dzdx =
        (field_at_clamped(height, ix + 1, iy) - field_at_clamped(height, ix - 1, iy)) /
        (cell_size * 2.0F);
    const float dzdy =
        (field_at_clamped(height, ix, iy + 1) - field_at_clamped(height, ix, iy - 1)) /
        (cell_size * 2.0F);

    constexpr float kVerticalExaggeration = 0.72F;
    const float nx = -dzdx * kVerticalExaggeration;
    const float ny = -dzdy * kVerticalExaggeration;
    constexpr float nz = 1.0F;
    const float inv_length = 1.0F / std::sqrt((nx * nx) + (ny * ny) + (nz * nz));

    constexpr float kLightX = -0.38F;
    constexpr float kLightY = -0.44F;
    constexpr float kLightZ = 0.82F;
    const float lit = std::max(0.0F, ((nx * inv_length) * kLightX) +
                                        ((ny * inv_length) * kLightY) +
                                        ((nz * inv_length) * kLightZ));
    return 0.74F + (lit * 0.30F);
}

[[nodiscard]] Rgb mountain_relief_color(const TerrainRegionProduct& product, std::uint32_t x,
                                        std::uint32_t y,
                                        const FieldNormalization& height_normalization,
                                        const FieldNormalization& relief_normalization) {
    const auto& height = terrain_product_field(product, kTerrainFieldHeightM);
    const auto& local_relief = terrain_product_field(product, kTerrainFieldLocalRelief);
    const auto& range_spine = terrain_product_field(product, kTerrainFieldMountainRangeSpine);
    const auto& envelope = terrain_product_field(product, kTerrainFieldMountainEnvelope);
    const auto& mountain_mass = terrain_product_field(product, kTerrainFieldMountainMass);
    const auto& shoulder = terrain_product_field(product, kTerrainFieldMountainShoulder);
    const auto& summit_core = terrain_product_field(product, kTerrainFieldMountainSummitCore);
    const auto& saddle_gate = terrain_product_field(product, kTerrainFieldMountainSaddleGate);
    const auto& mountain_support = terrain_product_field(product, kTerrainFieldMountainSupport);
    const auto& ridge_hierarchy =
        terrain_product_field(product, kTerrainFieldMountainRidgeHierarchy);
    const auto& ridge_support = terrain_product_field(product, kTerrainFieldRidgeSupport);
    const auto& peak_candidates =
        terrain_product_field(product, kTerrainFieldMountainPeakCandidates);
    const auto& peak_prominence =
        terrain_product_field(product, kTerrainFieldMountainPeakProminence);
    const auto& peak_support = terrain_product_field(product, kTerrainFieldPeakSupport);
    const auto& ridge_influence =
        terrain_product_field(product, kTerrainFieldMountainRidgeInfluence);

    const float h =
        std::pow(normalized_field_value(height, x, y, height_normalization), 0.88F);
    const float relief = normalized_field_value(local_relief, x, y, relief_normalization);
    const float spine = cubey::procedural::saturate(range_spine.at(x, y));
    const float envelope_value = cubey::procedural::saturate(envelope.at(x, y));
    const float mass = cubey::procedural::saturate(mountain_mass.at(x, y));
    const float shoulder_value = cubey::procedural::saturate(shoulder.at(x, y));
    const float summit = cubey::procedural::saturate(summit_core.at(x, y));
    const float saddle = cubey::procedural::saturate(saddle_gate.at(x, y));
    const float mountain = cubey::procedural::saturate(mountain_support.at(x, y));
    const float hierarchy = cubey::procedural::saturate(ridge_hierarchy.at(x, y));
    const float ridge = cubey::procedural::saturate(ridge_support.at(x, y));
    const float candidate = cubey::procedural::saturate(peak_candidates.at(x, y));
    const float prominence = cubey::procedural::saturate(peak_prominence.at(x, y));
    const float peak = cubey::procedural::saturate(peak_support.at(x, y));
    const float influence = cubey::procedural::saturate(ridge_influence.at(x, y));

    Rgb color = mountain_relief_ramp(h);
    color = lerp_rgb(color, Rgb{0.28F, 0.34F, 0.22F}, envelope_value * 0.10F);
    color = lerp_rgb(color, Rgb{0.34F, 0.40F, 0.26F}, mass * 0.10F);
    color = lerp_rgb(color, Rgb{0.48F, 0.43F, 0.28F}, shoulder_value * 0.10F);
    color = lerp_rgb(color, Rgb{0.26F, 0.28F, 0.22F}, saddle * 0.12F);
    color = lerp_rgb(color, Rgb{0.32F, 0.36F, 0.26F}, mountain * 0.10F);
    color = lerp_rgb(color, Rgb{0.48F, 0.46F, 0.34F}, spine * 0.05F);
    color = lerp_rgb(color, Rgb{0.70F, 0.64F, 0.46F}, influence * 0.12F);
    color = lerp_rgb(color, Rgb{0.78F, 0.71F, 0.52F}, hierarchy * 0.08F);
    color = lerp_rgb(color, Rgb{0.84F, 0.78F, 0.60F}, ridge * 0.10F);
    color = lerp_rgb(color, Rgb{0.90F, 0.84F, 0.68F}, candidate * 0.05F);
    color = lerp_rgb(color, Rgb{0.96F, 0.93F, 0.80F}, prominence * 0.18F);
    color = lerp_rgb(color, Rgb{0.98F, 0.95F, 0.84F}, summit * 0.14F);
    color = lerp_rgb(color, Rgb{0.98F, 0.96F, 0.88F}, peak * 0.24F);

    const float shade = hillshade(height, x, y);
    const float relief_boost =
        0.95F + (cubey::procedural::smoothstep(0.18F, 0.86F, relief) * 0.12F);
    color.r *= shade * relief_boost;
    color.g *= shade * relief_boost;
    color.b *= shade * relief_boost;
    return color;
}

[[nodiscard]] Rgb material_color(const TerrainRegionProduct& product, std::uint32_t x,
                                 std::uint32_t y) {
    const auto& rock = terrain_product_field(product, kTerrainFieldMaterialRock);
    const auto& soil = terrain_product_field(product, kTerrainFieldMaterialSoil);
    const auto& grass = terrain_product_field(product, kTerrainFieldMaterialGrass);
    return {
        .r = (rock.at(x, y) * 0.62F) + (soil.at(x, y) * 0.44F) + (grass.at(x, y) * 0.16F),
        .g = (rock.at(x, y) * 0.58F) + (soil.at(x, y) * 0.31F) + (grass.at(x, y) * 0.48F),
        .b = (rock.at(x, y) * 0.54F) + (soil.at(x, y) * 0.18F) + (grass.at(x, y) * 0.18F),
    };
}

[[nodiscard]] const cubey::procedural::ScalarField2D&
field_for_debug_view(const TerrainRegionProduct& product, TerrainDebugView view) {
    switch (view) {
    case TerrainDebugView::Height:
        return terrain_product_field(product, kTerrainFieldHeightM);
    case TerrainDebugView::PreProcessHeight:
        return terrain_product_field(product, kTerrainFieldPreProcessHeightM);
    case TerrainDebugView::MountainProfileHeight:
        return terrain_product_field(product, kTerrainFieldMountainProfileHeightM);
    case TerrainDebugView::Slope:
        return terrain_product_field(product, kTerrainFieldSlope);
    case TerrainDebugView::ErosionDelta:
        return terrain_product_field(product, kTerrainFieldErosionDeltaM);
    case TerrainDebugView::GullyMask:
        return terrain_product_field(product, kTerrainFieldGullyMask);
    case TerrainDebugView::CreaseProxy:
        return terrain_product_field(product, kTerrainFieldCreaseProxy);
    case TerrainDebugView::PostErosionHeight:
        return terrain_product_field(product, kTerrainFieldPostErosionHeightM);
    case TerrainDebugView::ThermalErosionDelta:
        return terrain_product_field(product, kTerrainFieldThermalErosionDeltaM);
    case TerrainDebugView::TalusDeposition:
        return terrain_product_field(product, kTerrainFieldTalusDepositionM);
    case TerrainDebugView::SlopeInstability:
        return terrain_product_field(product, kTerrainFieldSlopeInstability);
    case TerrainDebugView::MountainRangeSpine:
        return terrain_product_field(product, kTerrainFieldMountainRangeSpine);
    case TerrainDebugView::MountainEnvelope:
        return terrain_product_field(product, kTerrainFieldMountainEnvelope);
    case TerrainDebugView::MountainMass:
        return terrain_product_field(product, kTerrainFieldMountainMass);
    case TerrainDebugView::MountainShoulder:
        return terrain_product_field(product, kTerrainFieldMountainShoulder);
    case TerrainDebugView::MountainSummitCore:
        return terrain_product_field(product, kTerrainFieldMountainSummitCore);
    case TerrainDebugView::MountainSaddleGate:
        return terrain_product_field(product, kTerrainFieldMountainSaddleGate);
    case TerrainDebugView::MountainSupport:
        return terrain_product_field(product, kTerrainFieldMountainSupport);
    case TerrainDebugView::MountainRidgeHierarchy:
        return terrain_product_field(product, kTerrainFieldMountainRidgeHierarchy);
    case TerrainDebugView::RidgeSupport:
        return terrain_product_field(product, kTerrainFieldRidgeSupport);
    case TerrainDebugView::MountainPeakCandidates:
        return terrain_product_field(product, kTerrainFieldMountainPeakCandidates);
    case TerrainDebugView::MountainPeakAnchors:
        return terrain_product_field(product, kTerrainFieldMountainPeakAnchors);
    case TerrainDebugView::MountainPeakProminence:
        return terrain_product_field(product, kTerrainFieldMountainPeakProminence);
    case TerrainDebugView::PeakSupport:
        return terrain_product_field(product, kTerrainFieldPeakSupport);
    case TerrainDebugView::MountainRidgeSkeleton:
        return terrain_product_field(product, kTerrainFieldMountainRidgeSkeleton);
    case TerrainDebugView::MountainRidgeInfluence:
        return terrain_product_field(product, kTerrainFieldMountainRidgeInfluence);
    case TerrainDebugView::MountainRidgeBody:
        return terrain_product_field(product, kTerrainFieldMountainRidgeBody);
    case TerrainDebugView::MountainValleyFloor:
        return terrain_product_field(product, kTerrainFieldMountainValleyFloor);
    case TerrainDebugView::MountainValleyIncision:
        return terrain_product_field(product, kTerrainFieldMountainValleyIncisionM);
    case TerrainDebugView::MountainUplift:
        return terrain_product_field(product, kTerrainFieldMountainUplift);
    case TerrainDebugView::RidgeUplift:
        return terrain_product_field(product, kTerrainFieldRidgeUplift);
    case TerrainDebugView::PeakUplift:
        return terrain_product_field(product, kTerrainFieldPeakUplift);
    case TerrainDebugView::DrainagePotential:
        return terrain_product_field(product, kTerrainFieldDrainagePotential);
    case TerrainDebugView::RoutingFillDelta:
        return terrain_product_field(product, kTerrainFieldRoutingFillDelta);
    case TerrainDebugView::FlowDirection:
        return terrain_product_field(product, kTerrainFieldFlowDirection);
    case TerrainDebugView::FlowAccumulation:
        return terrain_product_field(product, kTerrainFieldFlowAccumulation);
    case TerrainDebugView::StreamOrder:
        return terrain_product_field(product, kTerrainFieldStreamOrder);
    case TerrainDebugView::RiverMask:
        return terrain_product_field(product, kTerrainFieldRiverMask);
    case TerrainDebugView::RiverTrunk:
        return terrain_product_field(product, kTerrainFieldRiverTrunk);
    case TerrainDebugView::Tributaries:
        return terrain_product_field(product, kTerrainFieldTributaries);
    case TerrainDebugView::RiverGraphPlan:
        return terrain_product_field(product, kTerrainFieldRiverGraphPlan);
    case TerrainDebugView::RiverGraphDischarge:
        return terrain_product_field(product, kTerrainFieldRiverGraphDischarge);
    case TerrainDebugView::SinkMask:
        return terrain_product_field(product, kTerrainFieldSinkMask);
    case TerrainDebugView::ChannelWidth:
        return terrain_product_field(product, kTerrainFieldChannelWidth);
    case TerrainDebugView::ChannelIncision:
        return terrain_product_field(product, kTerrainFieldChannelIncision);
    case TerrainDebugView::ValleyIncision:
        return terrain_product_field(product, kTerrainFieldValleyIncision);
    case TerrainDebugView::Wetness:
        return terrain_product_field(product, kTerrainFieldWetness);
    case TerrainDebugView::Deposition:
        return terrain_product_field(product, kTerrainFieldDeposition);
    case TerrainDebugView::Vegetation:
        return terrain_product_field(product, kTerrainFieldVegetationPotential);
    case TerrainDebugView::Final:
    case TerrainDebugView::MountainRelief:
    case TerrainDebugView::MountainProcessReview:
    case TerrainDebugView::Material:
        break;
    }
    throw std::runtime_error("terrain debug view does not map to a scalar field");
}

[[nodiscard]] std::vector<std::uint8_t> render_debug_view(const TerrainRegionProduct& product,
                                                          TerrainDebugView view) {
    const cubey::procedural::Grid2DDesc& desc = product.fields.desc();
    const cubey::procedural::ScalarField2D* scalar_field = nullptr;
    FieldNormalization scalar_normalization{};
    FieldNormalization height_normalization{};
    FieldNormalization relief_normalization{};
    MountainProcessReviewNormalization process_review_normalization{};
    if (view == TerrainDebugView::Final || view == TerrainDebugView::MountainRelief) {
        height_normalization =
            make_field_normalization(terrain_product_field(product, kTerrainFieldHeightM), false);
        if (view == TerrainDebugView::MountainRelief) {
            relief_normalization =
                make_field_normalization(terrain_product_field(product, kTerrainFieldLocalRelief),
                                         false);
        }
    } else if (view == TerrainDebugView::MountainProcessReview) {
        process_review_normalization = MountainProcessReviewNormalization{
            .profile_height = make_field_normalization(
                terrain_product_field(product, kTerrainFieldMountainProfileHeightM), false),
            .ridge_body = make_field_normalization(
                terrain_product_field(product, kTerrainFieldMountainRidgeBody), false),
            .valley_floor = make_field_normalization(
                terrain_product_field(product, kTerrainFieldMountainValleyFloor), false),
            .valley_incision = make_field_normalization(
                terrain_product_field(product, kTerrainFieldMountainValleyIncisionM), false),
            .height =
                make_field_normalization(terrain_product_field(product, kTerrainFieldHeightM),
                                         false),
            .post_erosion_height = make_field_normalization(
                terrain_product_field(product, kTerrainFieldPostErosionHeightM), false),
            .slope_instability = make_field_normalization(
                terrain_product_field(product, kTerrainFieldSlopeInstability), false),
            .thermal_erosion_delta = make_field_normalization(
                terrain_product_field(product, kTerrainFieldThermalErosionDeltaM), false),
            .talus_deposition = make_field_normalization(
                terrain_product_field(product, kTerrainFieldTalusDepositionM), false),
        };
    } else if (view != TerrainDebugView::Material) {
        scalar_field = &field_for_debug_view(product, view);
        scalar_normalization =
            make_field_normalization(*scalar_field, view == TerrainDebugView::FlowAccumulation);
    }

    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(desc.width) *
                                     static_cast<std::size_t>(desc.height) * 4U);
    for (std::uint32_t y = 0; y < desc.height; ++y) {
        for (std::uint32_t x = 0; x < desc.width; ++x) {
            Rgb color{};
            if (view == TerrainDebugView::Final) {
                color = final_color(product, x, y, height_normalization);
            } else if (view == TerrainDebugView::MountainRelief) {
                color = mountain_relief_color(product, x, y, height_normalization,
                                              relief_normalization);
            } else if (view == TerrainDebugView::MountainProcessReview) {
                color = mountain_process_review_color(product, x, y,
                                                      process_review_normalization);
            } else if (view == TerrainDebugView::Material) {
                color = material_color(product, x, y);
            } else {
                color = scalar_color(*scalar_field, x, y, scalar_normalization);
            }
            const std::size_t flipped_y = static_cast<std::size_t>(desc.height - 1U - y);
            const std::size_t index =
                ((flipped_y * static_cast<std::size_t>(desc.width)) + x) * 4U;
            put_pixel(pixels, index, color);
        }
    }
    return pixels;
}

[[nodiscard]] std::string hex_u64(std::uint64_t value) {
    std::ostringstream stream;
    stream << "0x" << std::hex << std::setw(16) << std::setfill('0') << value;
    return stream.str();
}

[[nodiscard]] nlohmann::json
stats_to_json(const cubey::procedural::ScalarFieldStats& stats) {
    return nlohmann::json{
        {"sample_count", stats.sample_count},
        {"min", stats.min},
        {"max", stats.max},
        {"span", stats.span},
        {"mean", stats.mean},
    };
}

[[nodiscard]] nlohmann::json grid_to_json(const cubey::procedural::Grid2DDesc& desc) {
    return nlohmann::json{
        {"width", desc.width},
        {"height", desc.height},
        {"cell_size_m", desc.cell_size},
        {"origin_x_m", desc.origin_x},
        {"origin_y_m", desc.origin_y},
    };
}

[[nodiscard]] nlohmann::json make_terrain_debug_manifest(const TerrainRegionProduct& product) {
    const TerrainRegionConfig& config = product.config;
    const cubey::procedural::Grid2DDesc& desc = product.fields.desc();

    nlohmann::json fields = nlohmann::json::object();
    for (const std::string& field_name : product.fields.field_names()) {
        fields[field_name] = stats_to_json(product.fields.summarize_field(field_name));
    }

    nlohmann::json views = nlohmann::json::array();
    nlohmann::json outputs = nlohmann::json::array();
    for (const TerrainDebugView view : terrain_debug_review_views()) {
        const std::string name(terrain_debug_view_name(view));
        views.push_back(name);
        outputs.push_back(name + ".png");
    }

    return nlohmann::json{
        {"schema", "cubey.terrain.scalar_capture.v1"},
        {"recipe_id", config.recipe_id},
        {"generator_revision", config.generator_revision},
        {"seed", config.seed},
        {"seed_hex", hex_u64(config.seed)},
        {"grid", grid_to_json(desc)},
        {"summary",
         {{"height", stats_to_json(product.summary.height)},
          {"slope", stats_to_json(product.summary.slope)},
          {"wetness", stats_to_json(product.summary.wetness)},
          {"river_coverage", product.summary.river_coverage},
          {"max_channel_width_m", product.summary.max_channel_width_m},
          {"content_hash", product.summary.content_hash},
          {"content_hash_hex", hex_u64(product.summary.content_hash)}}},
        {"field_count", product.fields.field_count()},
        {"fields", std::move(fields)},
        {"views", std::move(views)},
        {"outputs", std::move(outputs)},
    };
}

} // namespace

std::string_view terrain_debug_view_name(TerrainDebugView view) {
    for (const DebugViewName entry : kDebugViewNames) {
        if (entry.view == view) {
            return entry.name;
        }
    }
    return "final";
}

TerrainDebugView terrain_debug_view_from_name(std::string_view name) {
    if (name.empty()) {
        return TerrainDebugView::Final;
    }
    for (const DebugViewName entry : kDebugViewNames) {
        if (name_matches(name, entry.name)) {
            return entry.view;
        }
    }
    throw std::runtime_error("unknown terrain debug view: " + std::string(name));
}

std::span<const TerrainDebugView> terrain_debug_review_views() {
    return kTerrainDebugReviewViews;
}

CaptureTicket enqueue_terrain_debug_png(CaptureQueue& captures, const TerrainRegionProduct& product,
                                        TerrainDebugView view,
                                        const std::filesystem::path& output_path) {
    if (output_path.empty()) {
        throw std::runtime_error("terrain debug PNG output path must be non-empty");
    }
    if (output_path.has_parent_path()) {
        std::filesystem::create_directories(output_path.parent_path());
    }
    const cubey::procedural::Grid2DDesc& desc = product.fields.desc();
    std::vector<std::uint8_t> pixels = render_debug_view(product, view);
    return captures.enqueue_png({
        .output_path = output_path,
        .width = desc.width,
        .height = desc.height,
        .rgba8 = std::move(pixels),
    });
}

void write_terrain_debug_png(const TerrainRegionProduct& product, TerrainDebugView view,
                             const std::filesystem::path& output_path) {
    cubey::jobs::InlineExecutor encode_jobs;
    CaptureQueue captures(encode_jobs);
    CaptureTicket ticket = enqueue_terrain_debug_png(captures, product, view, output_path);
    ticket.finish();
}

void write_terrain_debug_manifest(const TerrainRegionProduct& product,
                                  const std::filesystem::path& output_dir) {
    if (output_dir.empty()) {
        throw std::runtime_error("terrain debug manifest output directory must be non-empty");
    }
    std::filesystem::create_directories(output_dir);
    const std::filesystem::path output_path = output_dir / "manifest.json";
    std::ofstream output(output_path);
    if (!output) {
        throw std::runtime_error("failed to open terrain debug manifest output: " +
                                 output_path.string());
    }
    output << make_terrain_debug_manifest(product).dump(2) << '\n';
}

void write_terrain_debug_review_pngs(const TerrainRegionProduct& product,
                                     const std::filesystem::path& output_dir) {
    if (output_dir.empty()) {
        throw std::runtime_error("terrain debug PNG output directory must be non-empty");
    }
    std::filesystem::create_directories(output_dir);
    cubey::jobs::JobSystem encode_jobs(kTerrainDebugEncodeWorkerCount);
    CaptureQueue captures(encode_jobs);
    std::deque<CaptureTicket> pending_tickets;
    for (const TerrainDebugView view : terrain_debug_review_views()) {
        const std::filesystem::path output_path =
            output_dir / (std::string(terrain_debug_view_name(view)) + ".png");
        pending_tickets.push_back(enqueue_terrain_debug_png(captures, product, view, output_path));
        if (pending_tickets.size() >= kTerrainDebugEncodeBacklog) {
            pending_tickets.front().finish();
            pending_tickets.pop_front();
        }
    }
    while (!pending_tickets.empty()) {
        pending_tickets.front().finish();
        pending_tickets.pop_front();
    }
    write_terrain_debug_manifest(product, output_dir);
}

} // namespace cubey::projects::terrain
