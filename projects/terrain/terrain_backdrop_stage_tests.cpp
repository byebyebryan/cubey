#include "terrain_backdrop_stage.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <numbers>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

void require_near(float actual, float expected, float tolerance, std::string_view message) {
    require(std::abs(actual - expected) <= tolerance, message);
}

void test_stage_requests_publish_the_medium_scene_contract() {
    require(cubey::projects::terrain::terrain_backdrop_stage_mode_from_name("") ==
                    cubey::projects::terrain::TerrainBackdropStageMode::Detached &&
                cubey::projects::terrain::terrain_backdrop_stage_mode_from_name("grounded") ==
                    cubey::projects::terrain::TerrainBackdropStageMode::Grounded,
            "stage mode names should preserve detached defaults and grounded diagnostics");
    const auto detached = cubey::projects::terrain::terrain_backdrop_stage_request(
        cubey::projects::terrain::TerrainBackdropStageMode::Detached);
    const auto grounded = cubey::projects::terrain::terrain_backdrop_stage_request(
        cubey::projects::terrain::TerrainBackdropStageMode::Grounded);
    require_near(detached.stage_radius_m, 300.0F, 0.0F,
                 "detached stage should reserve the foreground radius");
    require_near(detached.orbit_min_radius_m, 50.0F, 0.0F,
                 "detached stage should publish the minimum orbit radius");
    require_near(detached.orbit_default_radius_m, 100.0F, 0.0F,
                 "detached stage should publish the default orbit radius");
    require_near(detached.orbit_max_radius_m, 250.0F, 0.0F,
                 "detached stage should publish the maximum orbit radius");
    require_near(detached.orbit_min_elevation_radians, 0.0F, 0.0F,
                 "detached stage should allow a level orbit");
    require_near(detached.orbit_max_elevation_radians, 30.0F * std::numbers::pi_v<float> / 180.0F,
                 0.000001F, "detached stage should allow a broader elevated orbit");
    require_near(detached.minimum_visible_terrain_distance_m, 3'200.0F, 0.0F,
                 "detached stage should publish the far-field visibility contract");
    require(detached.orbit_max_elevation_radians < grounded.orbit_max_elevation_radians,
            "detached stage should preserve a shallower orbit than grounded diagnostics");
}

void test_detached_stage_search_is_deterministic_and_panoramic() {
    constexpr std::array<std::uint64_t, 3> seeds{0U, 9012U, 12345U};
    for (const std::uint64_t seed : seeds) {
        const auto source = cubey::projects::terrain::resolve_terrain_source_parameters({
            .seed = seed,
            .preset = cubey::projects::terrain::TerrainPreset::Mountain,
            .version = cubey::projects::terrain::TerrainSourceVersion::V2_1,
        });
        const auto request = cubey::projects::terrain::terrain_backdrop_stage_request(
            cubey::projects::terrain::TerrainBackdropStageMode::Detached);
        const auto first = cubey::projects::terrain::plan_terrain_backdrop_stage(source, request);
        const auto second = cubey::projects::terrain::plan_terrain_backdrop_stage(source, request);
        require_near(first.source_focus_xz.x, second.source_focus_xz.x, 0.0F,
                     "detached stage x placement should be deterministic");
        require_near(first.source_focus_xz.y, second.source_focus_xz.y, 0.0F,
                     "detached stage z placement should be deterministic");
        require_near(first.score, second.score, 0.0F,
                     "detached stage score should be deterministic");
        require(first.contract_satisfied,
                "detached stage should satisfy the panoramic contract for review seeds");
        require(first.panorama_sector_count == 24U &&
                    first.lower_frame_clear_sector_count == 24U,
                "detached stage should keep every lower-frame sector clear");
        require(first.relief_sector_count >= 14U,
                "detached stage should retain useful relief in most sectors");
        require(first.minimum_lower_frame_terrain_distance_m >= 3'200.0F,
                "detached stage should keep terrain outside the lower-frame distance");
        require_near(first.terrain_vertical_offset_m, -first.target_height_m, 0.0F,
                     "detached stage should map the physical focus to local zero");
    }
}

