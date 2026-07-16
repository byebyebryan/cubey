#include "terrain_directional_backdrop_study.h"

#include <algorithm>
#include <cmath>
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

template <typename Function>
void require_throws(Function&& function, std::string_view message) {
    try {
        function();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error(std::string(message));
}

class DirectionalStageSource final : public cubey::projects::terrain::TerrainHeightSource {
  public:
    [[nodiscard]] cubey::projects::terrain::TerrainHeightSourceMetadata
    metadata() const noexcept override {
        return {.id = "directional-stage-test", .seed = 11U, .relief_scale_m = 2'000.0F};
    }

    [[nodiscard]] float
    sample_height(const cubey::projects::terrain::TerrainQuery& query) const override {
        const float broad_rise = std::clamp((query.world_xz.x - 2'000.0F) / 8'000.0F, 0.0F, 1.0F);
        return 120.0F + 1'500.0F * broad_rise;
    }
};

void test_lane_names_round_trip() {
    using namespace cubey::projects::terrain;
    for (const TerrainDirectionalBackdropLane lane : {
             TerrainDirectionalBackdropLane::HardCut,
             TerrainDirectionalBackdropLane::ContinuousCurrent,
             TerrainDirectionalBackdropLane::Placement,
             TerrainDirectionalBackdropLane::Shaped,
             TerrainDirectionalBackdropLane::ExpandedShaped,
             TerrainDirectionalBackdropLane::ExpandedRadial,
             TerrainDirectionalBackdropLane::CachedRadial,
         }) {
        require(terrain_directional_backdrop_lane_from_name(
                    terrain_directional_backdrop_lane_name(lane)) == lane,
                "directional backdrop lane names should round trip");
    }
}

void test_cached_radial_stride_contract() {
    using namespace cubey::projects::terrain;
    require(cached_radial_backdrop_render_stride() == 3U,
            "cached radial should default to production-style stride three");
    require(cached_radial_backdrop_render_stride(2U) == 2U &&
                cached_radial_backdrop_render_stride(3U) == 3U,
            "cached radial should accept the fixed comparison strides");
    require_throws([] { (void)cached_radial_backdrop_render_stride(1U); },
                   "cached radial should reserve stride one for the expanded control");
    require_throws([] { (void)cached_radial_backdrop_render_stride(4U); },
                   "cached radial should reject unreviewed coarser strides");
}

void test_stage_is_grounded_and_camera_clear() {
    using namespace cubey::projects::terrain;
    const DirectionalStageSource source;
    const TerrainDirectionalPlacementPlan placement = evaluate_terrain_directional_placement(
        source, directional_backdrop_placement_request(), {0.0F, 0.0F});
    const TerrainBackdropStagePlan stage = make_directional_backdrop_stage_plan(source, placement);
    require(stage.mode == TerrainBackdropStageMode::Grounded,
            "directional stage should use grounded ownership");
    require(stage.source_focus_xz.x == 0.0F && stage.source_focus_xz.y == 0.0F,
            "directional stage should preserve the measured source focus");
    require(stage.minimum_camera_clearance_m >= 10.0F,
            "directional stage should keep the tested orbit envelope above terrain");
    require(stage.orbit_min_radius_m == 50.0F && stage.orbit_default_radius_m == 100.0F &&
                stage.orbit_max_radius_m == 250.0F,
            "directional stage should publish the fixed comparison orbit envelope");
    require(std::abs(stage.showcase_yaw_radians - std::numbers::pi_v<float> * 0.5F) < 0.35F,
            "directional stage should face the measured mountain arc");
}

void test_expanded_stage_uses_far_field_scale() {
    using namespace cubey::projects::terrain;
    const DirectionalStageSource source;
    const TerrainDirectionalPlacementPlan placement = evaluate_terrain_directional_placement(
        source, directional_backdrop_placement_request(), {0.0F, 0.0F});
    const TerrainDirectionalReliefParameters relief =
        expanded_directional_backdrop_relief_parameters(placement);
    require(relief.broad_start_m == 6'000.0F && relief.broad_full_m == 18'000.0F &&
                relief.detail_start_m == 10'000.0F && relief.detail_full_m == 26'000.0F,
            "expanded relief should restore structure and detail over far-field distances");
    require(expanded_backdrop_outer_radius_m() == 32'768.0F,
            "expanded backdrop should provide room beyond the relief transition");
    const TerrainRadialReliefParameters radial =
        expanded_radial_backdrop_relief_parameters(placement);
    require(radial.floor_footprint_m == 6'000.0F && radial.broad_start_m == 1'000.0F &&
                radial.broad_full_m == 24'000.0F && radial.detail_start_m == 5'000.0F &&
                radial.detail_full_m == 30'000.0F,
            "radial relief should use a broad far-field transition band");
    require(radial.focus_xz.x == placement.source_focus_xz.x &&
                radial.focus_xz.y == placement.source_focus_xz.y,
            "radial relief should remain centered on the selected stage focus");

    const TerrainBackdropStagePlan stage =
        make_directional_backdrop_stage_plan(source, placement, 1.0F,
                                             {
                                                 .focus_height_m = 500.0F,
                                                 .orbit_min_radius_m = 100.0F,
                                                 .orbit_default_radius_m = 400.0F,
                                                 .orbit_max_radius_m = 1'000.0F,
                                             });
    require(stage.target_height_m - stage.source_center_height_m >= 500.0F,
            "expanded stage focus should remain at least 500 m above the valley floor");
    require(stage.orbit_min_radius_m == 100.0F && stage.orbit_default_radius_m == 400.0F &&
                stage.orbit_max_radius_m == 1'000.0F,
            "expanded stage should allow a one-kilometer orbit");
    require(stage.minimum_camera_clearance_m >= 10.0F,
            "expanded orbit envelope should remain above terrain");
}

} // namespace

int main() {
    try {
        test_lane_names_round_trip();
        test_cached_radial_stride_contract();
        test_stage_is_grounded_and_camera_clear();
        test_expanded_stage_uses_far_field_scale();
        std::cout << "terrain_directional_backdrop_study_tests: ok\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "terrain_directional_backdrop_study_tests: " << error.what() << '\n';
        return 1;
    }
}
