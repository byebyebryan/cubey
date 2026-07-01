#pragma once

#include "terrain_config.h"

#include <cubey/procedural/field_set_2d.h>
#include <cubey/procedural/field_metadata.h>
#include <cubey/procedural/hash.h>

#include <cstdint>
#include <string_view>

namespace cubey::projects::terrain {

inline constexpr std::string_view kTerrainFieldHeightM = "height_m";
inline constexpr std::string_view kTerrainFieldPreProcessHeightM = "pre_process_height_m";
inline constexpr std::string_view kTerrainFieldBaseElevation = "base_elevation";
inline constexpr std::string_view kTerrainFieldBroadRelief = "broad_relief";
inline constexpr std::string_view kTerrainFieldMountainProfileHeightM =
    "mountain_profile_height_m";
inline constexpr std::string_view kTerrainFieldMountainRangeSpine = "mountain_range_spine";
inline constexpr std::string_view kTerrainFieldMountainEnvelope = "mountain_envelope";
inline constexpr std::string_view kTerrainFieldMountainMass = "mountain_mass";
inline constexpr std::string_view kTerrainFieldMountainShoulder = "mountain_shoulder";
inline constexpr std::string_view kTerrainFieldMountainSummitCore = "mountain_summit_core";
inline constexpr std::string_view kTerrainFieldMountainSaddleGate = "mountain_saddle_gate";
inline constexpr std::string_view kTerrainFieldMountainSupport = "mountain_support";
inline constexpr std::string_view kTerrainFieldMountainRidgeHierarchy =
    "mountain_ridge_hierarchy";
inline constexpr std::string_view kTerrainFieldRidgeSupport = "ridge_support";
inline constexpr std::string_view kTerrainFieldMountainPeakCandidates =
    "mountain_peak_candidates";
inline constexpr std::string_view kTerrainFieldMountainPeakAnchors = "mountain_peak_anchors";
inline constexpr std::string_view kTerrainFieldMountainPeakProminence =
    "mountain_peak_prominence";
inline constexpr std::string_view kTerrainFieldPeakSupport = "peak_support";
inline constexpr std::string_view kTerrainFieldMountainRidgeSkeleton =
    "mountain_ridge_skeleton";
inline constexpr std::string_view kTerrainFieldMountainRidgeInfluence =
    "mountain_ridge_influence";
inline constexpr std::string_view kTerrainFieldMountainUplift = "mountain_uplift";
inline constexpr std::string_view kTerrainFieldRidgeUplift = "ridge_uplift";
inline constexpr std::string_view kTerrainFieldPeakUplift = "peak_uplift";
inline constexpr std::string_view kTerrainFieldDetailResidual = "detail_residual";
inline constexpr std::string_view kTerrainFieldSlope = "slope";
inline constexpr std::string_view kTerrainFieldCurvature = "curvature";
inline constexpr std::string_view kTerrainFieldLocalRelief = "local_relief";
inline constexpr std::string_view kTerrainFieldErosionDeltaM = "erosion_delta_m";
inline constexpr std::string_view kTerrainFieldGullyMask = "gully_mask";
inline constexpr std::string_view kTerrainFieldCreaseProxy = "crease_proxy";
inline constexpr std::string_view kTerrainFieldPostErosionHeightM = "post_erosion_height_m";
inline constexpr std::string_view kTerrainFieldThermalErosionDeltaM =
    "thermal_erosion_delta_m";
inline constexpr std::string_view kTerrainFieldTalusDepositionM = "talus_deposition_m";
inline constexpr std::string_view kTerrainFieldSlopeInstability = "slope_instability";
inline constexpr std::string_view kTerrainFieldDrainagePotential = "drainage_potential";
inline constexpr std::string_view kTerrainFieldRoutingFillDelta = "routing_fill_delta";
inline constexpr std::string_view kTerrainFieldFlowDirection = "flow_direction";
inline constexpr std::string_view kTerrainFieldFlowAccumulation = "flow_accumulation";
inline constexpr std::string_view kTerrainFieldStreamOrder = "stream_order";
inline constexpr std::string_view kTerrainFieldRiverMask = "river_mask";
inline constexpr std::string_view kTerrainFieldRiverTrunk = "river_trunk";
inline constexpr std::string_view kTerrainFieldTributaries = "tributaries";
inline constexpr std::string_view kTerrainFieldRiverGraphPlan = "river_graph_plan";
inline constexpr std::string_view kTerrainFieldRiverGraphDischarge = "river_graph_discharge";
inline constexpr std::string_view kTerrainFieldSinkMask = "sink_mask";
inline constexpr std::string_view kTerrainFieldChannelWidth = "channel_width";
inline constexpr std::string_view kTerrainFieldValleyWidth = "valley_width";
inline constexpr std::string_view kTerrainFieldChannelIncision = "channel_incision";
inline constexpr std::string_view kTerrainFieldValleyIncision = "valley_incision";
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
