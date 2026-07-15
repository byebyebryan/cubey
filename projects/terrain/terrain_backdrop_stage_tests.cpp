#include "terrain_backdrop_stage.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
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
    require_near(detached.orbit_max_radius_m, 150.0F, 0.0F,
                 "detached stage should publish the maximum orbit radius");
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
        require(first.panorama_sector_count == 24U && first.horizon_clear_sector_count == 24U,
                "detached stage should keep every panoramic sector clear");
        require(first.relief_sector_count >= 14U,
                "detached stage should retain useful relief in most sectors");
        require(first.stage_plane_height_m >= first.source_center_height_m + 239.9F,
                "detached stage should keep its ownership boundary below the orbit frame");
    }
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
    require(plan.coarse_candidate_count == 289U && plan.full_candidate_count == 16U,
            "grounded stage should publish its bounded search budget");
}

} // namespace

int main() {
    try {
        test_stage_requests_publish_the_medium_scene_contract();
        test_detached_stage_search_is_deterministic_and_panoramic();
        test_grounded_stage_returns_a_finite_natural_candidate();
        std::cout << "terrain_backdrop_stage_tests: ok\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "terrain_backdrop_stage_tests: " << error.what() << '\n';
        return 1;
    }
}
