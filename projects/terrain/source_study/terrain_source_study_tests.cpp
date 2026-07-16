#include "terrain_source_study.h"

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

void test_registry_is_complete_and_strict() {
    using namespace cubey::projects::terrain;
    const auto recipes = terrain_source_study_recipes();
    require(recipes.size() == 8U, "source study should publish the eight-recipe matrix");
    for (const TerrainSourceStudyRecipeInfo& info : recipes) {
        require(!info.id.empty() && !info.operator_family.empty() && !info.reference.empty(),
                "source study recipes should retain review metadata");
        require(terrain_source_study_recipe_from_name(info.id) == info.recipe &&
                    terrain_source_study_recipe_name(info.recipe) == info.id,
                "source study recipe names should round trip");
    }
    bool rejected = false;
    try {
        static_cast<void>(terrain_source_study_recipe_from_name("unknown-source"));
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    require(rejected, "source study should reject unknown recipes");
}

void test_calibration_and_samples_are_deterministic() {
    using namespace cubey::projects::terrain;
    constexpr std::array<TerrainQuery, 4> queries{{
        {.world_xz = {0.0F, 0.0F}, .footprint_m = 0.0F},
        {.world_xz = {4'000.0F, -8'000.0F}, .footprint_m = 16.0F},
        {.world_xz = {-12'000.0F, 6'000.0F}, .footprint_m = 64.0F},
        {.world_xz = {16'000.0F, 16'000.0F}, .footprint_m = 128.0F},
    }};
    for (const TerrainSourceStudyRecipeInfo& info : terrain_source_study_recipes()) {
        const TerrainSourceStudyCalibration first = terrain_source_study_calibration(info.recipe);
        const TerrainSourceStudyCalibration second = terrain_source_study_calibration(info.recipe);
        require(first.raw_p05 == second.raw_p05 && first.raw_p95 == second.raw_p95 &&
                    first.scale_m == second.scale_m && first.sample_count == 198'147U,
                "source study calibration should be deterministic across the fixed matrix");
        require(first.raw_p95 > first.raw_p05 && first.scale_m > 0.0F,
                "source study calibration should retain measurable relief");
        const TerrainSourceStudySource source(info.recipe, 9012U, first);
        require(source.metadata().id == info.id && source.metadata().relief_scale_m == 3'500.0F,
                "source study metadata should publish calibrated physical units");
        for (const TerrainQuery& query : queries) {
            const float a = source.sample_height(query);
            const float b = source.sample_height(query);
            require(std::isfinite(a) && a >= 0.0F && a == b,
                    "source study samples should be finite, non-negative, and deterministic");
        }
    }
}

void test_seeds_and_footprints_change_the_field() {
    using namespace cubey::projects::terrain;
    for (const TerrainSourceStudyRecipeInfo& info : terrain_source_study_recipes()) {
        const TerrainSourceStudyCalibration calibration =
            terrain_source_study_calibration(info.recipe);
        const TerrainSourceStudySource first(info.recipe, 0U, calibration);
        const TerrainSourceStudySource second(info.recipe, 12345U, calibration);
        float seed_difference = 0.0F;
        float footprint_difference = 0.0F;
        for (int z = -4; z <= 4; ++z) {
            for (int x = -4; x <= 4; ++x) {
                const cubey::math::Vec2 position{static_cast<float>(x) * 3'500.0F + 137.0F,
                                                 static_cast<float>(z) * 3'500.0F - 281.0F};
                const float fine = first.sample_height({.world_xz = position, .footprint_m = 0.0F});
                const float coarse =
                    first.sample_height({.world_xz = position, .footprint_m = 256.0F});
                seed_difference += std::abs(
                    fine - second.sample_height({.world_xz = position, .footprint_m = 0.0F}));
                footprint_difference += std::abs(fine - coarse);
            }
        }
        require(seed_difference > 1.0F, "source study seeds should produce distinct fields");
        if (footprint_difference <= 0.01F) {
            throw std::runtime_error("source study footprint filtering produced no change for " +
                                     std::string(info.id));
        }
    }
}

void test_neighbor_samples_remain_continuous() {
    using namespace cubey::projects::terrain;
    for (const TerrainSourceStudyRecipeInfo& info : terrain_source_study_recipes()) {
        const TerrainSourceStudyCalibration calibration =
            terrain_source_study_calibration(info.recipe);
        const TerrainSourceStudySource source(info.recipe, 9012U, calibration);
        for (int z = -4; z <= 4; ++z) {
            for (int x = -4; x <= 4; ++x) {
                const cubey::math::Vec2 position{static_cast<float>(x) * 512.0F,
                                                 static_cast<float>(z) * 512.0F};
                const float center =
                    source.sample_height({.world_xz = position, .footprint_m = 32.0F});
                const float neighbor = source.sample_height(
                    {.world_xz = position + cubey::math::Vec2{32.0F, 0.0F}, .footprint_m = 32.0F});
                require(std::isfinite(center) && std::isfinite(neighbor) &&
                            std::abs(center - neighbor) < 10'000.0F,
                        "source study should not introduce discontinuous height jumps");
            }
        }
    }
}

} // namespace

int main() {
    try {
        test_registry_is_complete_and_strict();
        test_calibration_and_samples_are_deterministic();
        test_seeds_and_footprints_change_the_field();
        test_neighbor_samples_remain_continuous();
        std::cout << "terrain_source_study_tests: ok\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "terrain_source_study_tests: " << error.what() << '\n';
        return 1;
    }
}
