#pragma once

#include "terrain_config.h"

#include <cubey/procedural/field_set_2d.h>
#include <cubey/procedural/field_metadata.h>
#include <cubey/procedural/hash.h>

#include <cstdint>
#include <string_view>

namespace cubey::projects::terrain {

inline constexpr std::string_view kTerrainFieldHeightM = "height_m";
inline constexpr std::string_view kTerrainFieldBaseElevation = "base_elevation";
inline constexpr std::string_view kTerrainFieldBroadRelief = "broad_relief";
inline constexpr std::string_view kTerrainFieldRidgeUplift = "ridge_uplift";
inline constexpr std::string_view kTerrainFieldDetailResidual = "detail_residual";
inline constexpr std::string_view kTerrainFieldSlope = "slope";
inline constexpr std::string_view kTerrainFieldCurvature = "curvature";
inline constexpr std::string_view kTerrainFieldLocalRelief = "local_relief";
inline constexpr std::string_view kTerrainFieldDrainagePotential = "drainage_potential";
inline constexpr std::string_view kTerrainFieldFlowDirection = "flow_direction";
inline constexpr std::string_view kTerrainFieldFlowAccumulation = "flow_accumulation";
inline constexpr std::string_view kTerrainFieldStreamOrder = "stream_order";
inline constexpr std::string_view kTerrainFieldRiverMask = "river_mask";
inline constexpr std::string_view kTerrainFieldRiverTrunk = "river_trunk";
inline constexpr std::string_view kTerrainFieldTributaries = "tributaries";
inline constexpr std::string_view kTerrainFieldSinkMask = "sink_mask";
inline constexpr std::string_view kTerrainFieldChannelWidth = "channel_width";
inline constexpr std::string_view kTerrainFieldValleyWidth = "valley_width";
inline constexpr std::string_view kTerrainFieldWetness = "wetness";
inline constexpr std::string_view kTerrainFieldDeposition = "deposition";
inline constexpr std::string_view kTerrainFieldMaterialRock = "material_rock";
inline constexpr std::string_view kTerrainFieldMaterialSoil = "material_soil";
inline constexpr std::string_view kTerrainFieldMaterialGrass = "material_grass";
inline constexpr std::string_view kTerrainFieldVegetationPotential = "vegetation_potential";

struct TerrainRegionSummary {
    cubey::procedural::ScalarFieldStats height{};
    cubey::procedural::ScalarFieldStats slope{};
    cubey::procedural::ScalarFieldStats wetness{};
    float river_coverage = 0.0F;
    float max_channel_width_m = 0.0F;
    std::uint64_t content_hash = cubey::procedural::kProceduralFnv1a64Offset;
};

struct TerrainRegionProduct {
    TerrainRegionConfig config{};
    cubey::procedural::FieldSet2D fields{};
    TerrainRegionSummary summary{};
};

[[nodiscard]] TerrainRegionProduct make_empty_terrain_region_product(
    const TerrainRegionConfig& config);
[[nodiscard]] bool terrain_product_has_field(const TerrainRegionProduct& product,
                                             std::string_view name);
[[nodiscard]] const cubey::procedural::ScalarField2D&
terrain_product_field(const TerrainRegionProduct& product, std::string_view name);
[[nodiscard]] TerrainRegionSummary summarize_terrain_region_product(
    const TerrainRegionConfig& config, const cubey::procedural::FieldSet2D& fields);

} // namespace cubey::projects::terrain
