#include "terrain_backdrop_camera.h"

#include <nlohmann/json.hpp>

#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace {

using cubey::projects::terrain::TerrainPreset;

constexpr std::array<TerrainPreset, 3> kPresets{
    TerrainPreset::Mountain,
    TerrainPreset::Upland,
    TerrainPreset::Plains,
};
constexpr std::array<std::uint64_t, 3> kSeeds{0U, 9012U, 12345U};
constexpr std::array<cubey::projects::terrain::TerrainBackdropCameraProfile, 2> kProfiles{
    cubey::projects::terrain::TerrainBackdropCameraProfile::Backdrop,
    cubey::projects::terrain::TerrainBackdropCameraProfile::Midground,
};

[[nodiscard]] nlohmann::json vec2_json(cubey::math::Vec2 value) {
    return nlohmann::json::array({value.x, value.y});
}

[[nodiscard]] nlohmann::json vec3_json(cubey::math::Vec3 value) {
    return nlohmann::json::array({value.x, value.y, value.z});
}

} // namespace

int main(int argc, char** argv) {
    cubey::projects::terrain::TerrainSourceVersion version =
        cubey::projects::terrain::TerrainSourceVersion::V1;
    if (argc == 3 && std::string_view(argv[1]) == "--source-version") {
        version = cubey::projects::terrain::terrain_source_version_from_name(argv[2]);
    } else if (argc != 1) {
        throw std::runtime_error("usage: terrain_backdrop_report [--source-version v1|v2|v2.1|v3]");
    }

    nlohmann::json plans = nlohmann::json::array();
    for (const auto profile : kProfiles) {
        for (const TerrainPreset preset : kPresets) {
            if (version != cubey::projects::terrain::TerrainSourceVersion::V1 &&
                preset != TerrainPreset::Mountain) {
                continue;
            }
            for (const std::uint64_t seed : kSeeds) {
                const auto source = cubey::projects::terrain::resolve_terrain_source_parameters({
                    .seed = seed,
                    .preset = preset,
                    .version = version,
                    .weathering = cubey::projects::terrain::TerrainWeatheringMode::Local,
                });
                const auto plan = cubey::projects::terrain::plan_terrain_backdrop_camera(
                    source, 1.0F, 16.0F / 9.0F, profile);
                const float target_render_footprint_m =
                    profile == cubey::projects::terrain::TerrainBackdropCameraProfile::Backdrop
                        ? 64.0F
                        : 32.0F;
                const float target_render_height_m =
                    cubey::projects::terrain::sample_terrain(
                        source, {.world_xz = {plan.target_position.x, plan.target_position.z},
                                 .footprint_m = target_render_footprint_m})
                        .height_m;
                plans.push_back({
                    {"profile",
                     cubey::projects::terrain::terrain_backdrop_camera_profile_name(profile)},
                    {"preset", cubey::projects::terrain::terrain_preset_name(preset)},
                    {"seed", seed},
                    {"anchor_xz_m", vec2_json(plan.anchor_xz)},
                    {"camera_position_m", vec3_json(plan.transform.translation)},
                    {"target_position_m", vec3_json(plan.target_position)},
                    {"target_render_footprint_m", target_render_footprint_m},
                    {"target_render_height_m", target_render_height_m},
                    {"yaw_radians", plan.yaw_radians},
                    {"pitch_radians", plan.pitch_radians},
                    {"target_distance_m", plan.target_distance_m},
                    {"target_elevation_radians", plan.target_elevation_radians},
                    {"camera_clearance_m", plan.camera_clearance_m},
                    {"clearance_raise_m", plan.clearance_raise_m},
                    {"foreground_clear_distance_m", plan.foreground_clear_distance_m},
                    {"foreground_min_margin_m", plan.foreground_min_margin_m},
                    {"near_frame_test_distance_m", plan.near_frame_test_distance_m},
                    {"near_frame_occluded_ray_count", plan.near_frame_occluded_ray_count},
                    {"near_frame_occupancy_ratio", plan.near_frame_occupancy_ratio},
                    {"near_frame_nearest_hit_distance_m", plan.near_frame_nearest_hit_distance_m},
                    {"aspect_ratio", plan.aspect_ratio},
                    {"score", plan.score},
                });
            }
        }
    }

    const nlohmann::json report{
        {"schema", "cubey.terrain.backdrop-camera.v4"},
        {"source_version", cubey::projects::terrain::terrain_source_version_name(version)},
        {"anchor_grid_m", {-4096, -2048, 0, 2048, 4096}},
        {"heading_count", 24},
        {"backdrop_sample_distances_m", {3200, 6400}},
        {"midground_sample_distances_m", {1600}},
        {"minimum_clearance_m", 150},
        {"foreground_clear_distance_m", 300},
        {"foreground_safety_margin_m", 10},
        {"near_frame_ndc_x", {-1.0, -0.5, 0.0, 0.5, 1.0}},
        {"near_frame_ndc_y", {0.0, 0.35, 0.70}},
        {"near_frame_start_distance_m", 100},
        {"near_frame_sample_step_m", 50},
        {"near_frame_target_distance_fraction", 0.75},
        {"near_frame_maximum_occluded_rays", 2},
        {"vertical_fov_degrees", 40},
        {"plans", std::move(plans)},
    };
    std::cout << report.dump(2) << '\n';
    return 0;
}
