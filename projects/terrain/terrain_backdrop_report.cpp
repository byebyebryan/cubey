#include "terrain_backdrop_camera.h"

#include <nlohmann/json.hpp>

#include <array>
#include <cstdint>
#include <iostream>

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

int main() {
    nlohmann::json plans = nlohmann::json::array();
    for (const auto profile : kProfiles) {
        for (const TerrainPreset preset : kPresets) {
            for (const std::uint64_t seed : kSeeds) {
                const auto source = cubey::projects::terrain::resolve_terrain_source_parameters({
                    .seed = seed,
                    .preset = preset,
                    .weathering = cubey::projects::terrain::TerrainWeatheringMode::Local,
                });
                const auto plan = cubey::projects::terrain::plan_terrain_backdrop_camera(
                    source, 1.0F, 16.0F / 9.0F, profile);
                plans.push_back({
                    {"profile",
                     cubey::projects::terrain::terrain_backdrop_camera_profile_name(profile)},
                    {"preset", cubey::projects::terrain::terrain_preset_name(preset)},
                    {"seed", seed},
                    {"anchor_xz_m", vec2_json(plan.anchor_xz)},
                    {"camera_position_m", vec3_json(plan.transform.translation)},
                    {"target_position_m", vec3_json(plan.target_position)},
                    {"yaw_radians", plan.yaw_radians},
                    {"pitch_radians", plan.pitch_radians},
                    {"target_distance_m", plan.target_distance_m},
                    {"target_elevation_radians", plan.target_elevation_radians},
                    {"camera_clearance_m", plan.camera_clearance_m},
                    {"clearance_raise_m", plan.clearance_raise_m},
                    {"foreground_clear_distance_m", plan.foreground_clear_distance_m},
                    {"foreground_min_margin_m", plan.foreground_min_margin_m},
                    {"aspect_ratio", plan.aspect_ratio},
                    {"score", plan.score},
                });
            }
        }
    }

    const nlohmann::json report{
        {"schema", "cubey.terrain.backdrop-camera.v3"},
        {"anchor_grid_m", {-4096, -2048, 0, 2048, 4096}},
        {"heading_count", 24},
        {"backdrop_sample_distances_m", {3200, 6400}},
        {"midground_sample_distances_m", {1600}},
        {"minimum_clearance_m", 150},
        {"foreground_clear_distance_m", 300},
        {"foreground_safety_margin_m", 10},
        {"vertical_fov_degrees", 40},
        {"plans", std::move(plans)},
    };
    std::cout << report.dump(2) << '\n';
    return 0;
}
