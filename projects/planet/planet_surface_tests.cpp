#include "planet_camera.h"
#include "planet_local_detail.h"
#include "planet_local_detail_runtime.h"
#include "planet_surface.h"
#include "planet_surface_field.h"
#include "planet_surface_runtime.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
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

[[nodiscard]] cubey::math::Vec3
highest_sampled_terrain_direction(const cubey::projects::planet::PlanetConfig& config) {
    cubey::math::Vec3 best_direction{0.0F, 0.0F, 1.0F};
    float best_height = std::numeric_limits<float>::lowest();
    constexpr std::uint32_t kSampleCount = 128U;
    constexpr float kGoldenAngle = 2.39996314F;
    for (std::uint32_t index = 0U; index < kSampleCount; ++index) {
        const float y =
            -1.0F + (2.0F * (static_cast<float>(index) + 0.5F)) / static_cast<float>(kSampleCount);
        const float radius = std::sqrt(std::max(1.0F - (y * y), 0.0F));
        const float theta = kGoldenAngle * static_cast<float>(index);
        const cubey::math::Vec3 direction = glm::normalize(
            cubey::math::Vec3{std::cos(theta) * radius, y, std::sin(theta) * radius});
        const float height =
            cubey::projects::planet::planet_surface_terrain_height_m(config, direction);
        if (height > best_height) {
            best_height = height;
            best_direction = direction;
        }
    }
    return best_direction;
}

