#include "terrain_lab_config.h"

#include <cubey/core/run_config.h>

#include <cmath>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void require_near(float value, float expected, float tolerance, const char* message) {
    require(value >= expected - tolerance && value <= expected + tolerance, message);
}

} // namespace

int main() {
    namespace terrain = cubey::projects::terrain_lab;

    const terrain::TerrainLabConfig defaults{};
    require(defaults.grid_width == terrain::kTerrainLabDefaultGridWidth,
            "terrain lab default width should be stable");
    require(defaults.grid_height == terrain::kTerrainLabDefaultGridHeight,
            "terrain lab default height should be stable");
    require(defaults.seed == terrain::kTerrainLabDefaultSeed,
            "terrain lab default seed should be stable");
    require_near(defaults.cell_size_m, terrain::kTerrainLabDefaultCellSizeMeters, 0.001F,
                 "terrain lab default cell size should be stable");
    require_near(defaults.elevation_scale_m, terrain::kTerrainLabDefaultElevationScaleMeters,
                 0.001F, "terrain lab default elevation scale should be stable");
    require(defaults.slice_preset == terrain::TerrainLabSlicePreset::TemperateMountainWatershed,
            "terrain lab should default to the watershed slice");
    require(defaults.debug_view == terrain::TerrainLabDebugView::Final,
            "terrain lab should default to final debug view");
    terrain::validate_terrain_lab_config(defaults);

    require(terrain::terrain_lab_slice_preset_from_name("") ==
                terrain::TerrainLabSlicePreset::TemperateMountainWatershed,
            "empty terrain lab slice preset should use watershed");
    require(terrain::terrain_lab_slice_preset_from_name("temperate-mountain-watershed") ==
                terrain::TerrainLabSlicePreset::TemperateMountainWatershed,
            "terrain lab slice preset should parse canonical name");
    require(terrain::terrain_lab_slice_preset_from_name("temperate_mountain_watershed") ==
                terrain::TerrainLabSlicePreset::TemperateMountainWatershed,
            "terrain lab slice preset should accept underscore alias");

    require(terrain::terrain_lab_debug_view_from_name("") == terrain::TerrainLabDebugView::Final,
            "empty terrain lab debug view should use final");
    require(terrain::terrain_lab_debug_view_from_name("height") ==
                terrain::TerrainLabDebugView::Height,
            "terrain lab height debug view should parse");
    require(terrain::terrain_lab_debug_view_from_name("flow-direction") ==
                terrain::TerrainLabDebugView::FlowDirection,
            "terrain lab flow direction debug view should parse");
    require(terrain::terrain_lab_debug_view_from_name("flow_direction") ==
                terrain::TerrainLabDebugView::FlowDirection,
            "terrain lab debug views should accept underscore aliases");
    require(terrain::terrain_lab_debug_view_from_name("flow-accumulation") ==
                terrain::TerrainLabDebugView::FlowAccumulation,
            "terrain lab flow accumulation debug view should parse");
    require(terrain::terrain_lab_debug_view_from_name("stream-power") ==
                terrain::TerrainLabDebugView::StreamPower,
            "terrain lab stream power debug view should parse");
    require(terrain::terrain_lab_debug_view_from_name("biome-density") ==
                terrain::TerrainLabDebugView::BiomeDensity,
            "terrain lab biome density debug view should parse");
    require(terrain::terrain_lab_debug_view_from_name("canopy-height") ==
                terrain::TerrainLabDebugView::CanopyHeight,
            "terrain lab canopy height debug view should parse");
    require(terrain::terrain_lab_debug_view_from_name("noise-off") ==
                terrain::TerrainLabDebugView::NoiseOff,
            "terrain lab noise-off debug view should parse");
    require(terrain::next_terrain_lab_debug_view(terrain::TerrainLabDebugView::Detail) ==
                terrain::TerrainLabDebugView::Slope,
            "terrain lab debug view cycle should enter geometry views after detail");
    require(terrain::next_terrain_lab_debug_view(terrain::TerrainLabDebugView::NoiseOff) ==
                terrain::TerrainLabDebugView::Final,
            "terrain lab debug view cycle should wrap");

    bool rejected = false;
    try {
        static_cast<void>(terrain::terrain_lab_debug_view_from_name("shoreline"));
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    require(rejected, "terrain lab should reject unknown debug views");

    rejected = false;
    try {
        static_cast<void>(terrain::terrain_lab_slice_preset_from_name("coastal-island"));
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    require(rejected, "terrain lab should reject unknown slice presets");

    cubey::RunConfig run_config;
    run_config.grid.width = 65;
    run_config.grid.height = 33;
    run_config.debug_view = "wetness";
    run_config.terrain.seed = 77U;
    run_config.terrain.seed_set = true;
    run_config.terrain.cell_size = 6.0F;
    run_config.terrain.relief = 1.25F;
    const terrain::TerrainLabConfig from_run_config =
        terrain::terrain_lab_config_from_run_config(run_config);
    require(from_run_config.grid_width == 65U,
            "terrain lab should read grid width from common run config");
    require(from_run_config.grid_height == 33U,
            "terrain lab should read grid height from common run config");
    require(from_run_config.debug_view == terrain::TerrainLabDebugView::Wetness,
            "terrain lab should read debug view from common run config");
    require(from_run_config.seed == terrain::kTerrainLabDefaultSeed,
            "terrain lab should not read coast-oriented terrain seed flags");
    require_near(from_run_config.cell_size_m, terrain::kTerrainLabDefaultCellSizeMeters, 0.001F,
                 "terrain lab should not read coast-oriented terrain cell-size flags");
    require_near(from_run_config.structure_strength, terrain::kTerrainLabDefaultStructureStrength,
                 0.001F, "terrain lab should not read coast-oriented terrain relief flags");

    terrain::TerrainLabConfig invalid = defaults;
    invalid.grid_width = terrain::kTerrainLabMinGridExtent - 1U;
    rejected = false;
    try {
        terrain::validate_terrain_lab_config(invalid);
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    require(rejected, "terrain lab should reject too-small grids");

    terrain::TerrainLabConfig large = defaults;
    large.grid_width = terrain::kTerrainLabMaxGridExtent;
    large.grid_height = terrain::kTerrainLabMaxGridExtent;
    terrain::validate_terrain_lab_config(large);

    invalid = defaults;
    invalid.grid_height = terrain::kTerrainLabMaxGridExtent + 1U;
    rejected = false;
    try {
        terrain::validate_terrain_lab_config(invalid);
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    require(rejected, "terrain lab should reject too-large grids");

    invalid = defaults;
    invalid.cell_size_m = 0.0F;
    rejected = false;
    try {
        terrain::validate_terrain_lab_config(invalid);
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    require(rejected, "terrain lab should reject nonpositive cell size");

    invalid = defaults;
    invalid.elevation_scale_m = std::nanf("");
    rejected = false;
    try {
        terrain::validate_terrain_lab_config(invalid);
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    require(rejected, "terrain lab should reject nonfinite elevation scale");

    invalid = defaults;
    invalid.structure_strength = -0.1F;
    rejected = false;
    try {
        terrain::validate_terrain_lab_config(invalid);
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    require(rejected, "terrain lab should reject invalid structure strength");

    invalid = defaults;
    invalid.process_strength = 4.5F;
    rejected = false;
    try {
        terrain::validate_terrain_lab_config(invalid);
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    require(rejected, "terrain lab should reject invalid process strength");

    invalid = defaults;
    invalid.detail_strength = -0.1F;
    rejected = false;
    try {
        terrain::validate_terrain_lab_config(invalid);
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    require(rejected, "terrain lab should reject invalid detail strength");

    return 0;
}
