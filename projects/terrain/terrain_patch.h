#pragma once

#include <cubey/procedural/field_2d.h>
#include <cubey/procedural/field_set_2d.h>
#include <cubey/procedural/patch_domain.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace cubey::projects::terrain {

inline constexpr std::string_view kTerrainRecipeUplandCatchmentV1 = "upland-catchment-v1";
inline constexpr std::uint32_t kTerrainGeneratorRevisionV1 = 1U;
inline constexpr std::uint32_t kTerrainDefaultGridSize = 257U;
inline constexpr std::uint32_t kTerrainProcessHaloSamples = 32U;
inline constexpr float kTerrainDefaultCellSizeM = 32.0F;
inline constexpr std::uint64_t kTerrainDefaultSeed = 0x7465'7272'6169'6e01ULL;

inline constexpr std::string_view kTerrainFieldSourceHeightM = "source_height_m";
inline constexpr std::string_view kTerrainFieldMountainSupport = "mountain_support";
inline constexpr std::string_view kTerrainFieldHeightM = "height_m";
inline constexpr std::string_view kTerrainFieldSlope = "slope";
inline constexpr std::string_view kTerrainFieldCurvature = "curvature";
inline constexpr std::string_view kTerrainFieldLocalReliefM = "local_relief_m";

struct TerrainPatchRequest {
    cubey::procedural::PatchDomain2D domain{};
    std::string recipe_id{std::string(kTerrainRecipeUplandCatchmentV1)};
    std::uint32_t generator_revision = kTerrainGeneratorRevisionV1;
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
void validate_terrain_patch_request(const TerrainPatchRequest& request);
[[nodiscard]] TerrainPatchProduct generate_terrain_patch(const TerrainPatchRequest& request);

} // namespace cubey::projects::terrain
