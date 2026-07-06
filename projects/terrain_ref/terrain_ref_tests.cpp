#include "terrain_engine_reference.h"
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

    run_config.grid.width = 129U;
    run_config.grid.height = 257U;
    run_config.terrain.seed = 42U;
    run_config.terrain.seed_set = true;
    run_config.terrain.cell_size = 64.0F;
    run_config.terrain.vertical_scale = 0.75F;
    run_config.terrain.camera_preset = "surface_low";
    run_config.terrain.water_surface = 0;
    run_config.terrain.recipe =
        std::string(cubey::projects::terrain_ref::kTerrainRefRecipeTerrainEngine);
    config = cubey::projects::terrain_ref::terrain_ref_config_from_run_config(run_config);
    require(config.grid_width == 129U && config.grid_height == 257U,
            "terrain_ref should use shared grid dimensions");
    require(config.seed == 42U, "terrain_ref should use terrain seed");
    require(config.cell_size_m == 64.0F, "terrain_ref should use terrain cell size");
    require(config.vertical_scale == 0.75F, "terrain_ref should use terrain vertical scale");
    require(config.camera_preset ==
                cubey::projects::terrain_ref::TerrainRefCameraPreset::SurfaceLow,
            "terrain_ref should parse surface-low camera alias");
    require(!config.water_surface, "terrain_ref should allow disabling water");

    run_config.terrain.recipe = "temperate-mountain-river";
    require_throws(
        [&run_config] {
            static_cast<void>(
                cubey::projects::terrain_ref::terrain_ref_config_from_run_config(run_config));
        },
        "terrain_ref should reject non-reference terrain recipes");
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

} // namespace

int main() {
    test_terrain_ref_config_from_run_config();
    test_terrain_engine_reference_sampling_is_deterministic();
    test_terrain_ref_mesh_uses_clipmap_grid();
    return 0;
}
