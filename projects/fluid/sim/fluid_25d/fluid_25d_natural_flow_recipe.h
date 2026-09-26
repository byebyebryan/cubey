#pragma once

#include "fluid_25d_scenarios.h"

#include <array>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace cubey::projects::fluid::fluid_25d {

inline constexpr std::string_view kFluid25DNaturalFlowRecipeSchema =
    "cubey.fluid25d.natural_flow_study.v1";
inline constexpr std::string_view kFluid25DHillsideFlowRecipeSchema =
    "cubey.fluid25d.hillside_flow_study.v1";
inline constexpr float kFluid25DNaturalFlowCellSizeM = 30.0F;
inline constexpr std::uint32_t kFluid25DNaturalFlowSourceCellCount = 5U;
inline constexpr std::uint32_t kFluid25DNaturalFlowMaximumGaugeHalfSpanCells = 10U;

struct Fluid25DNaturalFlowRecipe {
    // Schema identity is checked against the selected study before import.
    bool has_expected_outlet = true;
    std::string candidate_id{};
    std::string elevation_sha256{};
    std::string transformed_crop_sha256{};
    std::uint32_t crop_x = 0U;
    std::uint32_t crop_z = 0U;
    std::uint32_t width = 0U;
    std::uint32_t height = 0U;
    float cell_size_m = 0.0F;
    std::array<std::uint32_t, 2U> source_center_xz{};
    std::array<std::array<std::uint32_t, 2U>, kFluid25DNaturalFlowSourceCellCount>
        source_cells_xz{};
    Fluid25DNaturalFlowEdge expected_outlet_edge = Fluid25DNaturalFlowEdge::West;
    std::array<std::uint32_t, 2U> expected_outlet_min_xz{};
    std::array<std::uint32_t, 2U> expected_outlet_max_xz{};
    std::vector<Fluid25DNaturalFlowGauge> gauges{};
};

[[nodiscard]] Fluid25DNaturalFlowRecipe
parse_fluid_25d_natural_flow_recipe(std::string_view json_text);
[[nodiscard]] Fluid25DNaturalFlowRecipe
load_fluid_25d_natural_flow_recipe(const std::filesystem::path& path);
void validate_fluid_25d_natural_flow_recipe(const Fluid25DNaturalFlowRecipe& recipe,
                                            std::uint32_t grid_width, std::uint32_t grid_height,
                                            float cell_size_m);
void validate_fluid_25d_natural_flow_recipe_provenance(
    const Fluid25DNaturalFlowRecipe& recipe, const Fluid25DTerrainCaseProvenance& provenance,
    std::uint32_t grid_width, std::uint32_t grid_height, float cell_size_m);

[[nodiscard]] Fluid25DScenarioData make_fluid_25d_natural_flow_study_scenario(
    const Fluid25DConfig& config, const cubey::asset::TerrainRasterHeightSource& source,
    const Fluid25DNaturalFlowRecipe& recipe, std::uint32_t crop_x, std::uint32_t crop_z);

} // namespace cubey::projects::fluid::fluid_25d
