#include "terrain_engine_reference.h"
#include "shadertoy_biome_reference.h"
#include "shadertoy_mountain_reference.h"
#include "terrain_ref_config.h"
#include "terrain_ref_mesh.h"

#include <cubey/core/run_config.h>

#include <cmath>
#include <cstdint>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void require_near(float value, float expected, float tolerance, const char* message) {
    require(std::abs(value - expected) <= tolerance, message);
}

template <typename Fn> void require_throws(Fn&& fn, const char* message) {
    try {
        fn();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error(message);
}

void test_terrain_ref_config_from_run_config() {
    cubey::RunConfig run_config;
    cubey::projects::terrain_ref::TerrainRefConfig config =
        cubey::projects::terrain_ref::terrain_ref_config_from_run_config(run_config);
    require(config.seed == cubey::projects::terrain_ref::kTerrainRefDefaultSeed,
            "terrain_ref should default seed");
    require(config.grid_width == cubey::projects::terrain_ref::kTerrainRefDefaultGridSize,
            "terrain_ref should default grid width");
    require(config.grid_height == cubey::projects::terrain_ref::kTerrainRefDefaultGridSize,
            "terrain_ref should default grid height");
    require(config.water_surface, "terrain_ref should default water on");
    require(config.camera_preset == cubey::projects::terrain_ref::TerrainRefCameraPreset::Oblique,
            "terrain_ref should default to oblique camera");
    require(config.recipe == cubey::projects::terrain_ref::TerrainRefRecipe::TerrainEngine,
            "terrain_ref should default to TerrainEngine recipe");
    require(config.material_mode == cubey::projects::terrain_ref::TerrainRefMaterialMode::Recipe,
            "terrain_ref should default to recipe material");

    run_config.grid.width = 129U;
    run_config.grid.height = 257U;
    run_config.terrain.seed = 42U;
    run_config.terrain.seed_set = true;
    run_config.terrain.cell_size = 64.0F;
    run_config.terrain.vertical_scale = 0.75F;
    run_config.terrain.camera_preset = "surface_low";
    run_config.terrain.preview_color = "height";
    run_config.terrain.water_surface = 0;
    run_config.terrain.recipe =
        std::string(cubey::projects::terrain_ref::kTerrainRefRecipeTerrainEngine);
    config = cubey::projects::terrain_ref::terrain_ref_config_from_run_config(run_config);
    require(config.recipe == cubey::projects::terrain_ref::TerrainRefRecipe::TerrainEngine,
            "terrain_ref should parse TerrainEngine recipe");
    require(config.grid_width == 129U && config.grid_height == 257U,
            "terrain_ref should use shared grid dimensions");
    require(config.seed == 42U, "terrain_ref should use terrain seed");
    require(config.cell_size_m == 64.0F, "terrain_ref should use terrain cell size");
    require(config.vertical_scale == 0.75F, "terrain_ref should use terrain vertical scale");
    require(config.camera_preset ==
                cubey::projects::terrain_ref::TerrainRefCameraPreset::SurfaceLow,
            "terrain_ref should parse surface-low camera alias");
    require(config.material_mode == cubey::projects::terrain_ref::TerrainRefMaterialMode::Height,
            "terrain_ref should parse height material preview");
    require(!config.water_surface, "terrain_ref should allow disabling water");

    run_config.terrain.camera_preset = "coastal_oblique";
    config = cubey::projects::terrain_ref::terrain_ref_config_from_run_config(run_config);
    require(config.camera_preset ==
                cubey::projects::terrain_ref::TerrainRefCameraPreset::CoastalOblique,
            "terrain_ref should parse coastal-oblique camera alias");
    require(cubey::projects::terrain_ref::terrain_ref_camera_preset_name(config.camera_preset) ==
                "coastal-oblique",
            "terrain_ref should expose coastal-oblique camera name");
    run_config.terrain.camera_preset = "surface_low";

    run_config.terrain.recipe =
        std::string(cubey::projects::terrain_ref::kTerrainRefRecipeShadertoyMountain);
    config = cubey::projects::terrain_ref::terrain_ref_config_from_run_config(run_config);
    require(config.recipe == cubey::projects::terrain_ref::TerrainRefRecipe::ShadertoyMountain,
            "terrain_ref should parse ShaderToy mountain recipe");

    run_config.terrain.recipe =
        std::string(cubey::projects::terrain_ref::kTerrainRefRecipeShadertoyAlpine);
    config = cubey::projects::terrain_ref::terrain_ref_config_from_run_config(run_config);
    require(config.recipe == cubey::projects::terrain_ref::TerrainRefRecipe::ShadertoyAlpine,
            "terrain_ref should parse ShaderToy alpine recipe");

    run_config.terrain.recipe =
        std::string(cubey::projects::terrain_ref::kTerrainRefRecipeShadertoyDunes);
    config = cubey::projects::terrain_ref::terrain_ref_config_from_run_config(run_config);
    require(config.recipe == cubey::projects::terrain_ref::TerrainRefRecipe::ShadertoyDunes,
            "terrain_ref should parse ShaderToy dunes recipe");

    run_config.terrain.recipe =
        std::string(cubey::projects::terrain_ref::kTerrainRefRecipeShadertoyLakeBasin);
    config = cubey::projects::terrain_ref::terrain_ref_config_from_run_config(run_config);
    require(config.recipe == cubey::projects::terrain_ref::TerrainRefRecipe::ShadertoyLakeBasin,
            "terrain_ref should parse ShaderToy lake-basin recipe");

    run_config.terrain.recipe =
        std::string(cubey::projects::terrain_ref::kTerrainRefRecipeShadertoyBadlands);
    config = cubey::projects::terrain_ref::terrain_ref_config_from_run_config(run_config);
    require(config.recipe == cubey::projects::terrain_ref::TerrainRefRecipe::ShadertoyBadlands,
            "terrain_ref should parse ShaderToy badlands recipe");

    run_config.terrain.recipe =
        std::string(cubey::projects::terrain_ref::kTerrainRefRecipeShadertoyCoastIsland);
    config = cubey::projects::terrain_ref::terrain_ref_config_from_run_config(run_config);
    require(config.recipe == cubey::projects::terrain_ref::TerrainRefRecipe::ShadertoyCoastIsland,
            "terrain_ref should parse ShaderToy coast-island recipe");

    run_config.terrain.recipe =
        std::string(cubey::projects::terrain_ref::kTerrainRefRecipeShadertoyPlains);
    config = cubey::projects::terrain_ref::terrain_ref_config_from_run_config(run_config);
    require(config.recipe == cubey::projects::terrain_ref::TerrainRefRecipe::ShadertoyPlains,
            "terrain_ref should parse ShaderToy plains recipe");

    run_config.terrain.recipe =
        std::string(cubey::projects::terrain_ref::kTerrainRefRecipeShadertoyGorge);
    config = cubey::projects::terrain_ref::terrain_ref_config_from_run_config(run_config);
    require(config.recipe == cubey::projects::terrain_ref::TerrainRefRecipe::ShadertoyGorge,
            "terrain_ref should parse ShaderToy gorge recipe");

    run_config.terrain.recipe =
        std::string(cubey::projects::terrain_ref::kTerrainRefRecipeShadertoyGlacialHighland);
    config = cubey::projects::terrain_ref::terrain_ref_config_from_run_config(run_config);
    require(config.recipe ==
                cubey::projects::terrain_ref::TerrainRefRecipe::ShadertoyGlacialHighland,
            "terrain_ref should parse ShaderToy glacial-highland recipe");

    run_config.terrain.recipe =
        std::string(cubey::projects::terrain_ref::kTerrainRefRecipeShadertoyCraterField);
    config = cubey::projects::terrain_ref::terrain_ref_config_from_run_config(run_config);
    require(config.recipe == cubey::projects::terrain_ref::TerrainRefRecipe::ShadertoyCraterField,
            "terrain_ref should parse ShaderToy crater-field recipe");

    run_config.terrain.recipe = "temperate-mountain-river";
    require_throws(
        [&run_config] {
            static_cast<void>(
                cubey::projects::terrain_ref::terrain_ref_config_from_run_config(run_config));
        },
        "terrain_ref should reject non-reference terrain recipes");

    run_config.terrain.recipe =
        std::string(cubey::projects::terrain_ref::kTerrainRefRecipeShadertoyMountain);
    run_config.terrain.preview_color = "river";
    require_throws(
        [&run_config] {
            static_cast<void>(
                cubey::projects::terrain_ref::terrain_ref_config_from_run_config(run_config));
        },
        "terrain_ref should reject unsupported preview color modes");
}

void test_terrain_engine_reference_sampling_is_deterministic() {
    constexpr std::uint64_t seed = 1234U;
    const float height_a =
        cubey::projects::terrain_ref::terrain_engine_reference_height(17.0F, 29.0F, seed);
    const float height_b =
        cubey::projects::terrain_ref::terrain_engine_reference_height(17.0F, 29.0F, seed);
    const float height_c =
        cubey::projects::terrain_ref::terrain_engine_reference_height(17.0F, 29.0F, seed + 1U);
    require_near(height_a, height_b, 0.0001F,
                 "TerrainEngine reference sampling should be deterministic");
    require(std::isfinite(height_a) && height_a >= 0.0F,
            "TerrainEngine reference height should be finite and nonnegative");
    require(std::abs(height_a - height_c) > 0.0001F,
            "TerrainEngine reference seed should affect height");
}

void test_terrain_ref_mesh_uses_clipmap_grid() {
    cubey::projects::terrain_ref::TerrainRefConfig config;
    config.grid_width = 65U;
    config.grid_height = 65U;
    config.cell_size_m = 32.0F;
    const cubey::render::ClipmapGrid2DConfig clipmap_config =
        cubey::projects::terrain_ref::terrain_ref_clipmap_config(config);
    const auto patches = cubey::render::clipmap_grid_2d_patches<32U>(clipmap_config);
    const cubey::render::ClipmapGrid2DDiagnostics diagnostics =
        cubey::render::clipmap_grid_2d_diagnostics(clipmap_config, patches);
    const cubey::projects::terrain_ref::TerrainRefMeshData mesh =
        cubey::projects::terrain_ref::make_terrain_ref_mesh(config);

    require(mesh.vertices.size() == diagnostics.total_vertices,
            "terrain_ref mesh should match clipmap vertex budget");
    require(mesh.indices.size() == diagnostics.total_vertices,
            "terrain_ref mesh should emit indexed clipmap triangles");
    require(cubey::projects::terrain_ref::terrain_ref_triangle_count(mesh) ==
                diagnostics.total_triangles,
            "terrain_ref mesh should match clipmap triangle diagnostics");
    require(mesh.vertices.front().position[1] == 0.0F,
            "terrain_ref mesh should leave height displacement to the shader");
}

void test_shadertoy_mountain_reference_sampling_is_deterministic() {
    constexpr std::uint64_t seed = 5678U;
    const float height_a =
        cubey::projects::terrain_ref::shadertoy_mountain_reference_height(113.0F, -71.0F, seed);
    const float height_b =
        cubey::projects::terrain_ref::shadertoy_mountain_reference_height(113.0F, -71.0F, seed);
    const float height_c =
        cubey::projects::terrain_ref::shadertoy_mountain_reference_height(113.0F, -71.0F,
                                                                          seed + 1U);
    const float detailed_height =
        cubey::projects::terrain_ref::shadertoy_mountain_reference_height(
            113.0F, -71.0F, seed,
            cubey::projects::terrain_ref::ShadertoyMountainReferenceDetail::Surface);
    require_near(height_a, height_b, 0.0001F,
                 "ShaderToy mountain sampling should be deterministic");
    require(std::isfinite(height_a) && height_a >= 0.0F,
            "ShaderToy mountain height should be finite and nonnegative");
    require(std::isfinite(detailed_height) && detailed_height >= 0.0F,
            "ShaderToy mountain detailed height should be finite and nonnegative");
    require(std::abs(height_a - height_c) > 0.0001F,
            "ShaderToy mountain seed should affect height");
}

void test_shadertoy_biome_reference_sampling_is_deterministic() {
    constexpr std::uint64_t seed = 9012U;
    const auto check_height = [](float height_a, float height_b, float height_c,
                                 const char* deterministic_message,
                                 const char* finite_message,
                                 const char* seed_message) {
        require_near(height_a, height_b, 0.0001F, deterministic_message);
        require(std::isfinite(height_a), finite_message);
        require(std::abs(height_a - height_c) > 0.0001F, seed_message);
    };

    check_height(cubey::projects::terrain_ref::shadertoy_alpine_reference_height(217.0F, -341.0F,
                                                                                 seed),
                 cubey::projects::terrain_ref::shadertoy_alpine_reference_height(217.0F, -341.0F,
                                                                                 seed),
                 cubey::projects::terrain_ref::shadertoy_alpine_reference_height(217.0F, -341.0F,
                                                                                 seed + 1U),
                 "ShaderToy alpine sampling should be deterministic",
                 "ShaderToy alpine height should be finite",
                 "ShaderToy alpine seed should affect height");
    check_height(cubey::projects::terrain_ref::shadertoy_dunes_reference_height(217.0F, -341.0F,
                                                                                seed),
                 cubey::projects::terrain_ref::shadertoy_dunes_reference_height(217.0F, -341.0F,
                                                                                seed),
                 cubey::projects::terrain_ref::shadertoy_dunes_reference_height(217.0F, -341.0F,
                                                                                seed + 1U),
                 "ShaderToy dunes sampling should be deterministic",
                 "ShaderToy dunes height should be finite",
                 "ShaderToy dunes seed should affect height");
    const auto lake_sample_sum = [](std::uint64_t sample_seed) {
        return cubey::projects::terrain_ref::shadertoy_lake_basin_reference_height(
                   217.0F, -341.0F, sample_seed) +
               cubey::projects::terrain_ref::shadertoy_lake_basin_reference_height(
                   4021.0F, -2789.0F, sample_seed) +
               cubey::projects::terrain_ref::shadertoy_lake_basin_reference_height(
                   -5113.0F, 1900.0F, sample_seed);
    };
    check_height(lake_sample_sum(seed), lake_sample_sum(seed), lake_sample_sum(seed + 1U),
                 "ShaderToy lake-basin sampling should be deterministic",
                 "ShaderToy lake-basin height should be finite",
                 "ShaderToy lake-basin seed should affect height");
    check_height(cubey::projects::terrain_ref::shadertoy_badlands_reference_height(217.0F,
                                                                                   -341.0F, seed),
                 cubey::projects::terrain_ref::shadertoy_badlands_reference_height(217.0F,
                                                                                   -341.0F, seed),
                 cubey::projects::terrain_ref::shadertoy_badlands_reference_height(
                     217.0F, -341.0F, seed + 1U),
                 "ShaderToy badlands sampling should be deterministic",
                 "ShaderToy badlands height should be finite",
                 "ShaderToy badlands seed should affect height");
    check_height(cubey::projects::terrain_ref::shadertoy_coast_island_reference_height(
                     217.0F, -341.0F, seed),
                 cubey::projects::terrain_ref::shadertoy_coast_island_reference_height(
                     217.0F, -341.0F, seed),
                 cubey::projects::terrain_ref::shadertoy_coast_island_reference_height(
                     217.0F, -341.0F, seed + 1U),
                 "ShaderToy coast-island sampling should be deterministic",
                 "ShaderToy coast-island height should be finite",
                 "ShaderToy coast-island seed should affect height");
    check_height(cubey::projects::terrain_ref::shadertoy_plains_reference_height(217.0F, -341.0F,
                                                                                 seed),
                 cubey::projects::terrain_ref::shadertoy_plains_reference_height(217.0F, -341.0F,
                                                                                 seed),
                 cubey::projects::terrain_ref::shadertoy_plains_reference_height(217.0F, -341.0F,
                                                                                 seed + 1U),
                 "ShaderToy plains sampling should be deterministic",
                 "ShaderToy plains height should be finite",
                 "ShaderToy plains seed should affect height");
    check_height(cubey::projects::terrain_ref::shadertoy_gorge_reference_height(217.0F, -341.0F,
                                                                                seed),
                 cubey::projects::terrain_ref::shadertoy_gorge_reference_height(217.0F, -341.0F,
                                                                                seed),
                 cubey::projects::terrain_ref::shadertoy_gorge_reference_height(217.0F, -341.0F,
                                                                                seed + 1U),
                 "ShaderToy gorge sampling should be deterministic",
                 "ShaderToy gorge height should be finite",
                 "ShaderToy gorge seed should affect height");
    check_height(cubey::projects::terrain_ref::shadertoy_glacial_highland_reference_height(
                     217.0F, -341.0F, seed),
                 cubey::projects::terrain_ref::shadertoy_glacial_highland_reference_height(
                     217.0F, -341.0F, seed),
                 cubey::projects::terrain_ref::shadertoy_glacial_highland_reference_height(
                     217.0F, -341.0F, seed + 1U),
                 "ShaderToy glacial-highland sampling should be deterministic",
                 "ShaderToy glacial-highland height should be finite",
                 "ShaderToy glacial-highland seed should affect height");
    check_height(cubey::projects::terrain_ref::shadertoy_crater_field_reference_height(
                     217.0F, -341.0F, seed),
                 cubey::projects::terrain_ref::shadertoy_crater_field_reference_height(
                     217.0F, -341.0F, seed),
                 cubey::projects::terrain_ref::shadertoy_crater_field_reference_height(
                     217.0F, -341.0F, seed + 1U),
                 "ShaderToy crater-field sampling should be deterministic",
                 "ShaderToy crater-field height should be finite",
                 "ShaderToy crater-field seed should affect height");
}

} // namespace

int main() {
    test_terrain_ref_config_from_run_config();
    test_terrain_engine_reference_sampling_is_deterministic();
    test_shadertoy_mountain_reference_sampling_is_deterministic();
    test_shadertoy_biome_reference_sampling_is_deterministic();
    test_terrain_ref_mesh_uses_clipmap_grid();
    return 0;
}
