#include "terrain_backdrop_stage.h"

#include <nlohmann/json.hpp>

#include <array>
#include <cstdint>
#include <iostream>

namespace {

constexpr std::array<std::uint64_t, 3> kSeeds{0U, 9012U, 12345U};
constexpr std::array<cubey::projects::terrain::TerrainBackdropStageMode, 2> kModes{
    cubey::projects::terrain::TerrainBackdropStageMode::Detached,
    cubey::projects::terrain::TerrainBackdropStageMode::Grounded,
};

[[nodiscard]] nlohmann::json vec2_json(cubey::math::Vec2 value) {
    return nlohmann::json::array({value.x, value.y});
}

} // namespace

int main() {
    using namespace cubey::projects::terrain;

    nlohmann::json plans = nlohmann::json::array();
    for (const TerrainBackdropStageMode mode : kModes) {
        for (const std::uint64_t seed : kSeeds) {
            const TerrainSourceParameters source = resolve_terrain_source_parameters({
                .seed = seed,
                .preset = TerrainPreset::Mountain,
                .version = TerrainSourceVersion::V2_1,
                .weathering = TerrainWeatheringMode::Off,
            });
            const TerrainBackdropStagePlan plan = plan_terrain_backdrop_stage(
                source, terrain_backdrop_stage_request(mode, 16.0F / 9.0F, 1.0F));
            plans.push_back({
                {"mode", terrain_backdrop_stage_mode_name(plan.mode)},
                {"seed", seed},
                {"source_focus_xz_m", vec2_json(plan.source_focus_xz)},
                {"source_center_height_m", plan.source_center_height_m},
                {"stage_plane_height_m", plan.stage_plane_height_m},
                {"target_height_m", plan.target_height_m},
                {"terrain_vertical_offset_m", plan.terrain_vertical_offset_m},
                {"local_relief_m", plan.local_relief_m},
                {"local_p95_slope", plan.local_p95_slope},
                {"minimum_camera_clearance_m", plan.minimum_camera_clearance_m},
                {"showcase_yaw_radians", plan.showcase_yaw_radians},
                {"stage_radius_m", plan.stage_radius_m},
                {"orbit_radius_m",
                 {plan.orbit_min_radius_m, plan.orbit_default_radius_m, plan.orbit_max_radius_m}},
                {"orbit_elevation_radians",
                 {plan.orbit_min_elevation_radians, plan.orbit_default_elevation_radians,
                  plan.orbit_max_elevation_radians}},
                {"panorama_sector_count", plan.panorama_sector_count},
                {"lower_frame_clear_sector_count", plan.lower_frame_clear_sector_count},
                {"relief_sector_count", plan.relief_sector_count},
                {"minimum_lower_frame_terrain_distance_m",
                 plan.minimum_lower_frame_terrain_distance_m},
                {"candidate_counts",
                 {plan.coarse_candidate_count, plan.refined_candidate_count,
                  plan.full_candidate_count}},
                {"contract_satisfied", plan.contract_satisfied},
                {"score", plan.score},
            });
        }
    }

    const nlohmann::json report{
        {"schema", "cubey.terrain.backdrop-stage.v2"},
        {"source", {{"preset", "mountain"}, {"version", "v2.1"}, {"weathering", "off"}}},
        {"detached_stage_radius_m", 300},
        {"minimum_visible_terrain_distance_m", 3200},
        {"panorama_azimuth_count", 24},
        {"review_azimuth_count", 8},
        {"plans", std::move(plans)},
    };
    std::cout << report.dump(2) << '\n';
    return 0;
}
