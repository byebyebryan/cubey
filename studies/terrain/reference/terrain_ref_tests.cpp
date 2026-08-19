#include "terrain_engine_reference.h"
#include "shadertoy_biome_reference.h"
#include "shadertoy_erosion_reference.h"
#include "shadertoy_mountain_reference.h"
#include "terrain_ref_config.h"
#include "terrain_ref_mesh.h"

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

void test_terrain_ref_config_model() {
    using namespace cubey::projects::terrain_ref;
    TerrainRefConfig config;
    require(config.seed == kTerrainRefDefaultSeed, "terrain_ref should default seed");
    require(config.grid_width == kTerrainRefDefaultGridSize &&
                config.grid_height == kTerrainRefDefaultGridSize,
            "terrain_ref should default grid dimensions");
    require(config.cell_size_m == kTerrainRefDefaultCellSizeM &&
                config.vertical_scale == kTerrainRefDefaultVerticalScale,
            "terrain_ref should default scale values");
    require(config.water_surface, "terrain_ref should default water on");
    require(config.camera_preset == TerrainRefCameraPreset::Oblique,
            "terrain_ref should default to oblique camera");
    require(config.recipe == TerrainRefRecipe::TerrainEngine,
            "terrain_ref should default to TerrainEngine recipe");
    require(config.material_mode == TerrainRefMaterialMode::Recipe,
            "terrain_ref should default to recipe material");
    require(config.surface_mode == TerrainRefSurfaceMode::Filtered,
            "terrain_ref should default to filtered surface");
    require(!config.erosion_filter_enabled,
            "terrain_ref should not filter ordinary recipes by default");

    config.grid_width = 129U;
    config.grid_height = 257U;
    config.seed = 42U;
    config.cell_size_m = 64.0F;
    config.vertical_scale = 0.75F;
    config.camera_preset = terrain_ref_camera_preset_from_name("surface_low");
    config.material_mode = terrain_ref_material_mode_from_name("height");
    config.surface_mode = terrain_ref_surface_mode_from_name("post-erosion");
    config.water_surface = false;
    config.recipe = terrain_ref_recipe_from_name(kTerrainRefRecipeShadertoyAlpine);
    config.erosion_filter_enabled = true;
    require(config.grid_width == 129U && config.grid_height == 257U && config.seed == 42U,
            "terrain_ref should preserve direct grid and seed values");
    require(config.camera_preset == TerrainRefCameraPreset::SurfaceLow &&
                config.material_mode == TerrainRefMaterialMode::Height &&
                config.surface_mode == TerrainRefSurfaceMode::Filtered && !config.water_surface,
            "terrain_ref should preserve parsed direct values");
    require(config.recipe == TerrainRefRecipe::ShadertoyAlpine &&
                config.erosion_filter_enabled,
            "terrain_ref should preserve the selected recipe and filter state");
    require(terrain_ref_camera_preset_from_name("coastal_oblique") ==
                TerrainRefCameraPreset::CoastalOblique,
            "terrain_ref should retain the coastal-oblique alias");
    require(terrain_ref_camera_preset_name(TerrainRefCameraPreset::CoastalOblique) ==
                "coastal-oblique",
            "terrain_ref should expose coastal-oblique camera name");
    require(terrain_ref_recipe_from_name(kTerrainRefRecipeShadertoyErosionFilter) ==
                TerrainRefRecipe::ShadertoyErosionFilter,
            "terrain_ref should parse the erosion-filter recipe");
    require(terrain_ref_material_mode_from_name("erosion") == TerrainRefMaterialMode::Erosion &&
                terrain_ref_surface_mode_from_name("pre-process") == TerrainRefSurfaceMode::Base,
            "terrain_ref should parse erosion diagnostic modes");
    require_throws(
        [] { static_cast<void>(terrain_ref_recipe_from_name("temperate-mountain-river")); },
        "terrain_ref should reject non-reference terrain recipes");
    require_throws(
        [] { static_cast<void>(terrain_ref_material_mode_from_name("river")); },
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

void test_shadertoy_erosion_reference_sampling() {
    using cubey::projects::terrain_ref::ShadertoyErosionSourceSample;
    using cubey::projects::terrain_ref::ShadertoyErosionReferenceSurface;
    constexpr std::uint64_t seed = 24680U;
    const auto sample =
        cubey::projects::terrain_ref::shadertoy_erosion_reference_sample(2317.0F, -1489.0F, seed);
    const auto repeated =
        cubey::projects::terrain_ref::shadertoy_erosion_reference_sample(2317.0F, -1489.0F, seed);
    const auto changed_seed = cubey::projects::terrain_ref::shadertoy_erosion_reference_sample(
        2317.0F, -1489.0F, seed + 1U);

    require_near(sample.base_height_m, repeated.base_height_m, 0.0001F,
                 "erosion reference base height should be deterministic");
    require_near(sample.filtered_height_m, repeated.filtered_height_m, 0.0001F,
                 "erosion reference filtered height should be deterministic");
    require_near(sample.erosion_delta_m, sample.base_height_m - sample.filtered_height_m, 0.001F,
                 "erosion reference delta should describe removed height");
    require(std::isfinite(sample.base_height_m) && std::isfinite(sample.filtered_height_m) &&
                std::isfinite(sample.erosion_delta_m) &&
                std::isfinite(sample.base_gradient_x) &&
                std::isfinite(sample.base_gradient_z) && std::isfinite(sample.gradient_x) &&
                std::isfinite(sample.gradient_z),
            "erosion reference sample should be finite");
    require(std::abs(sample.erosion_delta_m) < 800.0F,
            "erosion reference delta should remain bounded");
    require(std::abs(sample.filtered_height_m - changed_seed.filtered_height_m) > 0.0001F,
            "erosion reference seed should affect filtered height");
    require_near(cubey::projects::terrain_ref::shadertoy_erosion_reference_height(
                     2317.0F, -1489.0F, seed, ShadertoyErosionReferenceSurface::Base),
                 sample.base_height_m, 0.0001F,
                 "erosion reference base surface should select base height");
    require_near(cubey::projects::terrain_ref::shadertoy_erosion_reference_height(
                     2317.0F, -1489.0F, seed, ShadertoyErosionReferenceSurface::Filtered),
                 sample.filtered_height_m, 0.0001F,
                 "erosion reference filtered surface should select filtered height");

    constexpr float step_m = 8.0F;
    const float dx = (cubey::projects::terrain_ref::shadertoy_erosion_reference_height(
                          2317.0F + step_m, -1489.0F, seed) -
                      cubey::projects::terrain_ref::shadertoy_erosion_reference_height(
                          2317.0F - step_m, -1489.0F, seed)) /
                     (2.0F * step_m);
    const float dz = (cubey::projects::terrain_ref::shadertoy_erosion_reference_height(
                          2317.0F, -1489.0F + step_m, seed) -
                      cubey::projects::terrain_ref::shadertoy_erosion_reference_height(
                          2317.0F, -1489.0F - step_m, seed)) /
                     (2.0F * step_m);
    require(std::abs(sample.gradient_x - dx) < 0.85F && std::abs(sample.gradient_z - dz) < 0.85F,
            "erosion reference process gradient should track filtered finite differences");
    const float normal_cos_v =
        cubey::projects::terrain_ref::shadertoy_erosion_reference_normal_cos_v(2317.0F, -1489.0F,
                                                                               seed);
    require(normal_cos_v > 0.0F && normal_cos_v <= 1.0F,
            "erosion reference normal cosine should be normalized");

    const ShadertoyErosionSourceSample generic_source{
        .height_m = 825.0F,
        .gradient_x = 0.24F,
        .gradient_z = -0.13F,
    };
    const auto generic = cubey::projects::terrain_ref::shadertoy_erosion_filter_sample(
        2317.0F, -1489.0F, seed, generic_source);
    const auto generic_repeated =
        cubey::projects::terrain_ref::shadertoy_erosion_filter_sample(
            2317.0F, -1489.0F, seed, generic_source);
    const auto generic_changed_seed =
        cubey::projects::terrain_ref::shadertoy_erosion_filter_sample(
            2317.0F, -1489.0F, seed + 1U, generic_source);
    const auto inactive = cubey::projects::terrain_ref::shadertoy_erosion_filter_sample(
        2317.0F, -1489.0F, seed, generic_source, 0.0F);
    require_near(generic.filtered_height_m, generic_repeated.filtered_height_m, 0.0001F,
                 "generic erosion filtering should be deterministic");
    require_near(generic.base_height_m, generic_source.height_m, 0.0001F,
                 "generic erosion filtering should preserve its source height");
    require_near(generic.base_gradient_x, generic_source.gradient_x, 0.0001F,
                 "generic erosion filtering should preserve its source x gradient");
    require_near(generic.base_gradient_z, generic_source.gradient_z, 0.0001F,
                 "generic erosion filtering should preserve its source z gradient");
    require(std::abs(generic.filtered_height_m - generic_changed_seed.filtered_height_m) >
                0.0001F,
            "generic erosion filtering should respond to seed changes");
    require_near(inactive.filtered_height_m, generic_source.height_m, 0.0001F,
                 "zero activity should preserve source height");
    require_near(inactive.gradient_x, generic_source.gradient_x, 0.0001F,
                 "zero activity should preserve source x gradient");
    require_near(inactive.gradient_z, generic_source.gradient_z, 0.0001F,
                 "zero activity should preserve source z gradient");
}

} // namespace

int main() {
    test_terrain_ref_config_model();
    test_terrain_engine_reference_sampling_is_deterministic();
    test_shadertoy_mountain_reference_sampling_is_deterministic();
    test_shadertoy_biome_reference_sampling_is_deterministic();
    test_shadertoy_erosion_reference_sampling();
    test_terrain_ref_mesh_uses_clipmap_grid();
    return 0;
}
