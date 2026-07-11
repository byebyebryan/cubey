#pragma once

#include <cubey/procedural/field_2d.h>
#include <cubey/procedural/field_set_2d.h>
#include <cubey/procedural/patch_domain.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace cubey::projects::terrain_hydrology_lab {

inline constexpr std::string_view kTerrainRecipeUplandCatchmentV1 = "upland-catchment-v1";
inline constexpr std::uint32_t kTerrainUplandCatchmentRevision = 2U;
inline constexpr std::string_view kTerrainRecipeUplandBroadNoiseControlV1 =
    "upland-broad-noise-control-v1";
inline constexpr std::uint32_t kTerrainUplandBroadNoiseControlRevision = 1U;
inline constexpr std::string_view kTerrainRecipeUplandLandscapeEvolutionV1 =
    "upland-landscape-evolution-v1";
inline constexpr std::uint32_t kTerrainUplandLandscapeEvolutionRevision = 1U;
inline constexpr std::uint32_t kTerrainDefaultGridSize = 257U;
inline constexpr std::uint32_t kTerrainProcessHaloSamples = 32U;
inline constexpr std::uint32_t kTerrainLandscapeProcessHaloSamples = 64U;
inline constexpr float kTerrainDefaultCellSizeM = 32.0F;
inline constexpr std::uint64_t kTerrainDefaultSeed = 0x7465'7272'6169'6e01ULL;

inline constexpr std::string_view kTerrainFieldSourceHeightM = "source_height_m";
inline constexpr std::string_view kTerrainFieldMountainSupport = "mountain_support";
inline constexpr std::string_view kTerrainFieldUpliftPotential = "uplift_potential";
inline constexpr std::string_view kTerrainFieldMacroMass = "macro_mass";
inline constexpr std::string_view kTerrainFieldBaseReliefM = "base_relief_m";
inline constexpr std::string_view kTerrainFieldUpliftRateMPerYear = "uplift_rate_m_per_year";
inline constexpr std::string_view kTerrainFieldProcessDrainageAreaM2 = "process_drainage_area_m2";
inline constexpr std::string_view kTerrainFieldProcessFlowDirectionX = "process_flow_direction_x";
inline constexpr std::string_view kTerrainFieldProcessFlowDirectionZ = "process_flow_direction_z";
inline constexpr std::string_view kTerrainFieldProcessBreachMask = "process_breach_mask";
inline constexpr std::string_view kTerrainFieldFluvialAdvectionRateMPerYear =
    "fluvial_advection_rate_m_per_year";
inline constexpr std::string_view kTerrainFieldHillslopeAdvectionRateMPerYear =
    "hillslope_advection_rate_m_per_year";
inline constexpr std::string_view kTerrainFieldThermalActiveMask = "thermal_active_mask";
inline constexpr std::string_view kTerrainFieldAnalyticalHeightM = "analytical_height_m";
inline constexpr std::string_view kTerrainFieldAltitudeCorrectionDeltaM =
    "altitude_correction_delta_m";
inline constexpr std::string_view kTerrainFieldProcessDeltaM = "process_delta_m";
inline constexpr std::string_view kTerrainFieldHeightM = "height_m";
inline constexpr std::string_view kTerrainFieldSlope = "slope";
inline constexpr std::string_view kTerrainFieldCurvature = "curvature";
inline constexpr std::string_view kTerrainFieldLocalReliefM = "local_relief_m";
inline constexpr std::string_view kTerrainFieldRoutingSurfaceM = "routing_surface_m";
inline constexpr std::string_view kTerrainFieldRoutingFillDeltaM = "routing_fill_delta_m";
inline constexpr std::string_view kTerrainFieldFlowDirectionX = "flow_direction_x";
inline constexpr std::string_view kTerrainFieldFlowDirectionZ = "flow_direction_z";
inline constexpr std::string_view kTerrainFieldContributingAreaM2 = "contributing_area_m2";
inline constexpr std::string_view kTerrainFieldStreamOrder = "stream_order";
inline constexpr std::string_view kTerrainFieldDischargeProxy = "discharge_proxy";
inline constexpr std::string_view kTerrainFieldSinkMask = "sink_mask";
inline constexpr std::string_view kTerrainFieldFlowBoundaryMask = "flow_boundary_mask";

struct TerrainPatchRequest {
    cubey::procedural::PatchDomain2D domain{};
    std::string recipe_id{std::string(kTerrainRecipeUplandCatchmentV1)};
    std::uint32_t generator_revision = kTerrainUplandCatchmentRevision;
};

struct TerrainFieldSummary {
    std::string name{};
    cubey::procedural::ScalarFieldStats stats{};
};

struct TerrainPatchSummary {
    std::vector<TerrainFieldSummary> fields{};
    std::uint64_t content_hash = 0U;
};

struct TerrainPatchProduct {
    TerrainPatchRequest request{};
    cubey::procedural::FieldSet2D fields{};
    TerrainPatchSummary summary{};
};

[[nodiscard]] TerrainPatchRequest default_terrain_patch_request();
[[nodiscard]] std::uint32_t terrain_generator_revision_for_recipe(std::string_view recipe_id);
void validate_terrain_patch_request(const TerrainPatchRequest& request);
[[nodiscard]] TerrainPatchProduct generate_terrain_patch(const TerrainPatchRequest& request);

} // namespace cubey::projects::terrain_hydrology_lab