void test_stage_search_budget_can_fit_a_bounded_source() {
    const auto source = cubey::projects::terrain::resolve_terrain_source_parameters({
        .seed = 9012U,
        .preset = cubey::projects::terrain::TerrainPreset::Mountain,
        .version = cubey::projects::terrain::TerrainSourceVersion::V2_1,
    });
    auto request = cubey::projects::terrain::terrain_backdrop_stage_request(
        cubey::projects::terrain::TerrainBackdropStageMode::Detached);
    request.search_extent_m = 12'000.0F;
    request.search_step_m = 4'000.0F;

    const auto plan = cubey::projects::terrain::plan_terrain_backdrop_stage(source, request);

    require(plan.coarse_candidate_count == 49U,
            "bounded terrain sources should be able to narrow the coarse search domain");
}

void test_grounded_stage_returns_a_finite_natural_candidate() {
    const auto source = cubey::projects::terrain::resolve_terrain_source_parameters({
        .seed = 9012U,
        .preset = cubey::projects::terrain::TerrainPreset::Mountain,
        .version = cubey::projects::terrain::TerrainSourceVersion::V2_1,
    });
    const auto request = cubey::projects::terrain::terrain_backdrop_stage_request(
        cubey::projects::terrain::TerrainBackdropStageMode::Grounded);
    const auto plan = cubey::projects::terrain::plan_terrain_backdrop_stage(source, request);
    require(std::isfinite(plan.source_focus_xz.x) && std::isfinite(plan.source_focus_xz.y) &&
                std::isfinite(plan.local_relief_m) && std::isfinite(plan.local_p95_slope) &&
                std::isfinite(plan.score),
            "grounded stage should return finite best-effort diagnostics");
    require_near(plan.stage_plane_height_m, plan.source_center_height_m, 0.001F,
                 "grounded stage should preserve natural center height");
    require_near(plan.terrain_vertical_offset_m, -plan.target_height_m, 0.0F,
                 "grounded stage should publish the same local coordinate mapping");
    require(plan.coarse_candidate_count == 289U && plan.full_candidate_count == 16U,
            "grounded stage should publish its bounded search budget");
}

void test_detached_distance_solver_generalizes_across_presets() {
    constexpr std::array presets{
        cubey::projects::terrain::TerrainPreset::Mountain,
        cubey::projects::terrain::TerrainPreset::Upland,
        cubey::projects::terrain::TerrainPreset::Plains,
    };
    for (const auto preset : presets) {
        const auto source = cubey::projects::terrain::resolve_terrain_source_parameters({
            .seed = 9012U,
            .preset = preset,
            .version = preset == cubey::projects::terrain::TerrainPreset::Mountain
                           ? cubey::projects::terrain::TerrainSourceVersion::V2_1
                           : cubey::projects::terrain::TerrainSourceVersion::V1,
        });
        const auto request = cubey::projects::terrain::terrain_backdrop_stage_request(
            cubey::projects::terrain::TerrainBackdropStageMode::Detached);
        const auto plan = cubey::projects::terrain::plan_terrain_backdrop_stage(source, request);
        require(plan.lower_frame_clear_sector_count == 24U &&
                    plan.minimum_lower_frame_terrain_distance_m >=
                        request.minimum_visible_terrain_distance_m,
                "detached stage should preserve lower-frame distance across terrain presets");
    }
}

} // namespace

int main() {
    try {
        test_stage_requests_publish_the_medium_scene_contract();
        test_detached_stage_search_is_deterministic_and_panoramic();
        test_stage_search_budget_can_fit_a_bounded_source();
        test_grounded_stage_returns_a_finite_natural_candidate();
        test_detached_distance_solver_generalizes_across_presets();
        std::cout << "terrain_backdrop_stage_tests: ok\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "terrain_backdrop_stage_tests: " << error.what() << '\n';
        return 1;
    }
}
