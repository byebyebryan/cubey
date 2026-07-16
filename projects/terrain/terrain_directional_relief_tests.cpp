#include "terrain_directional_relief.h"

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

class FootprintSource final : public cubey::projects::terrain::TerrainHeightSource {
  public:
    [[nodiscard]] cubey::projects::terrain::TerrainHeightSourceMetadata
    metadata() const noexcept override {
        return {
            .id = "directional-relief-test",
            .seed = 9012U,
            .base_height_m = 100.0F,
            .relief_scale_m = 4'000.0F,
            .gradient_step_m = 8.0F,
        };
    }

    [[nodiscard]] float sample_height(
        const cubey::projects::terrain::TerrainQuery& query) const override {
        return 2'100.0F - std::min(query.footprint_m, 6'000.0F) * 0.2F;
    }
};

void test_composition_is_anchored_and_one_sided() {
    using namespace cubey::projects::terrain;
    const FootprintSource source;
    TerrainDirectionalReliefParameters parameters;
    parameters.mountain_yaw_radians = std::numbers::pi_v<float> * 0.5F;
    const TerrainDirectionalReliefSource relief(source, parameters);

    const TerrainDirectionalReliefSample center =
        relief.sample_composition({.world_xz = {0.0F, 0.0F}, .footprint_m = 16.0F});
    require_near(center.directional_distance_m, 0.0F, 0.001F,
                 "directional warp should remain anchored at the focus");
    require_near(center.broad_gate, 0.0F, 0.0F,
                 "focus should remain on the low side of the rise");
    require_near(center.height_m, center.floor_height_m, 0.001F,
                 "focus height should resolve to the filtered floor");

    const TerrainDirectionalReliefSample mountain =
        relief.sample_composition({.world_xz = {14'000.0F, 0.0F}, .footprint_m = 16.0F});
    require_near(mountain.broad_gate, 1.0F, 0.0F,
                 "far mountain side should restore broad relief");
    require_near(mountain.detail_gate, 1.0F, 0.0F,
                 "far mountain side should restore source detail");
    require_near(mountain.height_m, mountain.source_height_m, 0.001F,
                 "far mountain side should preserve the wrapped source");

    const TerrainDirectionalReliefSample open =
        relief.sample_composition({.world_xz = {-14'000.0F, 0.0F}, .footprint_m = 16.0F});
    require_near(open.broad_gate, 0.0F, 0.0F,
                 "opposite horizon should remain open rather than forming a ring");
    require_near(open.height_m, open.floor_height_m, 0.001F,
                 "opposite horizon should retain only the quiet floor");
}

void test_composition_is_deterministic() {
    using namespace cubey::projects::terrain;
    const FootprintSource source;
    TerrainDirectionalReliefParameters parameters;
    parameters.focus_xz = {4'000.0F, -2'000.0F};
    const TerrainDirectionalReliefSource first(source, parameters);
    const TerrainDirectionalReliefSource second(source, parameters);
    const TerrainQuery query{.world_xz = {10'000.0F, 3'000.0F}, .footprint_m = 64.0F};
    require(first.sample_height(query) == second.sample_height(query),
            "directional relief should be deterministic for the same source and parameters");
    require(first.metadata().seed == source.metadata().seed &&
                first.metadata().id == "terrain-directional-relief",
            "directional relief should publish stable source metadata");
}

} // namespace

int main() {
    try {
        test_composition_is_anchored_and_one_sided();
        test_composition_is_deterministic();
        std::cout << "terrain_directional_relief_tests: ok\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "terrain_directional_relief_tests: " << error.what() << '\n';
        return 1;
    }
}
