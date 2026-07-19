#include "terrain_natural_backdrop_stage.h"

#include <algorithm>
#include <cmath>
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

class NaturalRiseSource final : public cubey::projects::terrain::TerrainHeightSource {
  public:
    [[nodiscard]] cubey::projects::terrain::TerrainHeightSourceMetadata
    metadata() const noexcept override {
        return {
            .id = "natural-stage-test",
            .seed = 9012U,
            .relief_scale_m = 2'000.0F,
            .gradient_step_m = 16.0F,
        };
    }

    [[nodiscard]] float sample_height(
        const cubey::projects::terrain::TerrainQuery& query) const override {
        const float rise = std::max(query.world_xz.x - 2'000.0F, 0.0F) * 0.18F;
        return std::min(rise, 1'800.0F);
    }
};

void test_natural_stage_uses_directional_placement_and_bounded_focus_height() {
    using namespace cubey::projects::terrain;
    const TerrainNaturalBackdropStageRequest request;
    const TerrainNaturalBackdropStagePlan plan =
        plan_terrain_natural_backdrop_stage(NaturalRiseSource{}, request);
    require(plan.placement.contract_satisfied && plan.stage.contract_satisfied,
            "natural stage should preserve the directional placement contract");
    require(plan.placement.coarse_candidate_count == 49U,
            "natural stage should use the finite seven by seven search domain");
    require_near(plan.stage.target_height_m - plan.stage.source_center_height_m, 500.0F, 0.001F,
                 "natural stage should retain the requested focus height when already clear");
    require(plan.stage.minimum_camera_clearance_m >= 10.0F,
            "natural stage should preserve camera clearance across the orbit envelope");
    require_near(plan.centered_search_support_radius_m, 29'900.0F, 0.001F,
                 "natural stage should publish search, refinement, and render support");
    require_near(plan.selected_support_radius_m, 16'400.0F, 0.001F,
                 "natural stage should include one gradient sample in selected support");
}

} // namespace

int main() {
    try {
        test_natural_stage_uses_directional_placement_and_bounded_focus_height();
        std::cout << "terrain_natural_backdrop_stage_tests: ok\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "terrain_natural_backdrop_stage_tests: " << error.what() << '\n';
        return 1;
    }
}
