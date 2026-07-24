#include <cubey/terrain/terrain_backdrop_preparation.h>

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

#ifndef CUBEY_TERRAIN_BACKDROP_SMOKE_ASSET
#error "CUBEY_TERRAIN_BACKDROP_SMOKE_ASSET must be defined by the terrain test target"
#endif

namespace {

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

template <typename Function>
void require_throws_containing(Function&& function, std::string_view expected,
                               std::string_view message) {
    try {
        function();
    } catch (const std::exception& error) {
        require(std::string_view(error.what()).find(expected) != std::string_view::npos, message);
        return;
    }
    throw std::runtime_error(std::string(message));
}

void test_prepares_selected_raster_product_and_foreground_offset() {
    const cubey::terrain::PreparedTerrainBackdropProduct prepared =
        cubey::terrain::prepare_raster_terrain_backdrop_product({
            .heightfield_path = CUBEY_TERRAIN_BACKDROP_SMOKE_ASSET,
            .render_stride = 2U,
        });
    require(prepared.placement.stage.contract_satisfied,
            "prepared terrain should retain the selected stage contract");
    require(prepared.product.source.id == "cubey-backdrop-smoke-directional-rise",
            "prepared terrain should retain raster source identity");
    require(prepared.product.request.render_stride == 2U,
            "prepared terrain should apply the requested render stride");
    require(prepared.baked_foreground_height_m ==
                prepared.placement.stage.target_height_m -
                    prepared.placement.stage.source_center_height_m,
            "prepared terrain should publish the baked foreground offset");
}

void test_rejects_missing_raster_path_before_loading() {
    const std::filesystem::path missing =
        std::filesystem::path(CUBEY_TERRAIN_BACKDROP_SMOKE_ASSET) / "missing";
    require_throws_containing(
        [&missing] {
            static_cast<void>(cubey::terrain::prepare_raster_terrain_backdrop_product({
                .heightfield_path = missing,
            }));
        },
        "terrain backdrop heightfield does not exist",
        "prepared terrain should identify a missing raster source");
}

} // namespace

int main() {
    try {
        test_prepares_selected_raster_product_and_foreground_offset();
        test_rejects_missing_raster_path_before_loading();
        std::cout << "terrain_backdrop_preparation_tests: ok\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "terrain_backdrop_preparation_tests: " << error.what() << '\n';
        return 1;
    }
}
