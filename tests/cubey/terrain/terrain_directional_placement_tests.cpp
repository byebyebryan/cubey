#include <cubey/asset/terrain_height_source.h>
#include <cubey/terrain/terrain_directional_placement.h>

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

class DirectionalRiseSource final : public cubey::asset::TerrainHeightSource {
  public:
    explicit DirectionalRiseSource(cubey::math::Vec2 origin = {}) : origin_(origin) {}

    [[nodiscard]] cubey::asset::TerrainHeightSourceMetadata metadata() const noexcept override {
        return {.id = "directional-rise-test", .seed = 7U, .relief_scale_m = 2'000.0F};
    }

    [[nodiscard]] float sample_height(const cubey::asset::TerrainQuery& query) const override {
        const float rise = std::max(query.world_xz.x - origin_.x - 2'000.0F, 0.0F) * 0.18F;
        return std::min(rise, 1'800.0F);
    }

  private:
    cubey::math::Vec2 origin_{};
};

void test_fixed_focus_finds_a_directional_mountain_arc() {
    using namespace cubey::terrain;
    const TerrainDirectionalPlacementPlan plan = evaluate_terrain_directional_placement(
        DirectionalRiseSource{}, TerrainDirectionalPlacementRequest{}, {0.0F, 0.0F});
    require(plan.contract_satisfied,
            "directional rise should satisfy the low-side placement contract");
    require(plan.mountain_sector_count >= 4U && plan.mountain_sector_count <= 14U,
            "directional rise should not become panoramic mountain coverage");
    require(plan.largest_mountain_arc_sectors >= 3U && plan.largest_open_arc_sectors >= 4U,
            "directional rise should retain both mountain and open arcs");
    require(std::abs(plan.mountain_yaw_radians - std::numbers::pi_v<float> * 0.5F) < 0.35F,
            "directional rise should face the positive x mountain side");
}

void test_search_is_deterministic() {
    using namespace cubey::terrain;
    const DirectionalRiseSource source;
    const TerrainDirectionalPlacementPlan first = plan_terrain_directional_placement(source);
    const TerrainDirectionalPlacementPlan second = plan_terrain_directional_placement(source);
    require(first.source_focus_xz.x == second.source_focus_xz.x &&
                first.source_focus_xz.y == second.source_focus_xz.y &&
                first.mountain_yaw_radians == second.mountain_yaw_radians &&
                first.score == second.score,
            "directional placement search should be deterministic");
    require(first.coarse_candidate_count == 289U && first.full_candidate_count == 16U,
            "directional placement should preserve its bounded search budget");
}

void test_search_budget_can_fit_a_bounded_source() {
    using namespace cubey::terrain;
    const DirectionalRiseSource source;
    TerrainDirectionalPlacementRequest request;
    request.search_extent_m = 12'000.0F;
    request.search_step_m = 4'000.0F;
    const TerrainDirectionalPlacementPlan plan =
        plan_terrain_directional_placement(source, request);
    require(plan.coarse_candidate_count == 49U,
            "directional placement should support a finite seven by seven search domain");
}

void test_search_can_be_centered_on_a_translated_source() {
    using namespace cubey::terrain;
    const cubey::math::Vec2 translation{120'000.0F, -75'000.0F};
    const TerrainDirectionalPlacementRequest request;
    const TerrainDirectionalPlacementPlan origin =
        plan_terrain_directional_placement(DirectionalRiseSource{}, request);
    const TerrainDirectionalPlacementPlan translated = plan_terrain_directional_placement(
        DirectionalRiseSource{translation}, request, translation);
    require(std::abs(translated.source_focus_xz.x - origin.source_focus_xz.x - translation.x) <
                    0.001F &&
                std::abs(translated.source_focus_xz.y - origin.source_focus_xz.y - translation.y) <
                    0.001F &&
                translated.contract_satisfied == origin.contract_satisfied &&
                translated.score == origin.score,
            "directional placement should translate with its explicit search center");
}

} // namespace

int main() {
    try {
        test_fixed_focus_finds_a_directional_mountain_arc();
        test_search_is_deterministic();
        test_search_budget_can_fit_a_bounded_source();
        test_search_can_be_centered_on_a_translated_source();
        std::cout << "terrain_directional_placement_tests: ok\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "terrain_directional_placement_tests: " << error.what() << '\n';
        return 1;
    }
}
