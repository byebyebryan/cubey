#include "planet_surface.h"

#include <cmath>
#include <cstdio>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void test_planet_surface_builds_expected_patch_counts() {
    const cubey::projects::planet::PlanetConfig config{
        .radius_m = 600000.0F,
        .patches_per_face = 2,
        .patch_resolution = 4,
        .max_lod_level = 0,
        .debug_view = cubey::projects::planet::PlanetDebugView::FaceId,
    };
    const cubey::projects::planet::PlanetSurfaceBuildResult result =
        cubey::projects::planet::make_planet_surface_mesh(config);

    require(result.diagnostics.patch_count == 24U, "planet surface should build six cube faces");
    require(result.diagnostics.vertex_count == 24U * 25U,
            "planet surface should build per-patch vertices");
    require(result.diagnostics.triangle_count == 24U * 4U * 4U * 2U,
            "planet surface should build two triangles per quad");
    require(result.diagnostics.min_edge_length_m > 0.0F,
            "planet surface should report positive edge lengths");
}

void test_planet_surface_vertices_stay_on_radius() {
    const cubey::projects::planet::PlanetConfig config{
        .radius_m = 1200.0F,
        .patches_per_face = 1,
        .patch_resolution = 3,
        .max_lod_level = 0,
    };
    const cubey::projects::planet::PlanetSurfaceBuildResult result =
        cubey::projects::planet::make_planet_surface_mesh(config);

    for (const cubey::render::VertexPositionColorNormalUv& vertex : result.mesh.vertices) {
        const float length = std::sqrt(vertex.position[0] * vertex.position[0] +
                                       vertex.position[1] * vertex.position[1] +
                                       vertex.position[2] * vertex.position[2]);
        require(std::abs(length - config.radius_m) < 0.1F,
                "planet surface vertices should lie on the sphere radius");
    }
}

void test_planet_surface_lod_subdivides_near_camera_patches() {
    const cubey::projects::planet::PlanetConfig config{
        .radius_m = 1000.0F,
        .camera_altitude_m = 500.0F,
        .patches_per_face = 1,
        .patch_resolution = 2,
        .max_lod_level = 1,
        .lod_target_edge_px = 1.0F,
        .debug_view = cubey::projects::planet::PlanetDebugView::LodLevel,
    };
    const cubey::projects::planet::PlanetSurfaceView view{
        .camera_position_m = {0.0F, 0.0F, 1500.0F},
    };
    const cubey::projects::planet::PlanetSurfaceBuildResult result =
        cubey::projects::planet::make_planet_surface_mesh(config, view);

    require(result.diagnostics.patch_count > 6U, "planet LOD should subdivide close patches");
    require(result.diagnostics.max_lod_level == 1U, "planet LOD should report max selected level");
    require(result.diagnostics.patches_by_lod[1] > 0U,
            "planet LOD should report level-one patch count");
    require(result.diagnostics.max_screen_error_px > result.diagnostics.min_screen_error_px,
            "planet LOD should report screen-error range");
}

} // namespace

int main() {
    try {
        test_planet_surface_builds_expected_patch_counts();
        test_planet_surface_vertices_stay_on_radius();
        test_planet_surface_lod_subdivides_near_camera_patches();
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "planet_surface_tests: %s\n", error.what());
        return 1;
    }
}
