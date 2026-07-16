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

class DirectionalStageSource final
    : public cubey::projects::terrain::TerrainHeightSource {
  public:
    [[nodiscard]] cubey::projects::terrain::TerrainHeightSourceMetadata
    metadata() const noexcept override {
        return {.id = "directional-stage-test", .seed = 11U, .relief_scale_m = 2'000.0F};
    }

    [[nodiscard]] float sample_height(
        const cubey::projects::terrain::TerrainQuery& query) const override {
        const float broad_rise = std::clamp((query.world_xz.x - 2'000.0F) / 8'000.0F,
                                            0.0F, 1.0F);
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
         }) {
        require(terrain_directional_backdrop_lane_from_name(
                    terrain_directional_backdrop_lane_name(lane)) == lane,
                "directional backdrop lane names should round trip");
    }
}

void test_stage_is_grounded_and_camera_clear() {
    using namespace cubey::projects::terrain;
    const DirectionalStageSource source;
    const TerrainDirectionalPlacementPlan placement = evaluate_terrain_directional_placement(
        source, directional_backdrop_placement_request(), {0.0F, 0.0F});
    const TerrainBackdropStagePlan stage =
        make_directional_backdrop_stage_plan(source, placement);
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

} // namespace

int main() {
    try {
        test_lane_names_round_trip();
        test_stage_is_grounded_and_camera_clear();
        std::cout << "terrain_directional_backdrop_study_tests: ok\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "terrain_directional_backdrop_study_tests: " << error.what() << '\n';
        return 1;
    }
}
