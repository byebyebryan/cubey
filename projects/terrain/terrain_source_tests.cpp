#include "terrain_source.h"

#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

using cubey::projects::terrain::TerrainPreset;
using cubey::projects::terrain::TerrainQuery;
using cubey::projects::terrain::TerrainSourceConfig;
using cubey::projects::terrain::TerrainWeatheringMode;

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

void require_near(float actual, float expected, float tolerance, std::string_view message) {
    require(std::abs(actual - expected) <= tolerance, message);
}

void test_names_and_validation() {
    require(cubey::projects::terrain::terrain_preset_from_name("mountain") ==
                TerrainPreset::Mountain,
            "terrain should parse mountain preset");
    require(cubey::projects::terrain::terrain_preset_name(TerrainPreset::Plains) == "plains",
            "terrain should name plains preset");
    require(cubey::projects::terrain::terrain_weathering_mode_from_name("local") ==
                TerrainWeatheringMode::Local,
            "terrain should parse local weathering");

    bool rejected = false;
    try {
        (void)cubey::projects::terrain::terrain_preset_from_name("canyon");
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    require(rejected, "terrain should reject unknown presets");
}

void test_source_is_deterministic_and_uses_full_seed() {
    TerrainSourceConfig config{
        .seed = 9012U,
        .preset = TerrainPreset::Mountain,
    };
    const auto first = cubey::projects::terrain::resolve_terrain_source_parameters(config);
    const auto repeat = cubey::projects::terrain::resolve_terrain_source_parameters(config);
    const TerrainQuery query{.world_xz = {1532.5F, -873.25F}};
    const auto first_sample = cubey::projects::terrain::sample_terrain(first, query);
    const auto repeat_sample = cubey::projects::terrain::sample_terrain(repeat, query);
    require_near(first_sample.height_m, repeat_sample.height_m, 0.0F,
                 "terrain source should be deterministic");
    require_near(first_sample.gradient_xz.x, repeat_sample.gradient_xz.x, 0.0F,
                 "terrain gradient should be deterministic");

    config.seed ^= 1ULL << 48U;
    const auto changed = cubey::projects::terrain::resolve_terrain_source_parameters(config);
    const auto changed_sample = cubey::projects::terrain::sample_terrain(changed, query);
    require(std::abs(first_sample.height_m - changed_sample.height_m) > 0.01F,
            "terrain source should use upper seed bits");
}

void test_presets_have_ordered_relief() {
    std::array<float, 3> spans{};
    std::array<float, 3> slopes{};
    constexpr std::array presets{TerrainPreset::Mountain, TerrainPreset::Upland,
                                 TerrainPreset::Plains};
    for (std::size_t index = 0; index < presets.size(); ++index) {
        const auto parameters = cubey::projects::terrain::resolve_terrain_source_parameters({
            .seed = 9012U,
            .preset = presets[index],
        });
        const auto summary = cubey::projects::terrain::summarize_terrain_source(
            parameters, {0.0F, 0.0F}, 16'384.0F, 33U);
        require(std::isfinite(summary.mean_height_m) && std::isfinite(summary.mean_slope),
                "terrain summary should stay finite");
        spans[index] = summary.max_height_m - summary.min_height_m;
        slopes[index] = summary.mean_slope;
    }
    require(spans[0] > spans[1] * 1.8F && spans[1] > spans[2] * 2.0F,
            "terrain preset relief should decrease from mountain to plains");
    require(slopes[0] > slopes[1] && slopes[1] > slopes[2],
            "terrain preset slope should decrease from mountain to plains");
}

void test_footprint_filters_unresolved_detail() {
    const auto parameters = cubey::projects::terrain::resolve_terrain_source_parameters({
        .seed = 12345U,
        .preset = TerrainPreset::Mountain,
    });
    double full_variation = 0.0;
    double coarse_variation = 0.0;
    float previous_full = 0.0F;
    float previous_coarse = 0.0F;
    for (int index = 0; index < 64; ++index) {
        const cubey::math::Vec2 point{static_cast<float>(index) * 16.0F, 211.0F};
        const float full = cubey::projects::terrain::sample_terrain_base_height(
            parameters, {.world_xz = point, .footprint_m = 0.0F});
        const float coarse = cubey::projects::terrain::sample_terrain_base_height(
            parameters, {.world_xz = point, .footprint_m = 256.0F});
        if (index > 0) {
            full_variation += std::abs(full - previous_full);
            coarse_variation += std::abs(coarse - previous_coarse);
        }
        previous_full = full;
        previous_coarse = coarse;
    }
    require(full_variation > coarse_variation,
            "terrain footprint should suppress unresolved local variation");
}

void test_clean_source_publishes_no_weathering_delta() {
    const auto parameters = cubey::projects::terrain::resolve_terrain_source_parameters({
        .seed = 42U,
        .preset = TerrainPreset::Upland,
        .weathering = TerrainWeatheringMode::Off,
    });
    const auto sample =
        cubey::projects::terrain::sample_terrain(parameters, {.world_xz = {-2048.0F, 4096.0F}});
    require_near(sample.base_height_m, sample.height_m, 0.0F,
                 "clean terrain should preserve base height");
    require_near(sample.weathering_delta_m, 0.0F, 0.0F,
                 "clean terrain should publish zero weathering delta");
}

} // namespace

int main() {
    try {
        test_names_and_validation();
        test_source_is_deterministic_and_uses_full_seed();
        test_presets_have_ordered_relief();
        test_footprint_filters_unresolved_detail();
        test_clean_source_publishes_no_weathering_delta();
        std::cout << "terrain_source_tests: ok\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "terrain_source_tests: " << error.what() << '\n';
        return 1;
    }
}
