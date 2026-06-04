#include "planet_surface.h"

#include <cmath>
#include <cstddef>
#include <cstdio>
#include <stdexcept>
#include <string_view>

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
        .skirts_enabled = false,
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
        .skirts_enabled = false,
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

void test_planet_surface_triangles_are_wound_outward() {
    const cubey::projects::planet::PlanetConfig config{
        .radius_m = 1200.0F,
        .patches_per_face = 1,
        .patch_resolution = 3,
        .max_lod_level = 0,
        .skirts_enabled = false,
    };
    const cubey::projects::planet::PlanetSurfaceBuildResult result =
        cubey::projects::planet::make_planet_surface_mesh(config);

    for (std::size_t index = 0; index < result.mesh.indices.size(); index += 3U) {
        const cubey::render::VertexPositionColorNormalUv& v0 =
            result.mesh.vertices[result.mesh.indices[index]];
        const cubey::render::VertexPositionColorNormalUv& v1 =
            result.mesh.vertices[result.mesh.indices[index + 1U]];
        const cubey::render::VertexPositionColorNormalUv& v2 =
            result.mesh.vertices[result.mesh.indices[index + 2U]];
        const cubey::math::Vec3 p0{v0.position[0], v0.position[1], v0.position[2]};
        const cubey::math::Vec3 p1{v1.position[0], v1.position[1], v1.position[2]};
        const cubey::math::Vec3 p2{v2.position[0], v2.position[1], v2.position[2]};
        const cubey::math::Vec3 n0{v0.normal[0], v0.normal[1], v0.normal[2]};
        const cubey::math::Vec3 n1{v1.normal[0], v1.normal[1], v1.normal[2]};
        const cubey::math::Vec3 n2{v2.normal[0], v2.normal[1], v2.normal[2]};
        const cubey::math::Vec3 triangle_normal = glm::normalize(glm::cross(p1 - p0, p2 - p0));
        const cubey::math::Vec3 vertex_normal = glm::normalize(n0 + n1 + n2);
        require(glm::dot(triangle_normal, vertex_normal) > 0.0F,
                "planet surface triangles should be wound outward for backface culling");
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
        .skirts_enabled = false,
    };
    const cubey::projects::planet::PlanetSurfaceView view{
        .camera_world_position_m = {0.0, 0.0, 1500.0},
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

void test_planet_surface_can_build_camera_relative_vertices() {
    const cubey::projects::planet::PlanetConfig config{
        .radius_m = 1200.0F,
        .patches_per_face = 1,
        .patch_resolution = 2,
        .max_lod_level = 0,
        .skirts_enabled = false,
    };
    const cubey::Transform3D camera{
        .translation = {0.0F, 0.0F, 1500.0F},
    };
    const cubey::projects::planet::PlanetFrame frame =
        cubey::projects::planet::make_planet_frame(config, camera);
    const cubey::projects::planet::PlanetSurfaceView view{
        .camera_world_position_m = frame.camera_world_position_m,
    };
    const cubey::projects::planet::PlanetSurfaceBuildResult result =
        cubey::projects::planet::make_planet_surface_mesh(config, view, frame);

    bool found_near_point = false;
    for (const cubey::render::VertexPositionColorNormalUv& vertex : result.mesh.vertices) {
        if (vertex.normal[2] > 0.999F) {
            found_near_point = true;
            require(std::abs(vertex.position[2] + 300.0F) < 0.25F,
                    "surface near camera should be relative to render origin");
        }
    }
    require(found_near_point, "planet surface should include the near sphere point");
}

void test_planet_surface_planner_culls_out_of_view_patches() {
    const cubey::projects::planet::PlanetConfig config{
        .radius_m = 1000.0F,
        .patches_per_face = 2,
        .patch_resolution = 2,
        .max_lod_level = 0,
    };
    const cubey::projects::planet::PlanetSurfaceView view{
        .camera_world_position_m = {0.0, 0.0, 1500.0},
        .camera_forward_world = {0.0F, 0.0F, -1.0F},
        .culling_enabled = true,
    };

    const cubey::projects::planet::PlanetSurfacePatchPlan plan =
        cubey::projects::planet::plan_planet_surface_patches(config, view);

    require(plan.diagnostics.planned_patch_count == 24U,
            "planet planner should count all terminal root patches");
    require(plan.diagnostics.visible_patch_count < plan.diagnostics.planned_patch_count,
            "planet planner should cull patches outside the camera view");
    require(plan.diagnostics.culled_horizon_count + plan.diagnostics.culled_view_count > 0U,
            "planet planner should report cull reasons");
}

void test_planet_surface_planner_culls_when_camera_looks_away() {
    const cubey::projects::planet::PlanetConfig config{
        .radius_m = 1000.0F,
        .patches_per_face = 2,
        .patch_resolution = 2,
        .max_lod_level = 0,
    };
    const cubey::projects::planet::PlanetSurfaceView view{
        .camera_world_position_m = {0.0, 0.0, 1500.0},
        .camera_forward_world = {0.0F, 0.0F, 1.0F},
        .culling_enabled = true,
    };

    const cubey::projects::planet::PlanetSurfacePatchPlan plan =
        cubey::projects::planet::plan_planet_surface_patches(config, view);

    require(plan.diagnostics.visible_patch_count == 0U,
            "planet planner should cull the surface when the camera looks away");
    require(plan.diagnostics.culled_view_count > 0U,
            "planet planner should attribute away-facing patches to view culling");
}

void test_planet_surface_planner_selects_near_lod() {
    const cubey::projects::planet::PlanetConfig config{
        .radius_m = 1000.0F,
        .patches_per_face = 1,
        .patch_resolution = 2,
        .max_lod_level = 1,
        .lod_target_edge_px = 1.0F,
    };
    const cubey::projects::planet::PlanetSurfaceView view{
        .camera_world_position_m = {0.0, 0.0, 1500.0},
        .camera_forward_world = {0.0F, 0.0F, -1.0F},
        .culling_enabled = true,
    };

    const cubey::projects::planet::PlanetSurfacePatchPlan plan =
        cubey::projects::planet::plan_planet_surface_patches(config, view);

    require(plan.diagnostics.max_lod_level == 1U,
            "planet planner should select higher LOD near the camera");
    require(plan.diagnostics.patches_by_lod[1] > 0U,
            "planet planner should report selected near-camera child patches");
}

void test_planet_surface_skirts_add_seam_geometry() {
    const cubey::projects::planet::PlanetConfig no_skirts{
        .radius_m = 1200.0F,
        .patches_per_face = 1,
        .patch_resolution = 2,
        .max_lod_level = 0,
        .skirts_enabled = false,
    };
    cubey::projects::planet::PlanetConfig with_skirts = no_skirts;
    with_skirts.skirts_enabled = true;
    with_skirts.skirt_depth_scale = 0.5F;

    const cubey::projects::planet::PlanetSurfaceBuildResult base =
        cubey::projects::planet::make_planet_surface_mesh(no_skirts);
    const cubey::projects::planet::PlanetSurfaceBuildResult skirted =
        cubey::projects::planet::make_planet_surface_mesh(with_skirts);

    require(skirted.diagnostics.triangle_count > base.diagnostics.triangle_count,
            "planet skirts should add triangles");
    require(skirted.diagnostics.vertex_count > base.diagnostics.vertex_count,
            "planet skirts should add vertices");
    require(skirted.diagnostics.seam_edge_count == skirted.diagnostics.patch_count * 4U,
            "planet skirts should report one seam edge per patch side");
    require(skirted.diagnostics.skirt_triangle_count > 0U,
            "planet skirts should report skirt triangles");
    require(skirted.diagnostics.min_skirt_depth_m > 0.0F &&
                skirted.diagnostics.max_skirt_depth_m >= skirted.diagnostics.min_skirt_depth_m,
            "planet skirts should report positive skirt depth range");
}

void test_planet_surface_skirt_vertices_drop_below_radius() {
    const cubey::projects::planet::PlanetConfig config{
        .radius_m = 1200.0F,
        .patches_per_face = 1,
        .patch_resolution = 2,
        .max_lod_level = 0,
        .debug_view = cubey::projects::planet::PlanetDebugView::Seams,
        .skirts_enabled = true,
    };
    const cubey::projects::planet::PlanetSurfaceBuildResult result =
        cubey::projects::planet::make_planet_surface_mesh(config);

    bool found_skirt_vertex = false;
    for (const cubey::render::VertexPositionColorNormalUv& vertex : result.mesh.vertices) {
        const float length = std::sqrt(vertex.position[0] * vertex.position[0] +
                                       vertex.position[1] * vertex.position[1] +
                                       vertex.position[2] * vertex.position[2]);
        if (length < config.radius_m - 0.1F) {
            found_skirt_vertex = true;
            break;
        }
    }
    require(found_skirt_vertex, "planet skirt vertices should be offset below the surface radius");
}

void test_planet_surface_seams_debug_view_parses() {
    require(cubey::projects::planet::planet_debug_view_from_string("seams") ==
                cubey::projects::planet::PlanetDebugView::Seams,
            "planet debug view should parse seams");
    require(std::string_view{cubey::projects::planet::planet_debug_view_name(
                cubey::projects::planet::PlanetDebugView::Seams)} == "seams",
            "planet debug view should name seams");
}

} // namespace

int main() {
    try {
        test_planet_surface_builds_expected_patch_counts();
        test_planet_surface_vertices_stay_on_radius();
        test_planet_surface_triangles_are_wound_outward();
        test_planet_surface_lod_subdivides_near_camera_patches();
        test_planet_surface_can_build_camera_relative_vertices();
        test_planet_surface_planner_culls_out_of_view_patches();
        test_planet_surface_planner_culls_when_camera_looks_away();
        test_planet_surface_planner_selects_near_lod();
        test_planet_surface_skirts_add_seam_geometry();
        test_planet_surface_skirt_vertices_drop_below_radius();
        test_planet_surface_seams_debug_view_parses();
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "planet_surface_tests: %s\n", error.what());
        return 1;
    }
}
