#include "procedural_terrain_config.h"
#include "procedural_terrain_fields.h"
#include "procedural_terrain_mesh.h"

#include <cubey/core/run_config.h>

#include <algorithm>
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
    require_near(defaults.valley_scale, terrain::kTerrainDefaultValleyScale, 0.001F,
                 "terrain default valley scale should be stable");
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
    require(terrain::terrain_debug_view_from_name("landform") ==
                terrain::TerrainDebugView::Landform,
            "landform terrain debug view should parse");
    require(terrain::terrain_debug_view_from_name("ridges") == terrain::TerrainDebugView::Ridges,
            "ridges terrain debug view should parse");
    require(terrain::terrain_debug_view_from_name("valleys") == terrain::TerrainDebugView::Valleys,
            "valleys terrain debug view should parse");
    require(terrain::next_terrain_debug_view(terrain::TerrainDebugView::Valleys) ==
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
    run_config.terrain.seed = 77;
    run_config.terrain.seed_set = true;
    run_config.terrain.cell_size = 6.0F;
    run_config.terrain.sea_level = 12.0F;
    run_config.terrain.land_extent = 0.62F;
    run_config.terrain.coast_noise = 0.25F;
    run_config.terrain.relief = 1.25F;
    run_config.terrain.ridges = 0.75F;
    run_config.terrain.valleys = 1.15F;
    const terrain::TerrainConfig from_run_config =
        terrain::terrain_config_from_run_config(run_config);
    require(from_run_config.grid_width == 65U, "terrain should read grid width from run config");
    require(from_run_config.grid_height == 33U, "terrain should read grid height from run config");
    require(from_run_config.seed == 77U, "terrain should read seed from run config");
    require_near(from_run_config.cell_size_m, 6.0F, 0.001F,
                 "terrain should read cell size from run config");
    require_near(from_run_config.sea_level_m, 12.0F, 0.001F,
                 "terrain should read sea level from run config");
    require_near(from_run_config.land_extent, 0.62F, 0.001F,
                 "terrain should read land extent from run config");
    require_near(from_run_config.coast_noise_strength, 0.25F, 0.001F,
                 "terrain should read coast noise from run config");
    require_near(from_run_config.relief_scale, 1.25F, 0.001F,
                 "terrain should read relief scale from run config");
    require_near(from_run_config.ridge_scale, 0.75F, 0.001F,
                 "terrain should read ridge scale from run config");
    require_near(from_run_config.valley_scale, 1.15F, 0.001F,
                 "terrain should read valley scale from run config");
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

    invalid = defaults;
    invalid.valley_scale = -0.1F;
    rejected = false;
    try {
        terrain::validate_terrain_config(invalid);
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    require(rejected, "terrain should reject invalid valley scale");

    terrain::TerrainConfig small = defaults;
    small.grid_width = 33;
    small.grid_height = 33;
    const terrain::TerrainFieldData fields = terrain::generate_terrain_fields(small);
    const terrain::TerrainMeshData mesh = terrain::make_terrain_mesh(fields);
    const terrain::TerrainMeshData clipped_land = terrain::make_clipped_land_mesh(fields, mesh);
    require(fields.sample_count() == 33U * 33U, "terrain fields should match grid dimensions");
    require(fields.height_m.size() == fields.sample_count(), "terrain height field size mismatch");
    require(fields.water_depth_m.size() == fields.sample_count(),
            "terrain water depth field size mismatch");
    require(fields.shore_sdf_m.size() == fields.sample_count(),
            "terrain shoreline field size mismatch");
    require(fields.land_potential.size() == fields.sample_count(),
            "terrain landform field size mismatch");
    require(fields.inland.size() == fields.sample_count(), "terrain inland field size mismatch");
    require(fields.ridge_strength.size() == fields.sample_count(),
            "terrain ridge field size mismatch");
    require(fields.valley_strength.size() == fields.sample_count(),
            "terrain valley field size mismatch");
    require(fields.material_masks.size() == fields.sample_count(),
            "terrain material field size mismatch");
    require(fields.min_height_m < 0.0F, "terrain should include underwater terrain");
    require(fields.max_height_m > 0.0F, "terrain should include land terrain");
    require(fields.max_water_depth_m > 0.0F, "terrain should include positive water depth");
    require(fields.max_abs_shore_sdf_m > 0.0F, "terrain should include shoreline distance");
    require(terrain::terrain_triangle_count(clipped_land) > 0U,
            "clipped land mesh should include land triangles");
    require(terrain::terrain_triangle_count(clipped_land) <=
                terrain::terrain_triangle_count(mesh) * 2U,
            "clipped land mesh should not exceed two triangles per source triangle");

    for (std::size_t index = 0; index < fields.sample_count(); ++index) {
        require(std::isfinite(fields.height_m[index]), "terrain height should be finite");
        require(std::isfinite(fields.water_depth_m[index]), "terrain water depth should be finite");
        require(std::isfinite(fields.shore_sdf_m[index]), "terrain shoreline should be finite");
        require(std::isfinite(fields.land_potential[index]),
                "terrain landform field should be finite");
        require(std::isfinite(fields.inland[index]), "terrain inland field should be finite");
        require(std::isfinite(fields.ridge_strength[index]),
                "terrain ridge field should be finite");
        require(std::isfinite(fields.valley_strength[index]),
                "terrain valley field should be finite");
        require(fields.water_depth_m[index] >= 0.0F, "terrain water depth should be positive");
        require_near(fields.water_depth_m[index],
                     std::max(0.0F, fields.desc.sea_level_m - fields.height_m[index]), 0.001F,
                     "terrain water depth should match sea level minus height");
        if (fields.water_depth_m[index] > 0.0F) {
            require(fields.shore_sdf_m[index] <= 0.001F,
                    "underwater samples should be on water side of shoreline");
        } else {
            require(fields.shore_sdf_m[index] >= -0.001F,
                    "land samples should be on land side of shoreline");
        }

        const terrain::TerrainMaterialMask mask = fields.material_masks[index];
        const float sum = mask.sand + mask.rock + mask.vegetation + mask.sediment;
        require_near(sum, 1.0F, 0.001F, "terrain material masks should be normalized");
    }

    for (const terrain::TerrainVertex& vertex : clipped_land.vertices) {
        require(vertex.fields.x >= fields.desc.sea_level_m - 0.001F,
                "clipped land mesh should not include underwater vertices");
    }

    terrain::TerrainConfig feature_grid = defaults;
    feature_grid.grid_width = 129;
    feature_grid.grid_height = 129;
    const terrain::TerrainFieldData feature_fields = terrain::generate_terrain_fields(feature_grid);
    float ridge_sum = 0.0F;
    float valley_sum = 0.0F;
    for (std::size_t index = 0; index < feature_fields.sample_count(); ++index) {
        ridge_sum += feature_fields.ridge_strength[index];
        valley_sum += feature_fields.valley_strength[index];
    }
    require(ridge_sum > 0.0F, "terrain should include ridge influence");
    require(valley_sum > 0.0F, "terrain should include valley influence");

    terrain::TerrainConfig isolated_relief = defaults;
    isolated_relief.coast_noise_strength = 0.0F;
    isolated_relief.relief_scale = 2.0F;
    isolated_relief.ridge_scale = 0.0F;
    isolated_relief.valley_scale = 0.0F;
    const terrain::TerrainFieldData isolated_fields =
        terrain::generate_terrain_fields(isolated_relief);
    float isolated_max_slope = 0.0F;
    for (std::size_t index = 0; index < isolated_fields.sample_count(); ++index) {
        if (isolated_fields.water_depth_m[index] <= 0.0F) {
            isolated_max_slope = std::max(isolated_max_slope, isolated_fields.slope[index]);
        }
    }
    require(isolated_max_slope < 4.0F,
            "isolated base relief should not create terrain spikes or fingers");

    const float shoreline_edge_limit = isolated_fields.desc.cell_size_m * 2.5F;
    for (std::uint32_t y = 0; y < isolated_fields.desc.height; ++y) {
        for (std::uint32_t x = 0; x < isolated_fields.desc.width; ++x) {
            const std::size_t sample = isolated_fields.index(x, y);
            const auto check_neighbor = [&](std::uint32_t nx, std::uint32_t ny) {
                const std::size_t neighbor = isolated_fields.index(nx, ny);
                const bool same_side = (isolated_fields.shore_sdf_m[sample] >= 0.0F) ==
                                       (isolated_fields.shore_sdf_m[neighbor] >= 0.0F);
                if (same_side) {
                    require(std::abs(isolated_fields.shore_sdf_m[sample] -
                                     isolated_fields.shore_sdf_m[neighbor]) <= shoreline_edge_limit,
                            "shoreline distance should be continuous on the same side");
                }
            };
            if (x + 1U < isolated_fields.desc.width) {
                check_neighbor(x + 1U, y);
            }
            if (y + 1U < isolated_fields.desc.height) {
                check_neighbor(x, y + 1U);
            }
        }
    }

    for (const std::uint64_t seed : {11ULL, 77ULL, 12345ULL}) {
        terrain::TerrainConfig seeded = defaults;
        seeded.seed = seed;
        seeded.grid_width = 65;
        seeded.grid_height = 65;
        const terrain::TerrainFieldData seeded_fields = terrain::generate_terrain_fields(seeded);
        std::size_t seeded_land = 0;
        std::size_t seeded_water = 0;
        std::size_t seeded_shore = 0;
        for (std::size_t index = 0; index < seeded_fields.sample_count(); ++index) {
            if (seeded_fields.water_depth_m[index] > 0.0F) {
                ++seeded_water;
            } else {
                ++seeded_land;
            }
            if (std::abs(seeded_fields.shore_sdf_m[index]) <=
                seeded_fields.desc.cell_size_m * 1.5F) {
                ++seeded_shore;
            }
        }
        const float land_fraction =
            static_cast<float>(seeded_land) / static_cast<float>(seeded_fields.sample_count());
        require(seeded_land > 0U && seeded_water > 0U,
                "seeded terrain should include land and water");
        require(land_fraction > 0.18F && land_fraction < 0.78F,
                "seeded terrain land fraction should remain sane");
        require(seeded_shore > 0U, "seeded terrain should include shoreline samples");
    }
}
