#include "terrain_radial_relief.h"

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
            .id = "radial-relief-test",
            .seed = 9012U,
            .base_height_m = 100.0F,
            .relief_scale_m = 4'000.0F,
            .gradient_step_m = 8.0F,
        };
    }

    [[nodiscard]] float
    sample_height(const cubey::projects::terrain::TerrainQuery& query) const override {
        return 2'100.0F - std::min(query.footprint_m, 8'000.0F) * 0.2F;
    }
};

void test_composition_is_centered_and_isotropic() {
    using namespace cubey::projects::terrain;
    const FootprintSource source;
    const TerrainRadialReliefSource relief(source, {});

    const TerrainRadialReliefSample center =
        relief.sample_composition({.world_xz = {0.0F, 0.0F}, .footprint_m = 16.0F});
    require_near(center.radial_distance_m, 0.0F, 0.0F,
                 "radial transition should remain anchored at the focus");
    require_near(center.broad_gate, 0.0F, 0.0F, "focus should retain only the filtered floor");
    require_near(center.height_m, center.floor_height_m, 0.001F,
                 "focus height should resolve to the filtered floor");

    const TerrainRadialReliefSample east =
        relief.sample_composition({.world_xz = {15'000.0F, 0.0F}, .footprint_m = 16.0F});
    const TerrainRadialReliefSample north =
        relief.sample_composition({.world_xz = {0.0F, 15'000.0F}, .footprint_m = 16.0F});
    require_near(east.broad_gate, north.broad_gate, 0.0001F,
                 "equal radii should produce equal broad gates");
    require_near(east.detail_gate, north.detail_gate, 0.0001F,
                 "equal radii should produce equal detail gates");

    const TerrainRadialReliefSample far =
        relief.sample_composition({.world_xz = {-32'000.0F, 0.0F}, .footprint_m = 16.0F});
    require_near(far.broad_gate, 1.0F, 0.0F, "far field should restore broad relief");
    require_near(far.detail_gate, 1.0F, 0.0F, "far field should restore source detail");
    require_near(far.height_m, far.source_height_m, 0.001F,
                 "far field should preserve the wrapped source");
}

void test_composition_is_deterministic() {
    using namespace cubey::projects::terrain;
    const FootprintSource source;
    TerrainRadialReliefParameters parameters;
    parameters.focus_xz = {4'000.0F, -2'000.0F};
    const TerrainRadialReliefSource first(source, parameters);
    const TerrainRadialReliefSource second(source, parameters);
    const TerrainQuery query{.world_xz = {10'000.0F, 3'000.0F}, .footprint_m = 64.0F};
    require(first.sample_height(query) == second.sample_height(query),
            "radial relief should be deterministic for the same source and parameters");
    require(first.metadata().seed == source.metadata().seed &&
                first.metadata().id == "terrain-radial-relief",
            "radial relief should publish stable source metadata");
}

void test_zero_detail_footprint_preserves_current_composition() {
    using namespace cubey::projects::terrain;
    const FootprintSource source;
    TerrainRadialReliefParameters parameters;
    parameters.detail_footprint_m = 0.0F;
    const TerrainRadialReliefSource relief(source, parameters);
    for (const float radius : {0.0F, 4'000.0F, 15'000.0F, 32'000.0F}) {
        const TerrainQuery query{.world_xz = {radius, 0.0F}, .footprint_m = 16.0F};
        const TerrainRadialReliefSample sample = relief.sample_composition(query);
        const float expected =
            sample.floor_height_m +
            sample.broad_gate * (sample.structure_height_m - sample.floor_height_m) +
            sample.detail_gate * (sample.source_height_m - sample.structure_height_m);
        require(sample.detail_height_m == sample.source_height_m,
                "zero detail footprint should retain the full source sample");
        require(sample.height_m == expected,
                "zero detail footprint should preserve the previous composition exactly");
    }
}

void test_filtered_detail_uses_only_source_footprints() {
    using namespace cubey::projects::terrain;
    const FootprintSource source;
    TerrainRadialReliefParameters parameters;
    parameters.structure_footprint_m = 900.0F;
    parameters.detail_footprint_m = 180.0F;
    parameters.detail_full_m = 24'000.0F;
    const TerrainRadialReliefSource relief(source, parameters);

    const TerrainRadialReliefSample far =
        relief.sample_composition({.world_xz = {24'000.0F, 0.0F}, .footprint_m = 16.0F});
    require_near(far.structure_height_m, source.sample_height({.footprint_m = 900.0F}), 0.0F,
                 "candidate structure should use the configured coherent footprint");
    require_near(far.detail_height_m, source.sample_height({.footprint_m = 180.0F}), 0.0F,
                 "candidate detail should use the configured coherent footprint");
    require(far.detail_height_m != far.source_height_m,
            "candidate diagnostics should distinguish filtered detail from the full source");
    require_near(far.height_m, far.detail_height_m, 0.001F,
                 "candidate far field should resolve to filtered coherent detail");
}

} // namespace

int main() {
    try {
        test_composition_is_centered_and_isotropic();
        test_composition_is_deterministic();
        test_zero_detail_footprint_preserves_current_composition();
        test_filtered_detail_uses_only_source_footprints();
        std::cout << "terrain_radial_relief_tests: ok\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "terrain_radial_relief_tests: " << error.what() << '\n';
        return 1;
    }
}
