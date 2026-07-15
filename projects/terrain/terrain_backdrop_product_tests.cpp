#include "terrain_backdrop_product.h"

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

[[nodiscard]] cubey::projects::terrain::TerrainBackdropProductRequest
request_for_seed(std::uint64_t seed) {
    return {
        .source = cubey::projects::terrain::resolve_terrain_source_parameters({
            .seed = seed,
            .preset = cubey::projects::terrain::TerrainPreset::Mountain,
            .version = cubey::projects::terrain::TerrainSourceVersion::V2_1,
            .weathering = cubey::projects::terrain::TerrainWeatheringMode::Off,
        }),
        .source_focus_xz = {4'000.0F, -8'000.0F},
        .density = cubey::projects::terrain::TerrainBackdropMeshDensity::Low,
        .vertical_offset_m = -1'200.0F,
    };
}

void test_density_profiles_publish_the_product_budget() {
    using namespace cubey::projects::terrain;
    const auto low = terrain_backdrop_density_profile(TerrainBackdropMeshDensity::Low);
    const auto medium = terrain_backdrop_density_profile(TerrainBackdropMeshDensity::Medium);
    const auto high = terrain_backdrop_density_profile(TerrainBackdropMeshDensity::High);
    require(low.angular_intervals == 1'024U && low.hidden_radial_intervals == 32U &&
                low.visible_radial_intervals == 256U && low.sector_count == 32U,
            "low backdrop density should remain a bounded diagnostic product");
    require(medium.angular_intervals == 2'048U && medium.hidden_radial_intervals == 48U &&
                medium.visible_radial_intervals == 512U && medium.sector_count == 32U,
            "medium backdrop density should remain a review product");
    require(high.angular_intervals == 3'072U && high.hidden_radial_intervals == 64U &&
                high.visible_radial_intervals == 768U && high.sector_count == 48U,
            "high backdrop density should publish the v1 production budget");
    require(terrain_backdrop_mesh_density_from_name("") == TerrainBackdropMeshDensity::High &&
                terrain_backdrop_mesh_density_from_name("medium") ==
                    TerrainBackdropMeshDensity::Medium,
            "backdrop density names should preserve the high default");
}

void test_product_is_deterministic_connected_and_outside_the_stage() {
    using namespace cubey::projects::terrain;
    const TerrainBackdropProduct first = make_terrain_backdrop_product(request_for_seed(9012U));
    const TerrainBackdropProduct second = make_terrain_backdrop_product(request_for_seed(9012U));
    const TerrainBackdropDensityProfile density = first.diagnostics.density;
    require(first.diagnostics.content_hash == second.diagnostics.content_hash,
            "cached backdrop product should be deterministic");
    require(first.diagnostics.maximum_sector_boundary_delta_m == 0.0F,
            "cached backdrop sectors should share exact boundary samples");
    require(first.sectors.size() == density.sector_count,
            "cached backdrop should produce one mesh per culling sector");
    require(first.diagnostics.source_sample_count ==
                static_cast<std::uint64_t>(density.angular_intervals) *
                    (density.hidden_radial_intervals + density.visible_radial_intervals + 1U),
            "cached backdrop should sample the global polar field exactly once");
    require(first.diagnostics.visible_triangle_count ==
                static_cast<std::uint64_t>(density.angular_intervals) *
                    density.visible_radial_intervals * 2U,
            "cached backdrop should publish its exact triangle budget");
    for (const TerrainBackdropSectorMesh& sector : first.sectors) {
        for (const auto& vertex : sector.vertices) {
            const float radius = std::sqrt(vertex.position[0] * vertex.position[0] +
                                           vertex.position[2] * vertex.position[2]);
            require(radius >= 3'199.99F,
                    "visible cached backdrop geometry should remain outside the stage floor");
            require(vertex.normal[1] >= -0.001F,
                    "cached backdrop normals should retain an upward orientation");
            require(vertex.color[0] >= 0.0F && vertex.color[0] <= 1.0F && vertex.color[1] >= 0.0F &&
                        vertex.color[1] <= 1.0F && vertex.color[2] >= 0.65F &&
                        vertex.color[2] <= 1.0F,
                    "cached material channels should remain bounded");
        }
    }
}

void test_product_hash_changes_with_the_source_seed() {
    using namespace cubey::projects::terrain;
    const TerrainBackdropProduct first = make_terrain_backdrop_product(request_for_seed(0U));
    const TerrainBackdropProduct second = make_terrain_backdrop_product(request_for_seed(12345U));
    require(first.diagnostics.content_hash != second.diagnostics.content_hash,
            "cached backdrop hash should track source content");
}

} // namespace

int main() {
    try {
        test_density_profiles_publish_the_product_budget();
        test_product_is_deterministic_connected_and_outside_the_stage();
        test_product_hash_changes_with_the_source_seed();
        std::cout << "terrain_backdrop_product_tests: ok\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "terrain_backdrop_product_tests: " << error.what() << '\n';
        return 1;
    }
}
