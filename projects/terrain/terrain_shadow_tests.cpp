#include "terrain_shadow.h"

#include <glm/geometric.hpp>

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

[[nodiscard]] bool finite(const cubey::math::Mat4& matrix) {
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            if (!std::isfinite(matrix[column][row])) {
                return false;
            }
        }
    }
    return true;
}

void test_projection_covers_product_bounds() {
    constexpr cubey::projects::terrain::TerrainShadowProductBounds bounds{
        .outer_radius_m = 16'384.0F,
        .minimum_height_m = -380.0F,
        .maximum_height_m = 3'120.0F,
    };
    const auto projection = cubey::projects::terrain::terrain_shadow_projection(
        bounds, glm::normalize(cubey::math::Vec3{-0.4F, 0.7F, 0.55F}));
    require(finite(projection.light_view_projection),
            "terrain shadow projection should be finite");
    require(projection.light_above_horizon && projection.depth_span_m > 0.0F &&
                projection.texel_world_size_m > 0.0F,
            "terrain shadow projection should publish usable dimensions");

    for (int elevation = 0; elevation < 2; ++elevation) {
        const float y = elevation == 0 ? bounds.minimum_height_m : bounds.maximum_height_m;
        for (int sample = 0; sample < 32; ++sample) {
            const float angle = static_cast<float>(sample) * 2.0F * std::numbers::pi_v<float> /
                                32.0F;
            const cubey::math::Vec4 clip = projection.light_view_projection *
                                           cubey::math::Vec4{
                                               std::cos(angle) * bounds.outer_radius_m, y,
                                               std::sin(angle) * bounds.outer_radius_m, 1.0F};
            const cubey::math::Vec3 ndc = cubey::math::Vec3{clip} / clip.w;
            require(std::abs(ndc.x) <= 1.001F && std::abs(ndc.y) <= 1.001F &&
                        ndc.z >= -0.001F && ndc.z <= 1.001F,
                    "terrain shadow projection should contain the complete product cylinder");
        }
    }
}

void test_cache_invalidation_contract() {
    using namespace cubey::projects::terrain;
    TerrainShadowCacheState cache;
    const cubey::math::Vec3 light =
        glm::normalize(cubey::math::Vec3{-0.4F, 0.7F, 0.55F});
    require(terrain_shadow_update_required(cache, true, 11U, light),
            "first enabled terrain shadow use should update");

    const TerrainShadowProjection projection = terrain_shadow_projection(
        {.outer_radius_m = 16'384.0F,
         .minimum_height_m = -380.0F,
         .maximum_height_m = 3'120.0F},
        light);
    update_terrain_shadow_cache(cache, 11U, projection);
    require(cache.valid && cache.update_count == 1U &&
                !terrain_shadow_update_required(cache, true, 11U, light),
            "rendered terrain shadow should remain cached");

    const cubey::math::Vec3 small_change = glm::normalize(cubey::math::Vec3{
        light.x + 0.001F, light.y, light.z});
    require(!terrain_shadow_update_required(cache, true, 11U, small_change),
            "sub-threshold light movement should retain the cached map");

    const cubey::math::Vec3 large_change = glm::normalize(cubey::math::Vec3{
        light.x + 0.02F, light.y, light.z});
    require(terrain_shadow_update_required(cache, true, 11U, large_change),
            "supra-threshold light movement should refresh the cached map");
    require(terrain_shadow_update_required(cache, true, 12U, light),
            "placement product changes should refresh the cached map");

    invalidate_terrain_shadow_cache(cache);
    require(terrain_shadow_update_required(cache, true, 11U, light),
            "resource invalidation should refresh the cached map");
}

void test_disabled_and_below_horizon_shadows_do_not_update() {
    using namespace cubey::projects::terrain;
    TerrainShadowCacheState cache;
    require(!terrain_shadow_update_required(cache, false, 1U, {0.0F, 1.0F, 0.0F}),
            "disabled shadows should not update");
    require(!terrain_shadow_update_required(cache, true, 1U, {0.0F, -1.0F, 0.0F}) &&
                !terrain_shadow_light_above_horizon({0.0F, -1.0F, 0.0F}),
            "below-horizon lights should suspend terrain shadow updates");
    require(terrain_shadow_light_above_horizon({0.0F, 1.0F, 0.0F}),
            "above-horizon lights should enable terrain shadow work");
}

} // namespace

int main() {
    try {
        test_projection_covers_product_bounds();
        test_cache_invalidation_contract();
        test_disabled_and_below_horizon_shadows_do_not_update();
        std::cout << "terrain shadow tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "terrain shadow tests failed: " << error.what() << '\n';
        return 1;
    }
}
