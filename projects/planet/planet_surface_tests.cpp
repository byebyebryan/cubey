#include "planet_surface.h"
#include "planet_surface_field.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void require_close(float actual, float expected, const char* message) {
    require(std::abs(actual - expected) < 0.0001F, message);
}

void test_planet_surface_patch_id_derives_root_bounds() {
    const cubey::projects::planet::PlanetConfig config{
        .patches_per_face = 2,
        .max_lod_level = 2,
    };

    const cubey::projects::planet::PlanetSurfacePatchBounds bounds =
        cubey::projects::planet::planet_surface_patch_bounds(
            config, cubey::projects::planet::PlanetSurfacePatchId{
                        .face = 3,
                        .level = 0,
                        .x = 1,
                        .y = 0,
                    });

    require_close(bounds.u0, 0.0F, "root patch x should derive u0 from patch id");
    require_close(bounds.v0, -1.0F, "root patch y should derive v0 from patch id");
    require_close(bounds.u1, 1.0F, "root patch x should derive u1 from patch id");
    require_close(bounds.v1, 0.0F, "root patch y should derive v1 from patch id");
}

void test_planet_surface_child_id_derives_parent_quadrants() {
    const cubey::projects::planet::PlanetConfig config{
        .patches_per_face = 2,
        .max_lod_level = 2,
    };
    const cubey::projects::planet::PlanetSurfacePatchId parent{
        .face = 4,
        .level = 0,
        .x = 1,
        .y = 0,
    };

    const cubey::projects::planet::PlanetSurfacePatchId child =
        cubey::projects::planet::planet_surface_child_patch_id(parent, 2U);
    require(child.face == parent.face, "child patch should keep parent face");
    require(child.level == 1U, "child patch should increment level");
    require(child.x == 2U, "child patch should double parent x plus child x offset");
    require(child.y == 1U, "child patch should double parent y plus child y offset");

    const cubey::projects::planet::PlanetSurfacePatchBounds bounds =
        cubey::projects::planet::planet_surface_patch_bounds(config, child);
    require_close(bounds.u0, 0.0F, "child quadrant should derive u0 from child id");
    require_close(bounds.v0, -0.5F, "child quadrant should derive v0 from child id");
    require_close(bounds.u1, 0.5F, "child quadrant should derive u1 from child id");
    require_close(bounds.v1, 0.0F, "child quadrant should derive v1 from child id");
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
        .terrain_enabled = false,
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

void test_planet_surface_terrain_displaces_within_height_bounds() {
    const cubey::projects::planet::PlanetConfig config{
        .radius_m = 1200.0F,
        .patches_per_face = 1,
        .patch_resolution = 8,
        .max_lod_level = 0,
        .skirts_enabled = false,
        .terrain_enabled = true,
        .terrain_height_scale_m = 80.0F,
        .terrain_noise_scale = 3.0F,
        .terrain_seed = 42U,
    };
    const cubey::projects::planet::PlanetSurfaceBuildResult result =
        cubey::projects::planet::make_planet_surface_mesh(config);

    bool found_displaced_vertex = false;
    for (const cubey::render::VertexPositionColorNormalUv& vertex : result.mesh.vertices) {
        const float radius = std::sqrt(vertex.position[0] * vertex.position[0] +
                                       vertex.position[1] * vertex.position[1] +
                                       vertex.position[2] * vertex.position[2]);
        require(radius >= config.radius_m - config.terrain_height_scale_m - 0.1F &&
                    radius <= config.radius_m + config.terrain_height_scale_m + 0.1F,
                "planet terrain vertices should stay within configured height bounds");
        if (std::abs(radius - config.radius_m) > 0.5F) {
            found_displaced_vertex = true;
        }
    }
    require(found_displaced_vertex, "planet terrain should visibly displace at least one vertex");
}

void test_planet_surface_terrain_detail_controls_change_shape() {
    const cubey::projects::planet::PlanetConfig coarse_config{
        .radius_m = 1200.0F,
        .patches_per_face = 1,
        .patch_resolution = 8,
        .max_lod_level = 0,
        .skirts_enabled = false,
        .terrain_enabled = true,
        .terrain_height_scale_m = 80.0F,
        .terrain_noise_scale = 3.0F,
        .terrain_seed = 42U,
        .terrain_mid_detail_strength = 0.0F,
        .terrain_fine_detail_strength = 0.0F,
    };
    cubey::projects::planet::PlanetConfig detailed_config = coarse_config;
    detailed_config.terrain_mid_detail_strength = 0.75F;
    detailed_config.terrain_fine_detail_strength = 0.30F;
    detailed_config.terrain_fine_detail_scale = 10.0F;

    const cubey::projects::planet::PlanetSurfaceBuildResult coarse =
        cubey::projects::planet::make_planet_surface_mesh(coarse_config);
    const cubey::projects::planet::PlanetSurfaceBuildResult detailed =
        cubey::projects::planet::make_planet_surface_mesh(detailed_config);

    require(coarse.mesh.vertices.size() == detailed.mesh.vertices.size(),
            "terrain detail controls should not change patch topology");
    bool found_changed_vertex = false;
    for (std::size_t index = 0; index < coarse.mesh.vertices.size(); ++index) {
        const cubey::render::VertexPositionColorNormalUv& a = coarse.mesh.vertices[index];
        const cubey::render::VertexPositionColorNormalUv& b = detailed.mesh.vertices[index];
        const float delta = std::abs(a.position[0] - b.position[0]) +
                            std::abs(a.position[1] - b.position[1]) +
                            std::abs(a.position[2] - b.position[2]);
        if (delta > 0.25F) {
            found_changed_vertex = true;
            break;
        }
    }
    require(found_changed_vertex, "terrain detail controls should affect generated terrain shape");
}

void test_planet_surface_field_disabled_terrain_returns_sphere_sample() {
    const cubey::projects::planet::PlanetConfig config{
        .radius_m = 1200.0F,
        .patches_per_face = 1,
        .patch_resolution = 4,
        .max_lod_level = 0,
        .terrain_enabled = false,
    };
    const cubey::projects::planet::PlanetSurfaceSample sample =
        cubey::projects::planet::planet_surface_sample_field(
            config, cubey::projects::planet::PlanetSurfacePatchId{.face = 4}, 0.25F, -0.5F);

    require_close(sample.height_m, 0.0F, "disabled terrain field should have zero height");
    require_close(sample.normalized_elevation, 0.0F,
                  "disabled terrain field should have zero normalized elevation");
    require_close(sample.normalized_slope, 0.0F,
                  "disabled terrain field should have zero normalized slope");
    require(glm::length(sample.normal - sample.sphere_normal) < 0.0001F,
            "disabled terrain field normal should match sphere normal");
    require(std::abs(static_cast<float>(glm::length(sample.world_position_m)) - config.radius_m) <
                0.1F,
            "disabled terrain field position should stay on planet radius");
}

void test_planet_surface_field_is_deterministic_for_seed() {
    const cubey::projects::planet::PlanetConfig config{
        .radius_m = 1200.0F,
        .patches_per_face = 1,
        .patch_resolution = 4,
        .max_lod_level = 0,
        .terrain_enabled = true,
        .terrain_height_scale_m = 80.0F,
        .terrain_noise_scale = 3.0F,
        .terrain_seed = 42U,
    };
    const cubey::projects::planet::PlanetSurfacePatchId id{.face = 4};

    const cubey::projects::planet::PlanetSurfaceSample a =
        cubey::projects::planet::planet_surface_sample_field(config, id, 0.25F, -0.5F);
    const cubey::projects::planet::PlanetSurfaceSample b =
        cubey::projects::planet::planet_surface_sample_field(config, id, 0.25F, -0.5F);
    cubey::projects::planet::PlanetConfig changed_seed = config;
    changed_seed.terrain_seed = 43U;
    const cubey::projects::planet::PlanetSurfaceSample c =
        cubey::projects::planet::planet_surface_sample_field(changed_seed, id, 0.25F, -0.5F);

    require_close(a.height_m, b.height_m,
                  "planet surface field should be deterministic for the same seed");
    require(std::abs(a.height_m - c.height_m) > 0.0001F,
            "planet surface field should respond to seed changes");
}

void test_planet_surface_field_reports_bounded_height_normal_and_slope() {
    const cubey::projects::planet::PlanetConfig config{
        .radius_m = 1200.0F,
        .patches_per_face = 1,
        .patch_resolution = 8,
        .max_lod_level = 0,
        .terrain_enabled = true,
        .terrain_height_scale_m = 80.0F,
        .terrain_noise_scale = 3.0F,
        .terrain_seed = 42U,
    };
    const cubey::projects::planet::PlanetSurfacePatchId id{.face = 2};
    const cubey::projects::planet::PlanetSurfaceSample sample =
        cubey::projects::planet::planet_surface_sample_field(config, id, -0.25F, 0.5F);

    require(sample.height_m >= -config.terrain_height_scale_m &&
                sample.height_m <= config.terrain_height_scale_m,
            "planet surface field height should stay in configured bounds");
    require(sample.normalized_elevation >= -1.0F && sample.normalized_elevation <= 1.0F,
            "planet surface field normalized elevation should stay in range");
    require(sample.normalized_slope >= 0.0F && sample.normalized_slope <= 1.0F,
            "planet surface field normalized slope should stay in range");
    require_close(sample.height_above_sea_m, sample.height_m - config.sea_level_m,
                  "planet surface field should report height above sea level");
    require(sample.water_depth_m >= 0.0F, "planet surface field water depth should be nonnegative");
    require(sample.normalized_bathymetry >= 0.0F && sample.normalized_bathymetry <= 1.0F,
            "planet surface field normalized bathymetry should stay in range");
    require(sample.shoreline_mask >= 0.0F && sample.shoreline_mask <= 1.0F,
            "planet surface field shoreline mask should stay in range");
    require(std::isfinite(sample.normal.x) && std::isfinite(sample.normal.y) &&
                std::isfinite(sample.normal.z),
            "planet surface field normal should be finite");
    require(glm::length(sample.normal) > 0.99F && glm::length(sample.normal) < 1.01F,
            "planet surface field normal should be normalized");
    require(glm::dot(sample.sphere_normal, sample.normal) > 0.25F,
            "planet surface field normal should remain outward-facing");
    require(sample.material == cubey::projects::planet::planet_surface_material(
                                   sample.height_above_sea_m, sample.normalized_elevation,
                                   sample.normalized_slope),
            "planet surface field sample should carry its classified material");
}

void test_planet_surface_field_classifies_material_bands() {
    require(cubey::projects::planet::planet_surface_material(-1.0F, 0.20F, 0.0F) ==
                cubey::projects::planet::PlanetSurfaceMaterial::Water,
            "planet surface material should classify below-sea terrain");
    require(cubey::projects::planet::planet_surface_material(1.0F, 0.0F, 0.05F) ==
                cubey::projects::planet::PlanetSurfaceMaterial::Lowland,
            "planet surface material should classify low gentle terrain");
    require(cubey::projects::planet::planet_surface_material(1.0F, 0.30F, 0.05F) ==
                cubey::projects::planet::PlanetSurfaceMaterial::Highland,
            "planet surface material should classify higher terrain");
    require(cubey::projects::planet::planet_surface_material(1.0F, 0.70F, 0.05F) ==
                cubey::projects::planet::PlanetSurfaceMaterial::Snow,
            "planet surface material should classify high snow terrain");

    const cubey::math::Vec3 water = cubey::projects::planet::planet_surface_material_color(
        cubey::projects::planet::PlanetSurfaceMaterial::Water, -0.2F, 0.0F);
    const cubey::math::Vec3 highland = cubey::projects::planet::planet_surface_material_color(
        cubey::projects::planet::PlanetSurfaceMaterial::Highland, 0.4F, 0.5F);
    require(water.z > water.x && water.z > water.y,
            "planet water material should be blue-dominant");
    require(highland.x > water.x && highland.y > water.y,
            "planet highland material should be brighter than water");
}

void test_planet_surface_field_reports_sea_level_and_bathymetry() {
    const cubey::projects::planet::PlanetConfig config{
        .radius_m = 1200.0F,
        .patches_per_face = 1,
        .patch_resolution = 4,
        .max_lod_level = 0,
        .terrain_enabled = true,
        .terrain_height_scale_m = 80.0F,
        .sea_level_m = 20.0F,
        .bathymetry_depth_scale_m = 40.0F,
        .shoreline_width_m = 10.0F,
    };

    require_close(cubey::projects::planet::planet_surface_height_above_sea_m(config, -10.0F),
                  -30.0F, "planet field should measure height relative to sea level");
    require_close(cubey::projects::planet::planet_surface_water_depth_m(config, -10.0F), 30.0F,
                  "planet field should report positive water depth below sea level");
    require_close(cubey::projects::planet::planet_surface_water_depth_m(config, 30.0F), 0.0F,
                  "planet field should report zero water depth above sea level");
    require_close(cubey::projects::planet::planet_surface_normalized_bathymetry(config, -20.0F),
                  1.0F, "planet field should clamp full bathymetry at configured depth scale");
    require(cubey::projects::planet::planet_surface_shoreline_mask(config, 20.0F) > 0.99F,
            "planet field should mark exact sea level as shoreline");
    require_close(cubey::projects::planet::planet_surface_shoreline_mask(config, 35.0F), 0.0F,
                  "planet field should fade shoreline mask outside configured width");

    const cubey::projects::planet::PlanetSurfaceSample sample =
        cubey::projects::planet::planet_surface_sample_field(
            config, cubey::projects::planet::PlanetSurfacePatchId{.face = 4}, 0.0F, 0.0F);
    require(sample.water_depth_m >= 0.0F,
            "planet surface sample should expose nonnegative water depth");
    require(sample.normalized_bathymetry >= 0.0F && sample.normalized_bathymetry <= 1.0F,
            "planet surface sample should expose clamped normalized bathymetry");
    require(sample.shoreline_mask >= 0.0F && sample.shoreline_mask <= 1.0F,
            "planet surface sample should expose clamped shoreline mask");
}

void test_planet_surface_tile_key_round_trips_patch_identity() {
    const cubey::projects::planet::PlanetSurfacePatchId patch_id{
        .face = 5,
        .level = 3,
        .x = 4,
        .y = 7,
    };

    const cubey::projects::planet::PlanetSurfaceTileKey key =
        cubey::projects::planet::planet_surface_tile_key_from_patch_id(patch_id);
    const cubey::projects::planet::PlanetSurfacePatchId round_trip =
        cubey::projects::planet::planet_surface_patch_id_from_tile_key(key);

    require(round_trip == patch_id, "planet surface tile keys should preserve patch identity");
}

void test_planet_surface_tile_payload_is_deterministic_and_bounded() {
    const cubey::projects::planet::PlanetConfig config{
        .radius_m = 1200.0F,
        .patches_per_face = 2,
        .patch_resolution = 4,
        .max_lod_level = 2,
        .terrain_enabled = true,
        .terrain_height_scale_m = 80.0F,
        .terrain_noise_scale = 3.0F,
        .terrain_seed = 77U,
        .sea_level_m = -12.0F,
        .bathymetry_depth_scale_m = 100.0F,
        .shoreline_width_m = 20.0F,
    };
    const cubey::projects::planet::PlanetSurfaceTileKey key{
        .face = 2,
        .level = 1,
        .x = 1,
        .y = 0,
    };

    const cubey::projects::planet::PlanetSurfaceTilePayload first =
        cubey::projects::planet::make_planet_surface_tile_payload(config, key, 3U);
    const cubey::projects::planet::PlanetSurfaceTilePayload second =
        cubey::projects::planet::make_planet_surface_tile_payload(config, key, 3U);

    require(first.key == key, "planet surface tile payload should retain its key");
    require(first.source == cubey::projects::planet::PlanetSurfaceTileSource::Procedural,
            "planet surface tile payload should report the procedural source");
    require(first.generator_revision == config.terrain_seed,
            "planet surface tile payload should expose the procedural generator revision");
    require(first.summary.sample_count == 16U,
            "planet surface tile payload should sample a square grid including edges");
    require(first.summary.min_height_m <= first.summary.max_height_m,
            "planet surface tile payload should report ordered height bounds");
    require(first.summary.min_height_m >= -config.terrain_height_scale_m - 0.001F,
            "planet surface tile payload should respect minimum terrain height");
    require(first.summary.max_height_m <= config.terrain_height_scale_m + 0.001F,
            "planet surface tile payload should respect maximum terrain height");
    require(first.summary.max_water_depth_m >= 0.0F,
            "planet surface tile payload should report nonnegative water depth");
    require(first.summary.max_shoreline_mask >= 0.0F && first.summary.max_shoreline_mask <= 1.0F,
            "planet surface tile payload should report bounded shoreline masks");
    require(first.summary.max_normalized_slope >= 0.0F &&
                first.summary.max_normalized_slope <= 1.0F,
            "planet surface tile payload should report bounded slope");
    require(first.summary.material_mask != 0U,
            "planet surface tile payload should report at least one material");
    require_close(first.summary.min_height_m, second.summary.min_height_m,
                  "planet surface tile payload should be deterministic");
    require_close(first.summary.max_height_m, second.summary.max_height_m,
                  "planet surface tile payload should be deterministic");
}

void test_planet_surface_terrain_normals_are_finite_and_outward() {
    const cubey::projects::planet::PlanetConfig config{
        .radius_m = 1200.0F,
        .patches_per_face = 1,
        .patch_resolution = 8,
        .max_lod_level = 0,
        .skirts_enabled = false,
        .terrain_enabled = true,
        .terrain_height_scale_m = 60.0F,
        .terrain_noise_scale = 2.5F,
        .terrain_seed = 99U,
    };
    const cubey::projects::planet::PlanetSurfaceBuildResult result =
        cubey::projects::planet::make_planet_surface_mesh(config);

    for (const cubey::render::VertexPositionColorNormalUv& vertex : result.mesh.vertices) {
        const cubey::math::Vec3 position{vertex.position[0], vertex.position[1],
                                         vertex.position[2]};
        const cubey::math::Vec3 normal{vertex.normal[0], vertex.normal[1], vertex.normal[2]};
        require(std::isfinite(normal.x) && std::isfinite(normal.y) && std::isfinite(normal.z),
                "planet terrain normals should be finite");
        require(glm::length(normal) > 0.99F && glm::length(normal) < 1.01F,
                "planet terrain normals should be normalized");
        require(glm::dot(glm::normalize(position), normal) > 0.25F,
                "planet terrain normals should remain roughly outward-facing");
    }
}

void test_planet_surface_refined_terrain_normals_are_finite_and_outward() {
    const cubey::projects::planet::PlanetConfig config{
        .radius_m = 1200.0F,
        .patches_per_face = 1,
        .patch_resolution = 4,
        .max_lod_level = 3,
        .lod_target_edge_px = 0.0001F,
        .skirts_enabled = false,
        .terrain_enabled = true,
        .terrain_height_scale_m = 60.0F,
        .terrain_noise_scale = 2.5F,
        .terrain_seed = 99U,
    };
    const cubey::projects::planet::PlanetSurfaceView view{
        .camera_world_position_m = {0.0, 0.0, 1800.0},
        .camera_forward_world = {0.0F, 0.0F, -1.0F},
        .culling_enabled = false,
    };
    const cubey::projects::planet::PlanetSurfaceBuildResult result =
        cubey::projects::planet::make_planet_surface_mesh(config, view);

    require(result.diagnostics.max_lod_level == 3U,
            "refined terrain normal test should exercise high-LOD patches");
    for (const cubey::render::VertexPositionColorNormalUv& vertex : result.mesh.vertices) {
        const cubey::math::Vec3 position{vertex.position[0], vertex.position[1],
                                         vertex.position[2]};
        const cubey::math::Vec3 normal{vertex.normal[0], vertex.normal[1], vertex.normal[2]};
        require(std::isfinite(normal.x) && std::isfinite(normal.y) && std::isfinite(normal.z),
                "refined planet terrain normals should be finite");
        require(glm::length(normal) > 0.99F && glm::length(normal) < 1.01F,
                "refined planet terrain normals should be normalized");
        require(glm::dot(glm::normalize(position), normal) > 0.25F,
                "refined planet terrain normals should remain roughly outward-facing");
    }
}

void test_planet_surface_triangles_are_wound_outward() {
    const cubey::projects::planet::PlanetConfig config{
        .radius_m = 1200.0F,
        .patches_per_face = 1,
        .patch_resolution = 3,
        .max_lod_level = 0,
        .skirts_enabled = false,
        .terrain_enabled = false,
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
        .terrain_enabled = false,
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

void test_planet_surface_planner_refines_visible_patches_with_fallback_coverage() {
    const cubey::projects::planet::PlanetConfig config{
        .radius_m = 1000.0F,
        .patches_per_face = 2,
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

    require(plan.diagnostics.visible_patch_count >= 24U,
            "planet planner should keep coarse fallback coverage while refining");
    require(
        plan.diagnostics.planned_patch_count > plan.diagnostics.visible_patch_count,
        "planet planner should count subdivided parent candidates separately from render leaves");
    require(plan.diagnostics.base_patch_count > 0U,
            "planet planner should retain base patches where refinement is not useful or visible");
    require(plan.diagnostics.refined_patch_count > 0U,
            "planet planner should select refined child patches near the camera");
    require(plan.diagnostics.subdivided_patch_count > 0U,
            "planet planner should report parent patches that hand off to children");
    require(plan.diagnostics.culled_horizon_count + plan.diagnostics.culled_view_count > 0U,
            "planet planner should report refinement cull reasons");
    require(plan.diagnostics.refinement_fallback_patch_count > 0U,
            "planet planner should report parent patches selected as refinement fallback");
    require(plan.diagnostics.min_cell_edge_m_by_lod[0] > 0.0F &&
                plan.diagnostics.max_cell_edge_m_by_lod[0] >=
                    plan.diagnostics.min_cell_edge_m_by_lod[0],
            "planet planner should report selected level-zero cell edge range");
    require(plan.diagnostics.min_cell_edge_m_by_lod[1] > 0.0F &&
                plan.diagnostics.max_cell_edge_m_by_lod[1] >=
                    plan.diagnostics.min_cell_edge_m_by_lod[1],
            "planet planner should report selected level-one cell edge range");
}

void test_planet_surface_mesh_consumes_selected_patch_instances() {
    const cubey::projects::planet::PlanetConfig config{
        .radius_m = 1000.0F,
        .patches_per_face = 2,
        .patch_resolution = 2,
        .max_lod_level = 1,
        .lod_target_edge_px = 1.0F,
        .skirts_enabled = false,
    };
    const cubey::projects::planet::PlanetSurfaceView view{
        .camera_world_position_m = {0.0, 0.0, 1500.0},
        .camera_forward_world = {0.0F, 0.0F, -1.0F},
        .culling_enabled = true,
    };
    const cubey::projects::planet::PlanetSurfacePatchPlan plan =
        cubey::projects::planet::plan_planet_surface_patches(config, view);
    const cubey::projects::planet::PlanetSurfaceBuildResult result =
        cubey::projects::planet::make_planet_surface_mesh(config, view, {}, plan);

    require(plan.selected_patches.size() == plan.diagnostics.patch_count,
            "planet planner selected patch list should match patch diagnostics");
    require(result.diagnostics.patch_count == plan.diagnostics.patch_count,
            "planet mesh build should consume the selected planner patch instances");
    require(result.diagnostics.vertex_count == plan.diagnostics.patch_count *
                                                   (config.patch_resolution + 1U) *
                                                   (config.patch_resolution + 1U),
            "planet mesh build should emit one grid for each selected patch instance");
}

void test_planet_surface_patch_grid_mesh_is_reusable() {
    const cubey::projects::planet::PlanetConfig config{
        .patches_per_face = 2,
        .patch_resolution = 5,
        .max_lod_level = 1,
        .skirts_enabled = false,
    };

    const cubey::projects::planet::PlanetPatchGridMeshData grid =
        cubey::projects::planet::make_planet_patch_grid_mesh(config);

    require(grid.vertices.size() == 36U,
            "planet patch grid should emit one reusable vertex grid per resolution");
    require(grid.indices.size() == 5U * 5U * 6U,
            "planet patch grid should emit two triangles per grid cell");
    require_close(grid.vertices.front().uv[0], 0.0F, "planet patch grid should start at u=0");
    require_close(grid.vertices.front().uv[1], 0.0F, "planet patch grid should start at v=0");
    require_close(grid.vertices.back().uv[0], 1.0F, "planet patch grid should end at u=1");
    require_close(grid.vertices.back().uv[1], 1.0F, "planet patch grid should end at v=1");
}

void test_planet_surface_patch_grid_mesh_can_include_skirts() {
    const cubey::projects::planet::PlanetConfig config{
        .patches_per_face = 1,
        .patch_resolution = 3,
        .max_lod_level = 0,
        .skirts_enabled = true,
    };

    const cubey::projects::planet::PlanetPatchGridMeshData grid =
        cubey::projects::planet::make_planet_patch_grid_mesh(config);

    require(grid.vertices.size() == 16U + 24U,
            "planet skirt grid should duplicate bottom vertices for each edge segment");
    require(grid.indices.size() == (3U * 3U * 6U) + (4U * 3U * 12U),
            "planet skirt grid should add double-sided skirt triangles around each edge segment");
    require_close(grid.vertices.back().skirt, 1.0F,
                  "planet skirt grid should mark duplicated bottom vertices");
}

void test_planet_surface_gpu_instances_preserve_patch_identity() {
    const cubey::projects::planet::PlanetConfig config{
        .radius_m = 1000.0F,
        .patches_per_face = 2,
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
    const std::vector<cubey::projects::planet::PlanetSurfaceGpuPatchInstance> instances =
        cubey::projects::planet::make_planet_surface_gpu_patch_instances(plan);

    require(instances.size() == plan.selected_patches.size(),
            "planet GPU instance list should match selected patch count");
    for (std::size_t index = 0; index < instances.size(); ++index) {
        const cubey::projects::planet::PlanetSurfacePatchInstance& patch =
            plan.selected_patches[index];
        const cubey::projects::planet::PlanetSurfaceGpuPatchInstance& instance = instances[index];
        require(instance.face == patch.id.face, "planet GPU instance should keep patch face");
        require(instance.level == patch.id.level, "planet GPU instance should keep patch level");
        require(instance.x == patch.id.x, "planet GPU instance should keep patch x");
        require(instance.y == patch.id.y, "planet GPU instance should keep patch y");
        require_close(instance.screen_error_px, patch.screen_error_px,
                      "planet GPU instance should keep screen-error diagnostic");
    }
}

void test_planet_surface_cpu_mesh_rejects_too_dense_live_lod() {
    cubey::projects::planet::PlanetConfig config{
        .patches_per_face = 2,
        .patch_resolution = cubey::projects::planet::kPlanetDefaultPatchResolution,
        .max_lod_level = cubey::projects::planet::kPlanetMaxLiveLodLevel,
    };
    cubey::projects::planet::validate_planet_config(config);

    try {
        static_cast<void>(cubey::projects::planet::make_planet_surface_mesh(config));
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("planet CPU debug mesh should reject too-dense live LOD settings");
}

void test_planet_surface_planner_keeps_fallback_when_camera_looks_away() {
    const cubey::projects::planet::PlanetConfig config{
        .radius_m = 1000.0F,
        .patches_per_face = 2,
        .patch_resolution = 2,
        .max_lod_level = 1,
        .lod_target_edge_px = 1.0F,
    };
    const cubey::projects::planet::PlanetSurfaceView view{
        .camera_world_position_m = {0.0, 0.0, 1500.0},
        .camera_forward_world = {0.0F, 0.0F, 1.0F},
        .culling_enabled = true,
    };

    const cubey::projects::planet::PlanetSurfacePatchPlan plan =
        cubey::projects::planet::plan_planet_surface_patches(config, view);

    require(plan.diagnostics.visible_patch_count == 24U,
            "planet planner should keep root fallback coverage when the camera looks away");
    require(plan.diagnostics.base_patch_count == 24U,
            "planet planner should render base patches when no refinement is visible");
    require(plan.diagnostics.refined_patch_count == 0U,
            "planet planner should not refine patches behind the camera");
    require(plan.diagnostics.refinement_fallback_patch_count == 24U,
            "planet planner should report root patches selected as refinement fallback");
    require(plan.diagnostics.culled_view_count > 0U,
            "planet planner should attribute blocked refinement to view culling");
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

void test_planet_surface_planner_records_high_lod_diagnostics() {
    const cubey::projects::planet::PlanetConfig config{
        .radius_m = 1000.0F,
        .patches_per_face = 1,
        .patch_resolution = 1,
        .max_lod_level = 4,
        .lod_target_edge_px = 0.0001F,
    };
    const cubey::projects::planet::PlanetSurfaceView view{
        .camera_world_position_m = {0.0, 0.0, 1500.0},
        .camera_forward_world = {0.0F, 0.0F, -1.0F},
        .culling_enabled = false,
    };

    const cubey::projects::planet::PlanetSurfacePatchPlan plan =
        cubey::projects::planet::plan_planet_surface_patches(config, view);

    require(plan.diagnostics.max_lod_level == 4U,
            "planet planner should select the configured high LOD level");
    require(plan.diagnostics.patches_by_lod[4] > 0U,
            "planet planner should record patch counts above the old four-level cap");
    require(plan.diagnostics.min_cell_edge_m_by_lod[4] > 0.0F &&
                plan.diagnostics.max_cell_edge_m_by_lod[4] >=
                    plan.diagnostics.min_cell_edge_m_by_lod[4],
            "planet planner should record high-LOD cell-size diagnostics");
}

void test_planet_surface_default_lod_reaches_near_camera_detail() {
    const cubey::projects::planet::PlanetConfig config{};
    const cubey::projects::planet::PlanetSurfaceView view{
        .camera_world_position_m = {0.0, 0.0, config.radius_m + 50000.0},
        .camera_forward_world = {0.0F, 0.0F, -1.0F},
        .culling_enabled = false,
    };

    const cubey::projects::planet::PlanetSurfacePatchPlan plan =
        cubey::projects::planet::plan_planet_surface_patches(config, view);

    require(plan.diagnostics.max_lod_level >= 4U,
            "default planet LOD should reach higher levels near the camera");
    require(plan.diagnostics.patch_count <= cubey::projects::planet::kPlanetMaxLivePatchInstances,
            "default planet LOD should stay inside the live patch budget");
}

void test_planet_surface_hysteresis_delays_split() {
    const cubey::projects::planet::PlanetConfig root_config{
        .patches_per_face = 1,
        .patch_resolution = 1,
        .max_lod_level = 0,
        .skirts_enabled = false,
        .terrain_enabled = false,
    };
    const cubey::projects::planet::PlanetSurfaceView view{
        .camera_world_position_m = {0.0, 0.0, root_config.radius_m + 50000.0},
        .camera_forward_world = {0.0F, 0.0F, -1.0F},
        .culling_enabled = false,
    };
    const cubey::projects::planet::PlanetSurfacePatchPlan root_plan =
        cubey::projects::planet::plan_planet_surface_patches(root_config, view);
    std::vector<cubey::projects::planet::PlanetSurfacePatchId> previous_roots{};
    previous_roots.reserve(root_plan.selected_patches.size());
    for (const cubey::projects::planet::PlanetSurfacePatchInstance& patch :
         root_plan.selected_patches) {
        previous_roots.push_back(patch.id);
    }

    cubey::projects::planet::PlanetConfig config = root_config;
    config.max_lod_level = 1;
    config.lod_hysteresis = 0.20F;
    config.lod_target_edge_px = root_plan.diagnostics.max_screen_error_px / 1.05F;

    const cubey::projects::planet::PlanetSurfacePatchPlan raw_plan =
        cubey::projects::planet::plan_planet_surface_patches(config, view);
    const cubey::projects::planet::PlanetSurfacePatchPlan stable_plan =
        cubey::projects::planet::plan_planet_surface_patches(
            config, view,
            cubey::projects::planet::PlanetSurfacePatchSelectionHints{
                .previous_selected_patches = previous_roots,
            });

    require(raw_plan.diagnostics.max_lod_level == 1U,
            "raw planet LOD should split just above the target");
    require(stable_plan.diagnostics.max_lod_level == 0U,
            "planet LOD hysteresis should keep previous parent patches stable");
    require(stable_plan.diagnostics.hysteresis_delayed_split_count > 0U,
            "planet LOD hysteresis should record delayed splits");
}

void test_planet_surface_hysteresis_delays_merge() {
    const cubey::projects::planet::PlanetConfig root_config{
        .patches_per_face = 1,
        .patch_resolution = 1,
        .max_lod_level = 0,
        .skirts_enabled = false,
        .terrain_enabled = false,
    };
    const cubey::projects::planet::PlanetSurfaceView view{
        .camera_world_position_m = {0.0, 0.0, root_config.radius_m + 50000.0},
        .camera_forward_world = {0.0F, 0.0F, -1.0F},
        .culling_enabled = false,
    };
    const cubey::projects::planet::PlanetSurfacePatchPlan root_plan =
        cubey::projects::planet::plan_planet_surface_patches(root_config, view);
    std::vector<cubey::projects::planet::PlanetSurfacePatchId> previous_children{};
    previous_children.reserve(root_plan.selected_patches.size() * 4U);
    for (const cubey::projects::planet::PlanetSurfacePatchInstance& patch :
         root_plan.selected_patches) {
        for (std::uint32_t child = 0; child < 4U; ++child) {
            previous_children.push_back(
                cubey::projects::planet::planet_surface_child_patch_id(patch.id, child));
        }
    }

    cubey::projects::planet::PlanetConfig config = root_config;
    config.max_lod_level = 1;
    config.lod_hysteresis = 0.20F;
    config.lod_target_edge_px = root_plan.diagnostics.max_screen_error_px / 0.95F;

    const cubey::projects::planet::PlanetSurfacePatchPlan raw_plan =
        cubey::projects::planet::plan_planet_surface_patches(config, view);
    const cubey::projects::planet::PlanetSurfacePatchPlan stable_plan =
        cubey::projects::planet::plan_planet_surface_patches(
            config, view,
            cubey::projects::planet::PlanetSurfacePatchSelectionHints{
                .previous_selected_patches = previous_children,
            });

    require(raw_plan.diagnostics.max_lod_level == 0U,
            "raw planet LOD should merge just below the target");
    require(stable_plan.diagnostics.max_lod_level == 1U,
            "planet LOD hysteresis should keep previous child patches stable");
    require(stable_plan.diagnostics.hysteresis_delayed_merge_count > 0U,
            "planet LOD hysteresis should record delayed merges");
}

void test_planet_surface_planner_falls_back_at_live_patch_budget() {
    const cubey::projects::planet::PlanetConfig config{
        .patches_per_face = 2,
        .patch_resolution = 1,
        .max_lod_level = cubey::projects::planet::kPlanetMaxLiveLodLevel,
        .lod_target_edge_px = 0.0001F,
        .skirts_enabled = false,
        .terrain_enabled = false,
    };
    const cubey::projects::planet::PlanetSurfaceView view{
        .camera_world_position_m = {0.0, 0.0, config.radius_m + 50000.0},
        .camera_forward_world = {0.0F, 0.0F, -1.0F},
        .culling_enabled = false,
    };

    const cubey::projects::planet::PlanetSurfacePatchPlan plan =
        cubey::projects::planet::plan_planet_surface_patches(config, view);

    require(plan.diagnostics.patch_count <= cubey::projects::planet::kPlanetMaxLivePatchInstances,
            "planet planner should stay inside the live patch budget");
    require(plan.diagnostics.budget_fallback_patch_count > 0U,
            "planet planner should record live patch budget fallback");
}

void test_planet_surface_skirts_add_seam_geometry() {
    const cubey::projects::planet::PlanetConfig no_skirts{
        .radius_m = 1200.0F,
        .patches_per_face = 1,
        .patch_resolution = 2,
        .max_lod_level = 0,
        .skirts_enabled = false,
        .terrain_enabled = false,
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
        .terrain_enabled = false,
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

void test_planet_surface_metric_debug_views_parse() {
    require(cubey::projects::planet::planet_debug_view_from_string("cell-edge") ==
                cubey::projects::planet::PlanetDebugView::CellEdge,
            "planet debug view should parse cell-edge");
    require(cubey::projects::planet::planet_debug_view_from_string("terrain-height") ==
                cubey::projects::planet::PlanetDebugView::TerrainHeight,
            "planet debug view should parse terrain-height");
    require(cubey::projects::planet::planet_debug_view_from_string("terrain-slope") ==
                cubey::projects::planet::PlanetDebugView::TerrainSlope,
            "planet debug view should parse terrain-slope");
    require(cubey::projects::planet::planet_debug_view_from_string("terrain-material") ==
                cubey::projects::planet::PlanetDebugView::TerrainMaterial,
            "planet debug view should parse terrain-material");
    require(cubey::projects::planet::planet_debug_view_from_string("lod-transition") ==
                cubey::projects::planet::PlanetDebugView::LodTransition,
            "planet debug view should parse lod-transition");
    require(cubey::projects::planet::planet_debug_view_from_string("bathymetry") ==
                cubey::projects::planet::PlanetDebugView::Bathymetry,
            "planet debug view should parse bathymetry");
    require(cubey::projects::planet::planet_debug_view_from_string("shoreline") ==
                cubey::projects::planet::PlanetDebugView::Shoreline,
            "planet debug view should parse shoreline");
    require(cubey::projects::planet::planet_debug_view_from_string("wireframe") ==
                cubey::projects::planet::PlanetDebugView::Wireframe,
            "planet debug view should parse wireframe");
    require(cubey::projects::planet::planet_debug_view_from_string("celestial-planes") ==
                cubey::projects::planet::PlanetDebugView::CelestialPlanes,
            "planet debug view should parse celestial-planes");
    require(std::string_view{cubey::projects::planet::planet_debug_view_name(
                cubey::projects::planet::PlanetDebugView::CellEdge)} == "cell-edge",
            "planet debug view should name cell-edge");
    require(std::string_view{cubey::projects::planet::planet_debug_view_name(
                cubey::projects::planet::PlanetDebugView::TerrainHeight)} == "terrain-height",
            "planet debug view should name terrain-height");
    require(std::string_view{cubey::projects::planet::planet_debug_view_name(
                cubey::projects::planet::PlanetDebugView::TerrainSlope)} == "terrain-slope",
            "planet debug view should name terrain-slope");
    require(std::string_view{cubey::projects::planet::planet_debug_view_name(
                cubey::projects::planet::PlanetDebugView::TerrainMaterial)} == "terrain-material",
            "planet debug view should name terrain-material");
    require(std::string_view{cubey::projects::planet::planet_debug_view_name(
                cubey::projects::planet::PlanetDebugView::LodTransition)} == "lod-transition",
            "planet debug view should name lod-transition");
    require(std::string_view{cubey::projects::planet::planet_debug_view_name(
                cubey::projects::planet::PlanetDebugView::Bathymetry)} == "bathymetry",
            "planet debug view should name bathymetry");
    require(std::string_view{cubey::projects::planet::planet_debug_view_name(
                cubey::projects::planet::PlanetDebugView::Shoreline)} == "shoreline",
            "planet debug view should name shoreline");
    require(std::string_view{cubey::projects::planet::planet_debug_view_name(
                cubey::projects::planet::PlanetDebugView::Wireframe)} == "wireframe",
            "planet debug view should name wireframe");
    require(std::string_view{cubey::projects::planet::planet_debug_view_name(
                cubey::projects::planet::PlanetDebugView::CelestialPlanes)} == "celestial-planes",
            "planet debug view should name celestial-planes");
}

void test_planet_surface_planner_records_lod_transition_pressure() {
    const cubey::projects::planet::PlanetConfig config{
        .radius_m = 1000.0F,
        .patches_per_face = 1,
        .patch_resolution = 2,
        .max_lod_level = 0,
        .lod_target_edge_px = 12.0F,
    };
    const cubey::projects::planet::PlanetSurfaceView view{
        .camera_world_position_m = {0.0, 0.0, 1500.0},
        .camera_forward_world = {0.0F, 0.0F, -1.0F},
        .viewport_height_px = 10.0F,
        .culling_enabled = false,
    };

    const cubey::projects::planet::PlanetSurfacePatchPlan plan =
        cubey::projects::planet::plan_planet_surface_patches(config, view);

    require(plan.diagnostics.transition_candidate_count > 0U,
            "planet planner should count patches near the LOD transition threshold");
    require(plan.diagnostics.max_transition_pressure > 0.0F,
            "planet planner should report positive transition pressure near the threshold");
}

} // namespace

int main() {
    try {
        test_planet_surface_patch_id_derives_root_bounds();
        test_planet_surface_child_id_derives_parent_quadrants();
        test_planet_surface_builds_expected_patch_counts();
        test_planet_surface_vertices_stay_on_radius();
        test_planet_surface_terrain_displaces_within_height_bounds();
        test_planet_surface_terrain_detail_controls_change_shape();
        test_planet_surface_field_disabled_terrain_returns_sphere_sample();
        test_planet_surface_field_is_deterministic_for_seed();
        test_planet_surface_field_reports_bounded_height_normal_and_slope();
        test_planet_surface_field_classifies_material_bands();
        test_planet_surface_field_reports_sea_level_and_bathymetry();
        test_planet_surface_tile_key_round_trips_patch_identity();
        test_planet_surface_tile_payload_is_deterministic_and_bounded();
        test_planet_surface_terrain_normals_are_finite_and_outward();
        test_planet_surface_refined_terrain_normals_are_finite_and_outward();
        test_planet_surface_triangles_are_wound_outward();
        test_planet_surface_lod_subdivides_near_camera_patches();
        test_planet_surface_can_build_camera_relative_vertices();
        test_planet_surface_planner_refines_visible_patches_with_fallback_coverage();
        test_planet_surface_mesh_consumes_selected_patch_instances();
        test_planet_surface_patch_grid_mesh_is_reusable();
        test_planet_surface_patch_grid_mesh_can_include_skirts();
        test_planet_surface_gpu_instances_preserve_patch_identity();
        test_planet_surface_cpu_mesh_rejects_too_dense_live_lod();
        test_planet_surface_planner_keeps_fallback_when_camera_looks_away();
        test_planet_surface_planner_selects_near_lod();
        test_planet_surface_planner_records_high_lod_diagnostics();
        test_planet_surface_default_lod_reaches_near_camera_detail();
        test_planet_surface_hysteresis_delays_split();
        test_planet_surface_hysteresis_delays_merge();
        test_planet_surface_planner_falls_back_at_live_patch_budget();
        test_planet_surface_skirts_add_seam_geometry();
        test_planet_surface_skirt_vertices_drop_below_radius();
        test_planet_surface_seams_debug_view_parses();
        test_planet_surface_metric_debug_views_parse();
        test_planet_surface_planner_records_lod_transition_pressure();
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "planet_surface_tests: %s\n", error.what());
        return 1;
    }
}