[[nodiscard]] cubey::math::DVec3 to_double(cubey::math::Vec3 value) {
    return {
        static_cast<double>(value.x),
        static_cast<double>(value.y),
        static_cast<double>(value.z),
    };
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

void test_planet_surface_terrain_feature_context_is_bounded() {
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
    const cubey::math::Vec3 directions[] = {
        glm::normalize(cubey::math::Vec3{1.0F, 0.25F, 0.10F}),
        glm::normalize(cubey::math::Vec3{-0.40F, 0.65F, 0.72F}),
        glm::normalize(cubey::math::Vec3{0.20F, -0.85F, 0.48F}),
        highest_sampled_terrain_direction(config),
    };

    for (const cubey::math::Vec3 direction : directions) {
        const cubey::projects::planet::PlanetTerrainFeatureContext features =
            cubey::projects::planet::planet_surface_terrain_feature_context(config, direction);
        require(std::isfinite(features.domain_point.x) && std::isfinite(features.domain_point.y) &&
                    std::isfinite(features.domain_point.z),
                "terrain feature context should expose a finite domain point");
        require(features.continent_mask >= 0.0F && features.continent_mask <= 1.0F,
                "terrain feature context should keep continent mask bounded");
        require(features.mountain_belt >= 0.0F && features.mountain_belt <= 1.0F,
                "terrain feature context should keep mountain belt bounded");
        require(features.valley_network >= 0.0F && features.valley_network <= 1.0F,
                "terrain feature context should keep valley network bounded");
        require(features.relief_gate >= 0.0F && features.relief_gate <= 1.0F,
                "terrain feature context should keep relief gate bounded");
        require(features.plain_gate >= 0.0F && features.plain_gate <= 1.0F,
                "terrain feature context should keep plain gate bounded");
        require(features.land_mask >= 0.0F && features.land_mask <= 1.0F,
                "terrain feature context should keep land mask bounded");
    }
}

void test_planet_surface_terrain_bands_sum_to_height() {
    const cubey::projects::planet::PlanetConfig config{
        .radius_m = 1200.0F,
        .patches_per_face = 1,
        .patch_resolution = 8,
        .max_lod_level = 0,
        .terrain_enabled = true,
        .terrain_height_scale_m = 80.0F,
        .terrain_noise_scale = 3.0F,
        .terrain_seed = 42U,
        .terrain_mid_detail_strength = 0.72F,
        .terrain_fine_detail_strength = 0.24F,
        .terrain_fine_detail_scale = 11.0F,
    };
    const cubey::math::Vec3 directions[] = {
        glm::normalize(cubey::math::Vec3{1.0F, 0.25F, 0.10F}),
        glm::normalize(cubey::math::Vec3{-0.40F, 0.65F, 0.72F}),
        glm::normalize(cubey::math::Vec3{0.20F, -0.85F, 0.48F}),
        highest_sampled_terrain_direction(config),
    };

    for (const cubey::math::Vec3 direction : directions) {
        const cubey::projects::planet::PlanetSurfaceTerrainBands bands =
            cubey::projects::planet::planet_surface_terrain_bands(config, direction);
        require(std::isfinite(bands.base_shape_m) && std::isfinite(bands.broad_relief_m) &&
                    std::isfinite(bands.mid_detail_m) && std::isfinite(bands.fine_detail_m),
                "planet terrain bands should stay finite");
        const float height =
            cubey::projects::planet::planet_surface_terrain_height_m(config, direction);
        require_close(bands.total_height_m(), height,
                      "planet terrain bands should sum to terrain height");
        require(height >= -config.terrain_height_scale_m &&
                    height <= config.terrain_height_scale_m,
                "planet terrain band total should respect configured height bounds");
    }

    cubey::projects::planet::PlanetConfig disabled = config;
    disabled.terrain_enabled = false;
    const cubey::projects::planet::PlanetSurfaceTerrainBands disabled_bands =
        cubey::projects::planet::planet_surface_terrain_bands(
            disabled, glm::normalize(cubey::math::Vec3{1.0F, 0.25F, 0.10F}));
    require_close(disabled_bands.total_height_m(), 0.0F,
                  "disabled terrain should expose zero terrain bands");
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
    require(sample.land_mask >= 0.0F && sample.land_mask <= 1.0F,
            "planet surface field land mask should stay in range");
    require(sample.moisture >= 0.0F && sample.moisture <= 1.0F,
            "planet surface field moisture should stay in range");
    require(sample.temperature >= 0.0F && sample.temperature <= 1.0F,
            "planet surface field temperature should stay in range");
    require(sample.roughness >= 0.0F && sample.roughness <= 1.0F,
            "planet surface field roughness should stay in range");
    require(std::isfinite(sample.normal.x) && std::isfinite(sample.normal.y) &&
                std::isfinite(sample.normal.z),
            "planet surface field normal should be finite");
    require(glm::length(sample.normal) > 0.99F && glm::length(sample.normal) < 1.01F,
            "planet surface field normal should be normalized");
    require(glm::dot(sample.sphere_normal, sample.normal) > 0.25F,
            "planet surface field normal should remain outward-facing");
    require(sample.material == cubey::projects::planet::planet_surface_material(
                                   sample.height_above_sea_m, sample.water_depth_m,
                                   sample.shoreline_mask, sample.normalized_elevation,
                                   sample.normalized_slope, sample.moisture, sample.temperature),
            "planet surface field sample should carry its classified material");
}

void test_planet_surface_field_classifies_material_bands() {
    require(cubey::projects::planet::planet_surface_material(-2000.0F, 2000.0F, 0.0F, -0.60F, 0.0F,
                                                             0.3F, 0.5F) ==
                cubey::projects::planet::PlanetSurfaceMaterial::DeepWater,
            "planet surface material should classify deep water");
    require(cubey::projects::planet::planet_surface_material(-20.0F, 20.0F, 0.2F, -0.05F, 0.0F,
                                                             0.5F, 0.7F) ==
                cubey::projects::planet::PlanetSurfaceMaterial::ShallowWater,
            "planet surface material should classify shallow water");
    require(cubey::projects::planet::planet_surface_material(20.0F, 0.0F, 0.8F, 0.0F, 0.05F, 0.5F,
                                                             0.8F) ==
                cubey::projects::planet::PlanetSurfaceMaterial::Beach,
            "planet surface material should classify gentle shoreline terrain as beach");
    require(cubey::projects::planet::planet_surface_material(100.0F, 0.0F, 0.0F, 0.0F, 0.05F, 0.6F,
                                                             0.7F) ==
                cubey::projects::planet::PlanetSurfaceMaterial::Lowland,
            "planet surface material should classify low gentle terrain");
    require(cubey::projects::planet::planet_surface_material(100.0F, 0.0F, 0.0F, 0.30F, 0.05F, 0.4F,
                                                             0.6F) ==
                cubey::projects::planet::PlanetSurfaceMaterial::Highland,
            "planet surface material should classify higher terrain");
    require(cubey::projects::planet::planet_surface_material(100.0F, 0.0F, 0.0F, 0.70F, 0.05F, 0.4F,
                                                             0.3F) ==
                cubey::projects::planet::PlanetSurfaceMaterial::Snow,
            "planet surface material should classify high snow terrain");

    const cubey::math::Vec3 water = cubey::projects::planet::planet_surface_material_color(
        cubey::projects::planet::PlanetSurfaceMaterial::DeepWater, -0.2F, 0.0F, 0.0F, 0.5F);
    const cubey::math::Vec3 beach = cubey::projects::planet::planet_surface_material_color(
        cubey::projects::planet::PlanetSurfaceMaterial::Beach, 0.0F, 0.0F, 0.3F, 0.8F);
    const cubey::math::Vec3 highland = cubey::projects::planet::planet_surface_material_color(
        cubey::projects::planet::PlanetSurfaceMaterial::Highland, 0.4F, 0.5F, 0.3F, 0.5F);
    require(water.z > water.x && water.z > water.y,
            "planet water material should be blue-dominant");
    require(beach.x > beach.z && beach.y > beach.z, "planet beach material should be sand-colored");
    require(highland.x > water.x && highland.y > water.y,
            "planet highland material should be brighter than water");
}

void test_planet_surface_field_produces_mixed_landforms() {
    const cubey::projects::planet::PlanetConfig config{};
    float min_height = std::numeric_limits<float>::max();
    float max_height = std::numeric_limits<float>::lowest();
    float max_slope = 0.0F;
    std::uint32_t land_count = 0U;
    std::uint32_t water_count = 0U;
    std::uint32_t material_mask = 0U;
    constexpr std::uint32_t kGrid = 8U;
    for (std::uint32_t face = 0U; face < 6U; ++face) {
        const cubey::projects::planet::PlanetSurfacePatchId id{.face = face};
        for (std::uint32_t y = 0U; y <= kGrid; ++y) {
            const float v = -1.0F + 2.0F * static_cast<float>(y) / static_cast<float>(kGrid);
            for (std::uint32_t x = 0U; x <= kGrid; ++x) {
                const float u = -1.0F + 2.0F * static_cast<float>(x) / static_cast<float>(kGrid);
                const cubey::projects::planet::PlanetSurfaceSample sample =
                    cubey::projects::planet::planet_surface_sample_field(config, id, u, v);
                min_height = std::min(min_height, sample.height_m);
                max_height = std::max(max_height, sample.height_m);
                max_slope = std::max(max_slope, sample.normalized_slope);
                land_count += sample.land_mask > 0.55F ? 1U : 0U;
                water_count += sample.water_depth_m > 0.0F ? 1U : 0U;
                material_mask |= 1U << static_cast<std::uint32_t>(sample.material);
            }
        }
    }

    std::uint32_t material_count = 0U;
    for (std::uint32_t bit = 0U; bit < 6U; ++bit) {
        material_count += (material_mask & (1U << bit)) != 0U ? 1U : 0U;
    }
    require(max_height - min_height > config.terrain_height_scale_m * 0.55F,
            "planet terrain should produce meaningful height relief");
    require(max_slope > 0.025F, "planet terrain should produce visible slope variation");
    require(land_count > 0U && water_count > 0U,
            "planet terrain should produce both land and water samples");
    require(material_count >= 3U, "planet terrain should produce multiple material classes");
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
    require(first.generator_revision == second.generator_revision,
            "planet surface tile payload revision should be deterministic");
    require(first.generator_revision != 0U,
            "planet surface tile payload should expose a nonzero generator revision");
    require(first.summary.sample_count == 16U,
            "planet surface tile payload should sample a square grid including edges");
    require(first.summary.min_height_m <= first.summary.max_height_m,
            "planet surface tile payload should report ordered height bounds");
    require(first.summary.min_height_m >= -config.terrain_height_scale_m - 0.001F,
            "planet surface tile payload should respect minimum terrain height");
    require(first.summary.max_height_m <= config.terrain_height_scale_m + 0.001F,
            "planet surface tile payload should respect maximum terrain height");
    require(first.summary.average_height_m >= first.summary.min_height_m &&
                first.summary.average_height_m <= first.summary.max_height_m,
            "planet surface tile payload should report average height inside range");
    require(first.summary.average_height_above_sea_m >= first.summary.min_height_above_sea_m &&
                first.summary.average_height_above_sea_m <= first.summary.max_height_above_sea_m,
            "planet surface tile payload should report average sea-relative height inside range");
    require(first.summary.max_water_depth_m >= 0.0F,
            "planet surface tile payload should report nonnegative water depth");
    require(first.summary.max_shoreline_mask >= 0.0F && first.summary.max_shoreline_mask <= 1.0F,
            "planet surface tile payload should report bounded shoreline masks");
    require(first.summary.land_coverage >= 0.0F && first.summary.land_coverage <= 1.0F,
            "planet surface tile payload should report bounded land coverage");
    require(first.summary.water_coverage >= 0.0F && first.summary.water_coverage <= 1.0F,
            "planet surface tile payload should report bounded water coverage");
    require(first.summary.shoreline_coverage >= 0.0F && first.summary.shoreline_coverage <= 1.0F,
            "planet surface tile payload should report bounded shoreline coverage");
    require(first.summary.max_normalized_slope >= 0.0F &&
                first.summary.max_normalized_slope <= 1.0F,
            "planet surface tile payload should report bounded slope");
    require(first.summary.min_moisture >= 0.0F && first.summary.min_moisture <= 1.0F &&
                first.summary.max_moisture >= first.summary.min_moisture &&
                first.summary.max_moisture <= 1.0F,
            "planet surface tile payload should report bounded moisture");
    require(first.summary.average_moisture >= first.summary.min_moisture &&
                first.summary.average_moisture <= first.summary.max_moisture,
            "planet surface tile payload should report average moisture inside range");
    require(first.summary.min_temperature >= 0.0F && first.summary.min_temperature <= 1.0F &&
                first.summary.max_temperature >= first.summary.min_temperature &&
                first.summary.max_temperature <= 1.0F,
            "planet surface tile payload should report bounded temperature");
    require(first.summary.average_temperature >= first.summary.min_temperature &&
                first.summary.average_temperature <= first.summary.max_temperature,
            "planet surface tile payload should report average temperature inside range");
    require(first.summary.min_roughness >= 0.0F && first.summary.min_roughness <= 1.0F &&
                first.summary.max_roughness >= first.summary.min_roughness &&
                first.summary.max_roughness <= 1.0F,
            "planet surface tile payload should report bounded roughness");
    require(first.summary.average_roughness >= first.summary.min_roughness &&
                first.summary.average_roughness <= first.summary.max_roughness,
            "planet surface tile payload should report average roughness inside range");
    require(first.summary.average_normalized_slope >= 0.0F &&
                first.summary.average_normalized_slope <= first.summary.max_normalized_slope,
            "planet surface tile payload should report average slope inside range");
    require(first.summary.material_mask != 0U,
            "planet surface tile payload should report at least one material");
    std::uint32_t material_count_total = 0U;
    for (std::uint32_t count : first.summary.material_counts) {
        material_count_total += count;
    }
    std::uint32_t dominant_index = 0U;
    for (std::uint32_t index = 0U; index < first.summary.material_counts.size(); ++index) {
        if (first.summary.material_counts[index] > first.summary.material_counts[dominant_index]) {
            dominant_index = index;
        }
    }
    require(material_count_total == first.summary.sample_count,
            "planet surface tile payload should count every sampled material");
    require(static_cast<std::uint32_t>(first.summary.dominant_material) == dominant_index,
            "planet surface tile payload should report the dominant material");
    require_close(first.summary.min_height_m, second.summary.min_height_m,
                  "planet surface tile payload should be deterministic");
    require_close(first.summary.max_height_m, second.summary.max_height_m,
                  "planet surface tile payload should be deterministic");

    cubey::projects::planet::PlanetConfig changed_config = config;
    changed_config.terrain_fine_detail_strength = 0.41F;
    const cubey::projects::planet::PlanetSurfaceTilePayload changed =
        cubey::projects::planet::make_planet_surface_tile_payload(changed_config, key, 3U);
    require(changed.generator_revision != first.generator_revision,
            "planet surface tile payload revision should change with terrain config");
}

void test_planet_surface_lod_neighbor_diagnostics_detect_mixed_edges() {
    const cubey::projects::planet::PlanetConfig config{
        .radius_m = 1200.0F,
        .patches_per_face = 2,
        .patch_resolution = 4,
        .max_lod_level = 1,
    };
    const std::vector<cubey::projects::planet::PlanetSurfacePatchInstance> patches{
        cubey::projects::planet::PlanetSurfacePatchInstance{
            .id = {.face = 0, .level = 1, .x = 0, .y = 0},
        },
        cubey::projects::planet::PlanetSurfacePatchInstance{
            .id = {.face = 0, .level = 1, .x = 1, .y = 0},
        },
        cubey::projects::planet::PlanetSurfacePatchInstance{
            .id = {.face = 0, .level = 1, .x = 0, .y = 1},
        },
        cubey::projects::planet::PlanetSurfacePatchInstance{
            .id = {.face = 0, .level = 1, .x = 1, .y = 1},
        },
        cubey::projects::planet::PlanetSurfacePatchInstance{
            .id = {.face = 0, .level = 0, .x = 1, .y = 0},
        },
    };

    const cubey::projects::planet::PlanetSurfaceLodNeighborDiagnostics diagnostics =
        cubey::projects::planet::analyze_planet_surface_lod_neighbors(config, patches);

    require(diagnostics.edge_count > 0U,
            "planet LOD neighbor diagnostics should find interior edges");
    require(diagnostics.boundary_edge_count > 0U,
            "planet LOD neighbor diagnostics should count face-boundary edges");
    require(diagnostics.mismatch_edge_count > 0U,
            "planet LOD neighbor diagnostics should find mixed LOD edges");
    require(diagnostics.max_lod_delta == 1U,
            "planet LOD neighbor diagnostics should report the max neighbor LOD delta");
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
        .terrain_enabled = false,
    };
    const cubey::projects::planet::PlanetSurfaceView view{
        .camera_world_position_m = {120.0, 75.0, 1500.0},
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
        .terrain_enabled = false,
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
        .terrain_enabled = false,
    };
    const cubey::projects::planet::PlanetSurfaceView view{
        .camera_world_position_m = {0.0, 0.0, 1500.0},
        .camera_forward_world = {0.0F, 0.0F, -1.0F},
        .culling_enabled = true,
    };
    const cubey::projects::planet::PlanetSurfacePatchPlan plan =
        cubey::projects::planet::plan_planet_surface_patches(config, view);
    const std::vector<cubey::projects::planet::PlanetSurfaceGpuPatchInstance> instances =
        cubey::projects::planet::make_planet_surface_gpu_patch_instances(config, plan);

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

void test_planet_surface_gpu_instances_mark_coarser_neighbor_edges() {
    const cubey::projects::planet::PlanetConfig config{
        .radius_m = 1000.0F,
        .patches_per_face = 2,
        .patch_resolution = 4,
        .max_lod_level = 1,
    };
    cubey::projects::planet::PlanetSurfacePatchPlan plan{};
    plan.selected_patches = {
        cubey::projects::planet::PlanetSurfacePatchInstance{
            .id = {.face = 0, .level = 1, .x = 0, .y = 0},
        },
        cubey::projects::planet::PlanetSurfacePatchInstance{
            .id = {.face = 0, .level = 1, .x = 1, .y = 0},
        },
        cubey::projects::planet::PlanetSurfacePatchInstance{
            .id = {.face = 0, .level = 1, .x = 0, .y = 1},
        },
        cubey::projects::planet::PlanetSurfacePatchInstance{
            .id = {.face = 0, .level = 1, .x = 1, .y = 1},
        },
        cubey::projects::planet::PlanetSurfacePatchInstance{
            .id = {.face = 0, .level = 0, .x = 1, .y = 0},
        },
    };

    const std::vector<cubey::projects::planet::PlanetSurfaceGpuPatchInstance> instances =
        cubey::projects::planet::make_planet_surface_gpu_patch_instances(config, plan);

    bool found_transition_edge = false;
    for (const cubey::projects::planet::PlanetSurfaceGpuPatchInstance& instance : instances) {
        if (instance.level == 1U && instance.edge_transition_mask != 0U) {
            found_transition_edge = true;
        }
        if (instance.level == 0U) {
            require(instance.edge_transition_mask == 0U,
                    "coarser planet GPU instances should not snap toward finer neighbors");
        }
    }
    require(found_transition_edge,
            "finer planet GPU instances should mark edges against coarser neighbors");
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
        .terrain_enabled = false,
    };
    const cubey::projects::planet::PlanetSurfaceView view{
        .camera_world_position_m = {0.0, 0.0, 5000.0},
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

void test_planet_surface_view_lod_target_can_reduce_surface_patch_budget() {
    const cubey::projects::planet::PlanetConfig config{
        .radius_m = 1000.0F,
        .patches_per_face = 1,
        .patch_resolution = 2,
        .max_lod_level = 3,
        .lod_target_edge_px = 0.5F,
        .terrain_enabled = false,
    };
    const cubey::projects::planet::PlanetSurfaceView dense_view{
        .camera_world_position_m = {0.0, 0.0, 1300.0},
        .camera_forward_world = {0.0F, 0.0F, -1.0F},
        .culling_enabled = false,
    };
    cubey::projects::planet::PlanetSurfaceView coarse_view = dense_view;
    coarse_view.lod_target_edge_px = 80.0F;

    const cubey::projects::planet::PlanetSurfacePatchPlan dense_plan =
        cubey::projects::planet::plan_planet_surface_patches(config, dense_view);
    const cubey::projects::planet::PlanetSurfacePatchPlan coarse_plan =
        cubey::projects::planet::plan_planet_surface_patches(config, coarse_view);

    require(coarse_plan.diagnostics.patch_count < dense_plan.diagnostics.patch_count,
            "view-specific LOD target should reduce selected patch count");
    require(coarse_plan.diagnostics.max_lod_level <= dense_plan.diagnostics.max_lod_level,
            "view-specific LOD target should not increase max selected LOD");
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

void test_planet_surface_earthlike_lod_reaches_meter_scale_budget() {
    cubey::projects::planet::PlanetConfig config{};
    config.max_lod_level = cubey::projects::planet::kPlanetMaxLiveLodLevel;
    config.lod_target_edge_px = 1.0F;
    const cubey::projects::planet::PlanetSurfaceView view{
        .camera_world_position_m = {0.0, 0.0, config.radius_m + 50000.0},
        .camera_forward_world = {0.0F, 0.0F, -1.0F},
        .culling_enabled = true,
    };

    const cubey::projects::planet::PlanetSurfacePatchPlan plan =
        cubey::projects::planet::plan_planet_surface_patches(config, view);

    require(plan.diagnostics.patch_count > 0U,
            "earthlike planet LOD should select visible coverage");
    require(plan.diagnostics.patch_count <= cubey::projects::planet::kPlanetMaxLivePatchInstances,
            "earthlike planet LOD should stay inside the live patch budget");
    require(plan.diagnostics.max_lod_level >= 10U,
            "earthlike planet LOD should reach deeper levels near the camera");
    const std::uint32_t selected_max_lod = plan.diagnostics.max_lod_level;
    require(plan.diagnostics.min_cell_edge_m_by_lod[selected_max_lod] > 0.0F &&
                plan.diagnostics.min_cell_edge_m_by_lod[selected_max_lod] < 80.0F,
            "earthlike planet LOD should expose sub-100m selected cells near the camera");
}

void test_planet_local_detail_plan_reports_viewer_centered_clipmap() {
    const cubey::projects::planet::PlanetConfig config{};
    const cubey::projects::planet::PlanetFrame frame = cubey::projects::planet::make_planet_frame(
        config, cubey::math::DVec3{0.0, 0.0, config.radius_m + 10000.0});

    const cubey::projects::planet::PlanetLocalDetailPlan plan =
        cubey::projects::planet::plan_planet_local_detail(config, frame);
    const cubey::projects::planet::PlanetLocalDetailDiagnostics diagnostics =
        cubey::projects::planet::planet_local_detail_diagnostics(config, plan);

    require(diagnostics.enabled, "planet local detail should default to enabled");
    require(diagnostics.active,
            "planet local detail should be active once coarse cells are visible");
    require(diagnostics.lod_levels == config.local_detail_lod_levels,
            "planet local detail should report configured clipmap levels");
    require(diagnostics.patch_count <
                cubey::render::clipmap_grid_2d_patch_count(config.local_detail_lod_levels),
            "planet local detail should skip subpixel fine levels at mid altitude");
    require(diagnostics.active_first_level > 0U,
            "planet local detail should start at a coarser center level at mid altitude");
    require(diagnostics.active_level_count <=
                cubey::projects::planet::kPlanetLocalDetailMaxActiveLevels,
            "planet local detail should cap the resident active level range");
    require(diagnostics.active_last_level ==
                diagnostics.active_first_level + diagnostics.active_level_count - 1U,
            "planet local detail should keep a contiguous active level range");
    require(diagnostics.active_outer_half_extent < diagnostics.outer_half_extent,
            "planet local detail should not allocate the configured full outer extent");
    require(std::abs(diagnostics.near_cell_size - 4.0F) < 0.0001F,
            "planet local detail defaults should expose a four-meter near cell");
    require(diagnostics.triangle_count > 0U,
            "planet local detail should report a positive triangle budget");
    require(std::abs(diagnostics.max_detail_delta_m - config.local_detail_height_strength_m) <
                0.0001F,
            "planet local detail should expose configured detail height");
    require(glm::length(plan.local_frame.world_origin_m - frame.local_frame.world_origin_m) < 0.001,
            "planet local detail should stay anchored to the planet local tangent frame");
}

void test_planet_local_detail_full_range_can_cover_inspection_horizon() {
    const cubey::projects::planet::PlanetConfig config{};
    constexpr float kInspectionOuterHalfExtentM = 131072.0F;
    const cubey::projects::planet::PlanetFrame frame = cubey::projects::planet::make_planet_frame(
        config, cubey::math::DVec3{0.0, 0.0, config.radius_m + 750.0});

    const cubey::projects::planet::PlanetLocalDetailPlan plan =
        cubey::projects::planet::plan_planet_local_detail(
            config, frame,
            cubey::projects::planet::PlanetLocalDetailView{
                .camera_clearance_m = 750.0F,
                .vertical_fov_radians = 1.04719758F,
                .viewport_height_px = 720.0F,
                .full_active_range = true,
                .minimum_lod_levels = cubey::projects::planet::kPlanetMaxLocalDetailLodLevels,
                .minimum_outer_half_extent_m = kInspectionOuterHalfExtentM,
            });
    const cubey::projects::planet::PlanetLocalDetailDiagnostics diagnostics =
        cubey::projects::planet::planet_local_detail_diagnostics(config, plan);

    require(diagnostics.active, "full-range local detail should remain active near the surface");
    require(diagnostics.lod_levels == cubey::projects::planet::kPlanetMaxLocalDetailLodLevels,
            "full-range local detail should use the requested inspection LOD count");
    require(diagnostics.active_last_level == diagnostics.lod_levels - 1U,
            "full-range local detail should keep the coarsest outer ring active");
    require(diagnostics.active_outer_half_extent == kInspectionOuterHalfExtentM,
            "full-range local detail should cover the requested inspection extent");
    require(diagnostics.active_level_count >
                cubey::projects::planet::kPlanetLocalDetailMaxActiveLevels,
            "full-range local detail should bypass the resident three-level cap");
}

void test_planet_local_detail_deactivates_when_subpixel() {
    const cubey::projects::planet::PlanetConfig config{};
    const cubey::projects::planet::PlanetFrame frame = cubey::projects::planet::make_planet_frame(
        config, cubey::math::DVec3{0.0, 0.0, config.radius_m + 2400000.0});
    const cubey::projects::planet::PlanetLocalDetailPlan plan =
        cubey::projects::planet::plan_planet_local_detail(
            config, frame,
            cubey::projects::planet::PlanetLocalDetailView{
                .camera_clearance_m = 2400000.0F,
                .vertical_fov_radians = 1.04719758F,
                .viewport_height_px = 720.0F,
            });
    const cubey::projects::planet::PlanetLocalDetailDiagnostics diagnostics =
        cubey::projects::planet::planet_local_detail_diagnostics(config, plan);

    require(!diagnostics.active,
            "planet local detail should deactivate when even the coarsest level is subpixel");
    require(diagnostics.patch_count == 0U,
            "inactive planet local detail should not allocate diagnostic patches");
    require(diagnostics.triangle_count == 0U,
            "inactive planet local detail should not allocate diagnostic triangles");
}

void test_planet_local_detail_finest_level_is_reachable_at_surface_floor() {
    const cubey::projects::planet::PlanetConfig config{
        .terrain_enabled = false,
    };
    const float min_altitude = cubey::projects::planet::planet_camera_min_altitude_m(config);
    const cubey::projects::planet::PlanetFrame frame = cubey::projects::planet::make_planet_frame(
        config, cubey::math::DVec3{0.0, 0.0, static_cast<double>(config.radius_m + min_altitude)});

    const cubey::projects::planet::PlanetLocalDetailPlan plan =
        cubey::projects::planet::plan_planet_local_detail(
            config, frame,
            cubey::projects::planet::PlanetLocalDetailView{
                .camera_clearance_m = frame.camera_surface_clearance_m,
                .vertical_fov_radians = 1.04719758F,
                .viewport_height_px = 720.0F,
            });
    const cubey::projects::planet::PlanetLocalDetailDiagnostics diagnostics =
        cubey::projects::planet::planet_local_detail_diagnostics(config, plan);

    require(diagnostics.active, "planet local detail should be active at the surface camera floor");
    require(diagnostics.active_first_level == 0U,
            "planet local detail should reach the finest LOD at the surface camera floor");
    require(diagnostics.projected_finest_cell_px >=
                cubey::projects::planet::kPlanetLocalDetailMinProjectedCellPx,
            "planet local detail finest cell should be pixel-visible at the camera floor");
}

void test_planet_local_detail_uses_terrain_relative_surface_floor() {
    const cubey::projects::planet::PlanetConfig config{};
    const float min_altitude = cubey::projects::planet::planet_camera_min_altitude_m(config);
    const cubey::math::Vec3 direction = highest_sampled_terrain_direction(config);
    const float terrain_height =
        cubey::projects::planet::planet_surface_terrain_height_m(config, direction);
    const cubey::projects::planet::PlanetFrame frame = cubey::projects::planet::make_planet_frame(
        config, to_double(direction) *
                    static_cast<double>(config.radius_m + terrain_height + min_altitude));

    const cubey::projects::planet::PlanetLocalDetailPlan plan =
        cubey::projects::planet::plan_planet_local_detail(config, frame);
    const cubey::projects::planet::PlanetLocalDetailDiagnostics diagnostics =
        cubey::projects::planet::planet_local_detail_diagnostics(config, plan);

    require(frame.camera_surface_height_m > 0.0F,
            "terrain-relative local detail test should select elevated terrain");
    require(std::abs(frame.camera_surface_clearance_m - min_altitude) < 2.0F,
            "planet frame should preserve terrain-relative surface clearance");
    require(diagnostics.active,
            "planet local detail should be active at terrain-relative surface clearance");
    require(diagnostics.active_first_level == 0U,
            "terrain-relative local detail should reach finest LOD at the camera floor");
}

void test_planet_local_detail_density_is_independent_of_planet_radius() {
    const cubey::projects::planet::PlanetConfig earth_config{};
    cubey::projects::planet::PlanetConfig mini_config =
        cubey::projects::planet::planet_config_for_scale_preset(
            cubey::projects::planet::PlanetScalePreset::Mini);
    const cubey::projects::planet::PlanetFrame earth_frame =
        cubey::projects::planet::make_planet_frame(
            earth_config, cubey::math::DVec3{0.0, 0.0, earth_config.radius_m + 50000.0});
    const cubey::projects::planet::PlanetFrame mini_frame =
        cubey::projects::planet::make_planet_frame(
            mini_config, cubey::math::DVec3{0.0, 0.0, mini_config.radius_m + 50000.0});

    const cubey::projects::planet::PlanetLocalDetailPlan earth_plan =
        cubey::projects::planet::plan_planet_local_detail(earth_config, earth_frame);
    const cubey::projects::planet::PlanetLocalDetailPlan mini_plan =
        cubey::projects::planet::plan_planet_local_detail(mini_config, mini_frame);
    const cubey::projects::planet::PlanetLocalDetailDiagnostics earth_diagnostics =
        cubey::projects::planet::planet_local_detail_diagnostics(earth_config, earth_plan);
    const cubey::projects::planet::PlanetLocalDetailDiagnostics mini_diagnostics =
        cubey::projects::planet::planet_local_detail_diagnostics(mini_config, mini_plan);

    require(std::abs(earth_diagnostics.near_cell_size - mini_diagnostics.near_cell_size) < 0.0001F,
            "planet local detail near cell should not change with planet radius");
    require(std::abs(earth_diagnostics.outer_half_extent - mini_diagnostics.outer_half_extent) <
                0.0001F,
            "planet local detail extent should not change with planet radius");
}

void test_planet_local_detail_mesh_matches_clipmap_budget() {
    const cubey::projects::planet::PlanetConfig config{};
    const cubey::projects::planet::PlanetFrame frame = cubey::projects::planet::make_planet_frame(
        config, cubey::math::DVec3{0.0, 0.0, config.radius_m + 250.0});

    const cubey::projects::planet::PlanetLocalDetailBuildResult build =
        cubey::projects::planet::make_planet_local_detail_mesh(config, frame);

    require(build.mesh.vertices.size() == build.mesh.indices.size(),
            "planet local detail mesh should use one index per generated vertex");
    require(build.mesh.vertices.size() == build.diagnostics.vertex_count,
            "planet local detail diagnostics should match generated vertex count");
    require(build.mesh.indices.size() == build.diagnostics.triangle_count * 3ULL,
            "planet local detail diagnostics should match generated triangle count");
    bool has_owned_vertex = false;
    for (const cubey::projects::planet::PlanetLocalDetailVertex& vertex : build.mesh.vertices) {
        has_owned_vertex = has_owned_vertex || vertex.blend > 0.99F;
    }
    require(has_owned_vertex,
            "planet local detail center vertices should be owned by the local layer");
}

void test_planet_local_detail_runtime_tracks_topology() {
    cubey::projects::planet::PlanetConfig config{};
    const cubey::projects::planet::PlanetFrame frame = cubey::projects::planet::make_planet_frame(
        config, cubey::math::DVec3{0.0, 0.0, config.radius_m + 250.0});
    cubey::projects::planet::PlanetLocalDetailRuntime runtime{};

    require(runtime.topology_changed(config),
            "planet local detail runtime should require an initial build");
    runtime.rebuild(config, frame);
    require(!runtime.topology_changed(config),
            "planet local detail runtime should accept matching topology");
    require(runtime.topology_changed(config,
                                     cubey::projects::planet::PlanetLocalDetailView{
                                         .camera_clearance_m = 2400000.0F,
                                         .vertical_fov_radians = 1.04719758F,
                                         .viewport_height_px = 720.0F,
                                     }),
            "planet local detail runtime should detect active level range changes");

    config.local_detail_cells_per_axis *= 2U;
    require(runtime.topology_changed(config),
            "planet local detail runtime should detect local-detail topology edits");
}

void test_planet_local_detail_runtime_exposes_inactive_diagnostics_after_clear() {
    const cubey::projects::planet::PlanetConfig config{};
    const cubey::projects::planet::PlanetFrame frame = cubey::projects::planet::make_planet_frame(
        config, cubey::math::DVec3{0.0, 0.0, config.radius_m + 250.0});
    cubey::projects::planet::PlanetLocalDetailRuntime runtime{};
    runtime.rebuild(config, frame);
    runtime.clear();

    const cubey::projects::planet::PlanetLocalDetailDiagnostics& diagnostics =
        runtime.diagnostics();
    require(!diagnostics.enabled,
            "cleared local detail runtime should expose inactive diagnostics");
    require(diagnostics.triangle_count == 0U,
            "cleared local detail runtime should not report drawable triangles");
    require(runtime.topology_changed(config),
            "cleared local detail runtime should require a rebuild before drawing");
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

void test_planet_surface_planner_repairs_lod_neighbor_deltas() {
    const cubey::projects::planet::PlanetConfig config{
        .radius_m = 1000.0F,
        .patches_per_face = 2,
        .patch_resolution = 2,
        .max_lod_level = 4,
        .lod_target_edge_px = 0.9F,
        .skirts_enabled = false,
        .terrain_enabled = false,
    };
    const cubey::projects::planet::PlanetSurfaceView view{
        .camera_world_position_m = {0.0, 0.0, 1150.0},
        .camera_forward_world = {0.0F, 0.0F, -1.0F},
        .viewport_height_px = 720.0F,
        .culling_enabled = true,
    };

    const cubey::projects::planet::PlanetSurfacePatchPlan plan =
        cubey::projects::planet::plan_planet_surface_patches(config, view);

    require(plan.diagnostics.max_lod_neighbor_delta <= 1U,
            "planet planner should repair selected neighbor LOD deltas to one step");
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
    require(cubey::projects::planet::planet_debug_view_from_string("terrain-band-base") ==
                cubey::projects::planet::PlanetDebugView::TerrainBandBase,
            "planet debug view should parse terrain-band-base");
    require(cubey::projects::planet::planet_debug_view_from_string("terrain-band-relief") ==
                cubey::projects::planet::PlanetDebugView::TerrainBandRelief,
            "planet debug view should parse terrain-band-relief");
    require(cubey::projects::planet::planet_debug_view_from_string("terrain-band-detail") ==
                cubey::projects::planet::PlanetDebugView::TerrainBandDetail,
            "planet debug view should parse terrain-band-detail");
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
    require(cubey::projects::planet::planet_debug_view_from_string("land-mask") ==
                cubey::projects::planet::PlanetDebugView::LandMask,
            "planet debug view should parse land-mask");
    require(cubey::projects::planet::planet_debug_view_from_string("moisture") ==
                cubey::projects::planet::PlanetDebugView::Moisture,
            "planet debug view should parse moisture");
    require(cubey::projects::planet::planet_debug_view_from_string("temperature") ==
                cubey::projects::planet::PlanetDebugView::Temperature,
            "planet debug view should parse temperature");
    require(cubey::projects::planet::planet_debug_view_from_string("roughness") ==
                cubey::projects::planet::PlanetDebugView::Roughness,
            "planet debug view should parse roughness");
    require(cubey::projects::planet::planet_debug_view_from_string("wireframe") ==
                cubey::projects::planet::PlanetDebugView::Wireframe,
            "planet debug view should parse wireframe");
    require(cubey::projects::planet::planet_debug_view_from_string("celestial-planes") ==
                cubey::projects::planet::PlanetDebugView::CelestialPlanes,
            "planet debug view should parse celestial-planes");
    require(cubey::projects::planet::planet_debug_view_from_string("local-detail-wireframe") ==
                cubey::projects::planet::PlanetDebugView::LocalDetailWireframe,
            "planet debug view should parse local-detail-wireframe");
    require(cubey::projects::planet::planet_debug_view_from_string("local-detail-blend") ==
                cubey::projects::planet::PlanetDebugView::LocalDetailBlend,
            "planet debug view should parse local-detail-blend");
    require(cubey::projects::planet::planet_debug_view_from_string("local-detail-lod") ==
                cubey::projects::planet::PlanetDebugView::LocalDetailLod,
            "planet debug view should parse local-detail-lod");
    require(cubey::projects::planet::planet_debug_view_from_string("local-detail-height") ==
                cubey::projects::planet::PlanetDebugView::LocalDetailHeight,
            "planet debug view should parse local-detail-height");
    require(cubey::projects::planet::planet_debug_view_from_string("local-detail-features") ==
                cubey::projects::planet::PlanetDebugView::LocalDetailFeatures,
            "planet debug view should parse local-detail-features");
    require(cubey::projects::planet::planet_debug_view_from_string("local-detail-final") ==
                cubey::projects::planet::PlanetDebugView::LocalDetailFinal,
            "planet debug view should parse local-detail-final");
    require(std::string_view{cubey::projects::planet::planet_debug_view_name(
                cubey::projects::planet::PlanetDebugView::CellEdge)} == "cell-edge",
            "planet debug view should name cell-edge");
    require(std::string_view{cubey::projects::planet::planet_debug_view_name(
                cubey::projects::planet::PlanetDebugView::TerrainHeight)} == "terrain-height",
            "planet debug view should name terrain-height");
    require(std::string_view{cubey::projects::planet::planet_debug_view_name(
                cubey::projects::planet::PlanetDebugView::TerrainBandBase)} ==
                "terrain-band-base",
            "planet debug view should name terrain-band-base");
    require(std::string_view{cubey::projects::planet::planet_debug_view_name(
                cubey::projects::planet::PlanetDebugView::TerrainBandRelief)} ==
                "terrain-band-relief",
            "planet debug view should name terrain-band-relief");
    require(std::string_view{cubey::projects::planet::planet_debug_view_name(
                cubey::projects::planet::PlanetDebugView::TerrainBandDetail)} ==
                "terrain-band-detail",
            "planet debug view should name terrain-band-detail");
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
                cubey::projects::planet::PlanetDebugView::LandMask)} == "land-mask",
            "planet debug view should name land-mask");
    require(std::string_view{cubey::projects::planet::planet_debug_view_name(
                cubey::projects::planet::PlanetDebugView::Moisture)} == "moisture",
            "planet debug view should name moisture");
    require(std::string_view{cubey::projects::planet::planet_debug_view_name(
                cubey::projects::planet::PlanetDebugView::Temperature)} == "temperature",
            "planet debug view should name temperature");
    require(std::string_view{cubey::projects::planet::planet_debug_view_name(
                cubey::projects::planet::PlanetDebugView::Roughness)} == "roughness",
            "planet debug view should name roughness");
    require(std::string_view{cubey::projects::planet::planet_debug_view_name(
                cubey::projects::planet::PlanetDebugView::Wireframe)} == "wireframe",
            "planet debug view should name wireframe");
    require(std::string_view{cubey::projects::planet::planet_debug_view_name(
                cubey::projects::planet::PlanetDebugView::CelestialPlanes)} == "celestial-planes",
            "planet debug view should name celestial-planes");
    require(std::string_view{cubey::projects::planet::planet_debug_view_name(
                cubey::projects::planet::PlanetDebugView::LocalDetailWireframe)} ==
                "local-detail-wireframe",
            "planet debug view should name local-detail-wireframe");
    require(std::string_view{cubey::projects::planet::planet_debug_view_name(
                cubey::projects::planet::PlanetDebugView::LocalDetailBlend)} ==
                "local-detail-blend",
            "planet debug view should name local-detail-blend");
    require(std::string_view{cubey::projects::planet::planet_debug_view_name(
                cubey::projects::planet::PlanetDebugView::LocalDetailLod)} == "local-detail-lod",
            "planet debug view should name local-detail-lod");
    require(std::string_view{cubey::projects::planet::planet_debug_view_name(
                cubey::projects::planet::PlanetDebugView::LocalDetailHeight)} ==
                "local-detail-height",
            "planet debug view should name local-detail-height");
    require(std::string_view{cubey::projects::planet::planet_debug_view_name(
                cubey::projects::planet::PlanetDebugView::LocalDetailFeatures)} ==
                "local-detail-features",
            "planet debug view should name local-detail-features");
    require(std::string_view{cubey::projects::planet::planet_debug_view_name(
                cubey::projects::planet::PlanetDebugView::LocalDetailFinal)} ==
                "local-detail-final",
            "planet debug view should name local-detail-final");
    require(static_cast<std::uint8_t>(
                cubey::projects::planet::PlanetDebugView::LocalDetailWireframe) == 19U,
            "local-detail shader debug range should start at wireframe");
    require(static_cast<std::uint8_t>(
                cubey::projects::planet::PlanetDebugView::LocalDetailLod) == 21U,
            "local-detail LOD shader debug value should stay synchronized");
    require(static_cast<std::uint8_t>(
                cubey::projects::planet::PlanetDebugView::LocalDetailFinal) == 24U,
            "local-detail shader final value should stay synchronized");
    require(static_cast<std::uint8_t>(
                cubey::projects::planet::PlanetDebugView::TerrainBandBase) == 25U,
            "terrain-band-base shader debug value should stay synchronized");
    require(static_cast<std::uint8_t>(
                cubey::projects::planet::PlanetDebugView::TerrainBandRelief) == 26U,
            "terrain-band-relief shader debug value should stay synchronized");
    require(static_cast<std::uint8_t>(
                cubey::projects::planet::PlanetDebugView::TerrainBandDetail) == 27U,
            "terrain-band-detail shader debug value should stay synchronized");
    require(!cubey::projects::planet::planet_debug_view_is_local_detail(
                cubey::projects::planet::PlanetDebugView::Final),
            "final planet view should not enable local-detail diagnostic rendering");
    require(cubey::projects::planet::planet_debug_view_is_local_detail(
                cubey::projects::planet::PlanetDebugView::LocalDetailWireframe),
            "local-detail-wireframe should enable local-detail diagnostic rendering");
    require(cubey::projects::planet::planet_debug_view_is_local_detail(
                cubey::projects::planet::PlanetDebugView::LocalDetailBlend),
            "local-detail-blend should enable local-detail diagnostic rendering");
    require(cubey::projects::planet::planet_debug_view_is_local_detail(
                cubey::projects::planet::PlanetDebugView::LocalDetailLod),
            "local-detail-lod should enable local-detail diagnostic rendering");
    require(cubey::projects::planet::planet_debug_view_is_local_detail(
                cubey::projects::planet::PlanetDebugView::LocalDetailHeight),
            "local-detail-height should enable local-detail diagnostic rendering");
    require(cubey::projects::planet::planet_debug_view_is_local_detail(
                cubey::projects::planet::PlanetDebugView::LocalDetailFeatures),
            "local-detail-features should enable local-detail diagnostic rendering");
    require(cubey::projects::planet::planet_debug_view_is_local_detail(
                cubey::projects::planet::PlanetDebugView::LocalDetailFinal),
            "local-detail-final should enable local-detail inspection rendering");
    require(!cubey::projects::planet::planet_debug_view_uses_local_detail_surface(
                cubey::projects::planet::PlanetDebugView::Final),
            "final planet view should keep continuous global terrain until local-detail handoff is solved");
    require(!cubey::projects::planet::planet_debug_view_uses_local_detail_surface(
                cubey::projects::planet::PlanetDebugView::TerrainBandBase),
            "terrain band debug views should inspect the global surface without local-detail overlay");
    require(cubey::projects::planet::planet_debug_view_uses_local_detail_surface(
                cubey::projects::planet::PlanetDebugView::TerrainHeight),
            "terrain field debug views should allow local-detail surface rendering");
    require(cubey::projects::planet::planet_debug_view_uses_local_detail_surface(
                cubey::projects::planet::PlanetDebugView::LocalDetailWireframe),
            "local-detail debug views should allow local-detail surface rendering");
    require(cubey::projects::planet::planet_debug_view_uses_local_detail_surface(
                cubey::projects::planet::PlanetDebugView::LocalDetailLod),
            "local-detail LOD debug view should allow local-detail surface rendering");
    require(cubey::projects::planet::planet_debug_view_uses_local_detail_surface(
                cubey::projects::planet::PlanetDebugView::LocalDetailFeatures),
            "local-detail feature debug view should allow local-detail surface rendering");
    require(cubey::projects::planet::planet_debug_view_uses_local_detail_surface(
                cubey::projects::planet::PlanetDebugView::LocalDetailFinal),
            "local-detail final view should allow local-detail surface rendering");
    require(!cubey::projects::planet::planet_debug_view_uses_local_detail_surface(
                cubey::projects::planet::PlanetDebugView::Wireframe),
            "global mesh wireframe should not draw local-detail surface overlays");
}

void test_planet_surface_planner_records_lod_transition_pressure() {
    const cubey::projects::planet::PlanetConfig config{
        .radius_m = 1000.0F,
        .patches_per_face = 1,
        .patch_resolution = 2,
        .max_lod_level = 0,
        .lod_target_edge_px = 12.0F,
        .terrain_enabled = false,
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

void test_planet_surface_screen_error_accounts_for_terrain_displacement() {
    cubey::projects::planet::PlanetConfig flat_config{
        .radius_m = 1000.0F,
        .patches_per_face = 1,
        .patch_resolution = 4,
        .max_lod_level = 0,
        .terrain_enabled = false,
    };
    cubey::projects::planet::PlanetConfig terrain_config = flat_config;
    terrain_config.terrain_enabled = true;
    terrain_config.terrain_height_scale_m = 400.0F;
    const cubey::projects::planet::PlanetSurfaceView view{
        .camera_world_position_m = {0.0, 0.0, 1500.0},
        .camera_forward_world = {0.0F, 0.0F, -1.0F},
        .viewport_height_px = 720.0F,
        .culling_enabled = false,
    };

    const cubey::projects::planet::PlanetSurfacePatchPlan flat_plan =
        cubey::projects::planet::plan_planet_surface_patches(flat_config, view);
    const cubey::projects::planet::PlanetSurfacePatchPlan terrain_plan =
        cubey::projects::planet::plan_planet_surface_patches(terrain_config, view);

    require(terrain_plan.diagnostics.max_screen_error_px >
                flat_plan.diagnostics.max_screen_error_px,
            "planet screen error should use the nearest possible displaced terrain bound");
}

void test_planet_surface_runtime_rebuilds_render_plan() {
    const cubey::projects::planet::PlanetConfig config{
        .radius_m = 600000.0F,
        .patches_per_face = 1,
        .patch_resolution = 4,
        .max_lod_level = 1,
        .skirts_enabled = true,
        .terrain_enabled = false,
    };
    const cubey::projects::planet::PlanetFrame frame = cubey::projects::planet::make_planet_frame(
        config, cubey::math::DVec3{0.0, 0.0, config.radius_m + 50000.0F});
    const cubey::projects::planet::PlanetSurfaceView view{
        .camera_world_position_m = frame.camera_world_position_m,
        .camera_forward_world = {0.0F, 0.0F, -1.0F},
        .viewport_height_px = 720.0F,
        .culling_enabled = false,
    };

    cubey::projects::planet::PlanetSurfaceRuntime runtime;
    runtime.rebuild(config, frame, view);

    const cubey::projects::planet::PlanetSurfaceDiagnostics& diagnostics = runtime.diagnostics();
    require(runtime.instance_count() == diagnostics.patch_count,
            "planet surface runtime should upload one instance per planned patch");
    require(diagnostics.patch_count > 0U, "planet surface runtime should keep planned patches");
    require(diagnostics.vertex_count ==
                runtime.patch_grid().vertices.size() * runtime.instance_count(),
            "planet surface runtime diagnostics should include instanced grid vertices");
    require(diagnostics.triangle_count ==
                (runtime.patch_grid().indices.size() / 3U) * runtime.instance_count(),
            "planet surface runtime diagnostics should include instanced grid triangles");
    require(diagnostics.skirt_triangle_count > 0U,
            "planet surface runtime diagnostics should preserve skirt totals");
    require(glm::length(runtime.render_origin_world_m() - frame.render_origin_world_m) < 0.001,
            "planet surface runtime should remember the build render origin");
}

void test_planet_surface_runtime_detects_plan_changes() {
    const cubey::projects::planet::PlanetConfig config{
        .radius_m = 600000.0F,
        .patches_per_face = 1,
        .patch_resolution = 4,
        .max_lod_level = 1,
        .terrain_enabled = false,
    };
    const cubey::projects::planet::PlanetFrame frame = cubey::projects::planet::make_planet_frame(
        config, cubey::math::DVec3{0.0, 0.0, config.radius_m + 50000.0F});
    const cubey::projects::planet::PlanetSurfaceView view{
        .camera_world_position_m = frame.camera_world_position_m,
        .camera_forward_world = {0.0F, 0.0F, -1.0F},
        .aspect_ratio = 16.0F / 9.0F,
        .viewport_height_px = 720.0F,
        .culling_enabled = true,
    };

    cubey::projects::planet::PlanetSurfaceRuntime runtime;
    require(runtime.plan_changed(config, frame, view),
            "planet surface runtime should request an initial plan");
    runtime.rebuild(config, frame, view);
    require(!runtime.plan_changed(config, frame, view),
            "planet surface runtime should keep the current plan for the same view");

    cubey::projects::planet::PlanetSurfaceView resized_view = view;
    resized_view.aspect_ratio = 4.0F / 3.0F;
    require(runtime.plan_changed(config, frame, resized_view),
            "planet surface runtime should detect viewport aspect changes");

    cubey::projects::planet::PlanetSurfaceView rotated_view = view;
    rotated_view.camera_forward_world = {0.2F, 0.0F, -0.98F};
    require(runtime.plan_changed(config, frame, rotated_view),
            "planet surface runtime should detect large camera rotation changes");

    cubey::projects::planet::PlanetFrame shifted_frame = frame;
    shifted_frame.render_origin_world_m.x += static_cast<double>(config.radius_m) * 0.03;
    require(runtime.plan_changed(config, shifted_frame, view),
            "planet surface runtime should detect render origin movement");
}

void test_planet_surface_runtime_detects_surface_scale_changes() {
    const cubey::projects::planet::PlanetConfig config{
        .radius_m = 6000000.0F,
        .patches_per_face = 1,
        .patch_resolution = 4,
        .max_lod_level = 2,
        .terrain_enabled = false,
    };
    const cubey::projects::planet::PlanetFrame frame = cubey::projects::planet::make_planet_frame(
        config, cubey::math::DVec3{0.0, 0.0, config.radius_m + 10000.0F});
    const cubey::projects::planet::PlanetSurfaceView view{
        .camera_world_position_m = frame.camera_world_position_m,
        .camera_forward_world = {0.0F, 0.0F, -1.0F},
        .aspect_ratio = 16.0F / 9.0F,
        .viewport_height_px = 720.0F,
        .culling_enabled = true,
    };

    cubey::projects::planet::PlanetSurfaceRuntime runtime;
    runtime.rebuild(config, frame, view);

    const cubey::projects::planet::PlanetFrame zoomed_frame =
        cubey::projects::planet::make_planet_frame(
            config, cubey::math::DVec3{0.0, 0.0, config.radius_m + 3000.0F});
    cubey::projects::planet::PlanetSurfaceView zoomed_view = view;
    zoomed_view.camera_world_position_m = zoomed_frame.camera_world_position_m;
    require(runtime.plan_changed(config, zoomed_frame, zoomed_view),
            "planet surface runtime should replan after near-surface clearance changes");

    const cubey::projects::planet::PlanetFrame surface_frame =
        cubey::projects::planet::make_planet_frame(
            config, cubey::math::DVec3{0.0, 0.0, config.radius_m + 1000.0F});
    cubey::projects::planet::PlanetSurfaceView surface_view = view;
    surface_view.camera_world_position_m = surface_frame.camera_world_position_m;
    runtime.rebuild(config, surface_frame, surface_view);

    const cubey::projects::planet::PlanetFrame walked_frame =
        cubey::projects::planet::make_planet_frame(
            config, cubey::math::DVec3{700.0, 0.0, config.radius_m + 1000.0F});
    cubey::projects::planet::PlanetSurfaceView walked_view = surface_view;
    walked_view.camera_world_position_m = walked_frame.camera_world_position_m;
    require(runtime.plan_changed(config, walked_frame, walked_view),
            "planet surface runtime should replan after small near-surface tangent travel");
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
        test_planet_surface_terrain_feature_context_is_bounded();
        test_planet_surface_terrain_bands_sum_to_height();
        test_planet_surface_field_disabled_terrain_returns_sphere_sample();
        test_planet_surface_field_is_deterministic_for_seed();
        test_planet_surface_field_reports_bounded_height_normal_and_slope();
        test_planet_surface_field_classifies_material_bands();
        test_planet_surface_field_produces_mixed_landforms();
        test_planet_surface_field_reports_sea_level_and_bathymetry();
        test_planet_surface_tile_key_round_trips_patch_identity();
        test_planet_surface_tile_payload_is_deterministic_and_bounded();
        test_planet_surface_lod_neighbor_diagnostics_detect_mixed_edges();
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
        test_planet_surface_gpu_instances_mark_coarser_neighbor_edges();
        test_planet_surface_cpu_mesh_rejects_too_dense_live_lod();
        test_planet_surface_planner_keeps_fallback_when_camera_looks_away();
        test_planet_surface_planner_selects_near_lod();
        test_planet_surface_view_lod_target_can_reduce_surface_patch_budget();
        test_planet_surface_planner_records_high_lod_diagnostics();
        test_planet_surface_default_lod_reaches_near_camera_detail();
        test_planet_surface_earthlike_lod_reaches_meter_scale_budget();
        test_planet_local_detail_plan_reports_viewer_centered_clipmap();
        test_planet_local_detail_full_range_can_cover_inspection_horizon();
        test_planet_local_detail_deactivates_when_subpixel();
        test_planet_local_detail_finest_level_is_reachable_at_surface_floor();
        test_planet_local_detail_uses_terrain_relative_surface_floor();
        test_planet_local_detail_density_is_independent_of_planet_radius();
        test_planet_local_detail_mesh_matches_clipmap_budget();
        test_planet_local_detail_runtime_tracks_topology();
        test_planet_local_detail_runtime_exposes_inactive_diagnostics_after_clear();
        test_planet_surface_hysteresis_delays_split();
        test_planet_surface_hysteresis_delays_merge();
        test_planet_surface_planner_falls_back_at_live_patch_budget();
        test_planet_surface_planner_repairs_lod_neighbor_deltas();
        test_planet_surface_skirts_add_seam_geometry();
        test_planet_surface_skirt_vertices_drop_below_radius();
        test_planet_surface_seams_debug_view_parses();
        test_planet_surface_metric_debug_views_parse();
        test_planet_surface_planner_records_lod_transition_pressure();
        test_planet_surface_screen_error_accounts_for_terrain_displacement();
        test_planet_surface_runtime_rebuilds_render_plan();
        test_planet_surface_runtime_detects_plan_changes();
        test_planet_surface_runtime_detects_surface_scale_changes();
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "planet_surface_tests: %s\n", error.what());
        return 1;
    }
}
