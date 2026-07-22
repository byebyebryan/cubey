#include "terrain_product_adapter.h"

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

class AnalyticSource final : public cubey::asset::TerrainHeightSource {
  public:
    [[nodiscard]] cubey::asset::TerrainHeightSourceMetadata
    metadata() const noexcept override {
        return {
            .id = "terrain-product-adapter-test",
            .seed = 9012U,
            .base_height_m = 600.0F,
            .relief_scale_m = 1'600.0F,
            .gradient_step_m = 16.0F,
        };
    }

    [[nodiscard]] float sample_height(const cubey::asset::TerrainQuery& query) const override {
        return 1'200.0F + 520.0F * std::sin(query.world_xz.x / 5'200.0F) +
               340.0F * std::cos(query.world_xz.y / 3'800.0F) +
               120.0F * std::sin((query.world_xz.x + query.world_xz.y) / 1'300.0F);
    }
};

[[nodiscard]] cubey::terrain::TerrainBackdropProductRequest product_request() {
    return {
        .source_focus_xz = {4'000.0F, -8'000.0F},
        .density = cubey::terrain::TerrainBackdropMeshDensity::Low,
        .vertical_offset_m = -1'200.0F,
    };
}

void test_surface_models_preserve_geometry_and_change_only_surface_channels() {
    using namespace cubey::projects::terrain;
    const AnalyticSource source;
    const cubey::terrain::TerrainBackdropProduct mineral = make_project_terrain_backdrop_product(
        product_request(), source, TerrainSurfaceModel::MineralControl);
    const cubey::terrain::TerrainBackdropProduct landform = make_project_terrain_backdrop_product(
        product_request(), source, TerrainSurfaceModel::LandformTransition);

    require(mineral.diagnostics.geometry_hash == landform.diagnostics.geometry_hash,
            "surface adapters must preserve identical terrain geometry");
    require(mineral.diagnostics.content_hash != landform.diagnostics.content_hash,
            "surface adapters should produce distinct semantic products");
    require(mineral.diagnostics.source_sample_count == landform.diagnostics.source_sample_count &&
                mineral.diagnostics.render_triangle_count ==
                    landform.diagnostics.render_triangle_count,
            "surface adapters must preserve source and topology budgets");
    require(mineral.diagnostics.mean_vegetation == 0.0F &&
                landform.diagnostics.mean_vegetation > 0.0F,
            "landform transition should populate vegetation without changing geometry");
}

void test_climate_surface_requires_a_bound_climate_source() {
    using namespace cubey::projects::terrain;
    bool rejected = false;
    try {
        static_cast<void>(make_project_terrain_backdrop_product(
            product_request(), AnalyticSource{}, TerrainSurfaceModel::ClimateTransition));
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    require(rejected, "climate transition should reject an unbound climate source");
}

} // namespace

int main() {
    try {
        test_surface_models_preserve_geometry_and_change_only_surface_channels();
        test_climate_surface_requires_a_bound_climate_source();
        std::cout << "terrain_product_adapter_tests: ok\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "terrain_product_adapter_tests: " << error.what() << '\n';
        return 1;
    }
}
