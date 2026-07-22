#include "terrain_backdrop_placement.h"

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

template <typename Function> void require_throws(Function&& function, std::string_view message) {
    try {
        function();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error(std::string(message));
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

    [[nodiscard]] float
    sample_height(const cubey::projects::terrain::TerrainQuery& query) const override {
        const float rise = std::max(query.world_xz.x - 2'000.0F, 0.0F) * 0.18F;
        return std::min(rise, 1'800.0F);
    }
};

class FlatSource final : public cubey::projects::terrain::TerrainHeightSource {
  public:
    [[nodiscard]] cubey::projects::terrain::TerrainHeightSourceMetadata
    metadata() const noexcept override {
        return {
            .id = "flat-stage-test",
            .seed = 0U,
            .relief_scale_m = 1.0F,
            .gradient_step_m = 16.0F,
        };
    }

    [[nodiscard]] float
    sample_height(const cubey::projects::terrain::TerrainQuery&) const override {
        return 0.0F;
    }
};

constexpr cubey::projects::terrain::TerrainHeightSourceBounds kLargeBounds{
    .minimum_xz = {-50'000.0F, -50'000.0F},
    .maximum_xz = {50'000.0F, 50'000.0F},
};

void test_selected_mode_preserves_directional_placement_and_focus_height() {
    using namespace cubey::projects::terrain;
    const TerrainBackdropPlacementRequest request;
    const TerrainBackdropPlacementPlan plan =
        plan_terrain_backdrop_placement(NaturalRiseSource{}, kLargeBounds, request);
    require(plan.mode == TerrainPlacementMode::Selected && plan.placement.contract_satisfied &&
                plan.stage.contract_satisfied,
            "selected stage should preserve placement and camera contracts");
    require(plan.placement.coarse_candidate_count == 49U,
            "selected stage should retain the finite seven by seven search domain");
    require_near(plan.stage.target_height_m - plan.stage.source_center_height_m, 500.0F, 0.001F,
                 "selected stage should retain the requested focus height when already clear");
    require_near(plan.centered_search_support_radius_m, 29'900.0F, 0.001F,
                 "selected stage should publish search, refinement, and render support");
    require_near(plan.selected_support_radius_m, 16'400.0F, 0.001F,
                 "selected stage should include one gradient sample in render support");
}

void test_raw_center_reports_failed_composition_without_rejecting_the_stage() {
    using namespace cubey::projects::terrain;
    TerrainBackdropPlacementRequest request;
    request.mode = TerrainPlacementMode::RawCenter;
    const TerrainHeightSourceBounds asymmetric_bounds{
        .minimum_xz = {-45'000.0F, -38'000.0F},
        .maximum_xz = {35'000.0F, 42'000.0F},
    };
    const TerrainBackdropPlacementPlan plan =
        plan_terrain_backdrop_placement(FlatSource{}, asymmetric_bounds, request);
    require_near(plan.placement.source_focus_xz.x, -5'000.0F, 0.001F,
                 "raw center should use the exact geometric source center");
    require_near(plan.placement.source_focus_xz.y, 2'000.0F, 0.001F,
                 "raw center should use the exact geometric source center");
    require(!plan.placement.contract_satisfied && plan.stage.contract_satisfied,
            "raw center should report failed composition while retaining camera clearance");
    require_near(plan.stage.showcase_yaw_radians, 0.0F, 0.001F,
                 "raw center should not curate its initial heading");
}

void test_selected_failure_reports_the_failed_contract() {
    using namespace cubey::projects::terrain;
    try {
        static_cast<void>(plan_terrain_backdrop_placement(FlatSource{}, kLargeBounds,
                                                          TerrainBackdropPlacementRequest{}));
    } catch (const std::exception& error) {
        const std::string message = error.what();
        require(message.find("mountain sectors 0 outside [4, 14]") != std::string::npos &&
                    message.find("mountain arc 0 < 3") != std::string::npos,
                "selected rejection should identify its failed directional thresholds");
        return;
    }
    throw std::runtime_error("flat terrain should fail selected placement");
}

void test_raw_sample_is_indexed_deterministic_and_coverage_safe() {
    using namespace cubey::projects::terrain;
    TerrainBackdropPlacementRequest request;
    request.mode = TerrainPlacementMode::RawSample;
    const float support = terrain_backdrop_selected_support_radius(request, 16.0F);
    cubey::math::Vec2 previous{};
    for (std::uint32_t index = 0U; index < 3U; ++index) {
        request.sample_index = index;
        const TerrainBackdropPlacementPlan first =
            plan_terrain_backdrop_placement(FlatSource{}, kLargeBounds, request);
        const TerrainBackdropPlacementPlan second =
            plan_terrain_backdrop_placement(FlatSource{}, kLargeBounds, request);
        require(first.placement.source_focus_xz.x == second.placement.source_focus_xz.x &&
                    first.placement.source_focus_xz.y == second.placement.source_focus_xz.y,
                "raw sample index should select a deterministic coordinate");
        require(terrain_height_source_bounds_contains_disk(
                    kLargeBounds, first.placement.source_focus_xz, support),
                "raw sample should retain the complete render support disk");
        const float x_index =
            (first.placement.source_focus_xz.x - kLargeBounds.minimum_xz.x) / 16.0F;
        const float z_index =
            (first.placement.source_focus_xz.y - kLargeBounds.minimum_xz.y) / 16.0F;
        require_near(x_index, std::round(x_index), 0.001F,
                     "raw sample x should align to source spacing");
        require_near(z_index, std::round(z_index), 0.001F,
                     "raw sample z should align to source spacing");
        require(!first.placement.contract_satisfied && first.stage.contract_satisfied,
                "raw sample should not reject a failed directional score");
        if (index > 0U) {
            require(first.placement.source_focus_xz.x != previous.x ||
                        first.placement.source_focus_xz.y != previous.y,
                    "representative raw indexes should select distinct coordinates");
        }
        previous = first.placement.source_focus_xz;
    }
}

void test_placement_rejects_insufficient_source_coverage() {
    using namespace cubey::projects::terrain;
    TerrainBackdropPlacementRequest request;
    request.mode = TerrainPlacementMode::RawSample;
    const TerrainHeightSourceBounds small_bounds{
        .minimum_xz = {-10'000.0F, -10'000.0F},
        .maximum_xz = {10'000.0F, 10'000.0F},
    };
    require_throws(
        [&request, &small_bounds] {
            static_cast<void>(plan_terrain_backdrop_placement(FlatSource{}, small_bounds, request));
        },
        "raw placement should reject a field that cannot cover the render disk");
}

} // namespace

int main() {
    try {
        test_selected_mode_preserves_directional_placement_and_focus_height();
        test_raw_center_reports_failed_composition_without_rejecting_the_stage();
        test_selected_failure_reports_the_failed_contract();
        test_raw_sample_is_indexed_deterministic_and_coverage_safe();
        test_placement_rejects_insufficient_source_coverage();
        std::cout << "terrain_backdrop_placement_tests: ok\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "terrain_backdrop_placement_tests: " << error.what() << '\n';
        return 1;
    }
}
