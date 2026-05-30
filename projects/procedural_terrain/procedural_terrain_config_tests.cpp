#include "procedural_terrain_config.h"
#include "procedural_terrain_fields.h"

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
    namespace terrain = cubey::projects::procedural_terrain;

    const terrain::TerrainConfig defaults{};
    require(defaults.grid_width == terrain::kTerrainDefaultGridWidth,
            "terrain default width should be stable");
    require(defaults.grid_height == terrain::kTerrainDefaultGridHeight,
            "terrain default height should be stable");
    require_near(defaults.land_extent, terrain::kTerrainDefaultLandExtent, 0.001F,
                 "terrain default land extent should be stable");
    require_near(defaults.relief_scale, terrain::kTerrainDefaultReliefScale, 0.001F,
                 "terrain default relief scale should be stable");
    terrain::validate_terrain_config(defaults);

    require(terrain::terrain_debug_view_from_name("") == terrain::TerrainDebugView::Final,
            "empty terrain debug view should use final");
    require(terrain::terrain_debug_view_from_name("height") == terrain::TerrainDebugView::Height,
            "height terrain debug view should parse");
    require(terrain::terrain_debug_view_from_name("water_depth") ==
                terrain::TerrainDebugView::WaterDepth,
            "water depth terrain debug view should parse");
    require(terrain::terrain_debug_view_from_name("shoreline") ==
                terrain::TerrainDebugView::Shoreline,
            "shoreline terrain debug view should parse");
    require(terrain::terrain_debug_view_from_name("material") ==
                terrain::TerrainDebugView::Material,
            "material terrain debug view should parse");
    require(terrain::terrain_debug_view_from_name("slope") == terrain::TerrainDebugView::Slope,
            "slope terrain debug view should parse");
    require(terrain::next_terrain_debug_view(terrain::TerrainDebugView::Slope) ==
                terrain::TerrainDebugView::Final,
            "terrain debug view cycle should wrap");

    bool rejected = false;
    try {
        static_cast<void>(terrain::terrain_debug_view_from_name("bathymetry"));
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    require(rejected, "terrain should reject unknown debug views");

    cubey::RunConfig run_config;
    run_config.grid.width = 65;
    run_config.grid.height = 33;
    run_config.debug_view = "shoreline";
    const terrain::TerrainConfig from_run_config =
        terrain::terrain_config_from_run_config(run_config);
    require(from_run_config.grid_width == 65U, "terrain should read grid width from run config");
    require(from_run_config.grid_height == 33U, "terrain should read grid height from run config");
    require(from_run_config.debug_view == terrain::TerrainDebugView::Shoreline,
            "terrain should read debug view from run config");

    terrain::TerrainConfig invalid = defaults;
    invalid.grid_width = terrain::kTerrainMinGridExtent - 1U;
    rejected = false;
    try {
        terrain::validate_terrain_config(invalid);
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    require(rejected, "terrain should reject too-small grids");

    invalid = defaults;
    invalid.land_extent = 0.1F;
    rejected = false;
    try {
        terrain::validate_terrain_config(invalid);
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    require(rejected, "terrain should reject invalid land extent");

    invalid = defaults;
    invalid.relief_scale = 0.0F;
    rejected = false;
    try {
        terrain::validate_terrain_config(invalid);
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    require(rejected, "terrain should reject invalid relief scale");

    terrain::TerrainConfig small = defaults;
    small.grid_width = 33;
    small.grid_height = 33;
    const terrain::TerrainFieldData fields = terrain::generate_terrain_fields(small);
    require(fields.sample_count() == 33U * 33U, "terrain fields should match grid dimensions");
    require(fields.height_m.size() == fields.sample_count(), "terrain height field size mismatch");
    require(fields.water_depth_m.size() == fields.sample_count(),
            "terrain water depth field size mismatch");
    require(fields.shore_sdf_m.size() == fields.sample_count(),
            "terrain shoreline field size mismatch");
    require(fields.material_masks.size() == fields.sample_count(),
            "terrain material field size mismatch");
    require(fields.min_height_m < 0.0F, "terrain should include underwater terrain");
    require(fields.max_height_m > 0.0F, "terrain should include land terrain");
    require(fields.max_water_depth_m > 0.0F, "terrain should include positive water depth");
    require(fields.max_abs_shore_sdf_m > 0.0F, "terrain should include shoreline distance");

    for (std::size_t index = 0; index < fields.sample_count(); ++index) {
        require(std::isfinite(fields.height_m[index]), "terrain height should be finite");
        require(std::isfinite(fields.water_depth_m[index]), "terrain water depth should be finite");
        require(std::isfinite(fields.shore_sdf_m[index]), "terrain shoreline should be finite");
        require(fields.water_depth_m[index] >= 0.0F, "terrain water depth should be positive");
        require_near(fields.water_depth_m[index],
                     std::max(0.0F, fields.desc.sea_level_m - fields.height_m[index]), 0.001F,
                     "terrain water depth should match sea level minus height");

        const terrain::TerrainMaterialMask mask = fields.material_masks[index];
        const float sum = mask.sand + mask.rock + mask.vegetation + mask.sediment;
        require_near(sum, 1.0F, 0.001F, "terrain material masks should be normalized");
    }
}
