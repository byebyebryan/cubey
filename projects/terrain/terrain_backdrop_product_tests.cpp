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

[[nodiscard]] cubey::projects::terrain::TerrainBackdropProductRequest product_request() {
    return {
        .source_focus_xz = {4'000.0F, -8'000.0F},
        .density = cubey::projects::terrain::TerrainBackdropMeshDensity::Low,
        .vertical_offset_m = -1'200.0F,
    };
}

[[nodiscard]] cubey::projects::terrain::TerrainSourceParameters
source_for_seed(std::uint64_t seed) {
    return cubey::projects::terrain::resolve_terrain_source_parameters({
        .seed = seed,
        .preset = cubey::projects::terrain::TerrainPreset::Mountain,
        .version = cubey::projects::terrain::TerrainSourceVersion::V2_1,
        .weathering = cubey::projects::terrain::TerrainWeatheringMode::Off,
    });
}

void test_density_profiles_publish_the_product_budget() {
    using namespace cubey::projects::terrain;
    const auto low = terrain_backdrop_density_profile(TerrainBackdropMeshDensity::Low);
    const auto medium = terrain_backdrop_density_profile(TerrainBackdropMeshDensity::Medium);
    const auto high = terrain_backdrop_density_profile(TerrainBackdropMeshDensity::High);
    require(low.angular_intervals == 1'024U && low.center_radial_intervals == 16U &&
                low.hidden_radial_intervals == 32U &&
                low.visible_radial_intervals == 256U && low.sector_count == 32U,
            "low backdrop density should remain a bounded diagnostic product");
    require(medium.angular_intervals == 2'048U && medium.center_radial_intervals == 24U &&
                medium.hidden_radial_intervals == 48U &&
                medium.visible_radial_intervals == 512U && medium.sector_count == 32U,
            "medium backdrop density should remain a review product");
    require(high.angular_intervals == 3'072U && high.center_radial_intervals == 32U &&
                high.hidden_radial_intervals == 64U &&
                high.visible_radial_intervals == 768U && high.sector_count == 48U,
            "high backdrop density should publish the v1 production budget");
    require(terrain_backdrop_mesh_density_from_name("") == TerrainBackdropMeshDensity::High &&
                terrain_backdrop_mesh_density_from_name("medium") ==
                    TerrainBackdropMeshDensity::Medium,
            "backdrop density names should preserve the high default");
}

void test_product_is_deterministic_connected_and_outside_the_stage() {
    using namespace cubey::projects::terrain;
    const TerrainSourceParameters source = source_for_seed(9012U);
    const TerrainBackdropProduct first = make_terrain_backdrop_product(product_request(), source);
    const TerrainBackdropProduct second = make_terrain_backdrop_product(product_request(), source);
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
    require(first.diagnostics.render_triangle_count == first.diagnostics.visible_triangle_count,
            "low backdrop product should retain every generated triangle for rendering");
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
    const TerrainBackdropProduct first =
        make_terrain_backdrop_product(product_request(), source_for_seed(0U), 0U);
    const TerrainBackdropProduct second =
        make_terrain_backdrop_product(product_request(), source_for_seed(12345U), 12345U);
    require(first.diagnostics.content_hash != second.diagnostics.content_hash,
            "cached backdrop hash should track source content");
}

void test_parameter_adapter_preserves_the_product() {
    using namespace cubey::projects::terrain;
    const TerrainSourceParameters parameters = source_for_seed(9012U);
    const ParameterTerrainHeightSource adapter(parameters, 9012U);
    const TerrainBackdropProduct direct =
        make_terrain_backdrop_product(product_request(), parameters, 9012U);
    const TerrainBackdropProduct generic =
        make_terrain_backdrop_product(product_request(), adapter);
    require(direct.diagnostics.content_hash == generic.diagnostics.content_hash,
            "parameter adapter should preserve cached backdrop content");
    require(generic.source.seed == 9012U && generic.source.id == "terrain-parameters",
            "cached backdrop should retain a source metadata snapshot");
}

void test_full_render_stride_retains_the_baked_topology() {
    using namespace cubey::projects::terrain;
    TerrainBackdropProductRequest request = product_request();
    request.density = TerrainBackdropMeshDensity::Medium;
    request.render_stride = 1U;
    const TerrainBackdropProduct product =
        make_terrain_backdrop_product(request, source_for_seed(9012U), 9012U);
    require(product.diagnostics.render_triangle_count == product.diagnostics.visible_triangle_count,
            "explicit stride one should retain the full baked topology for source studies");
}

void test_continuous_product_fills_the_center_and_preserves_the_outer_seam() {
    using namespace cubey::projects::terrain;
    TerrainBackdropProductRequest request = product_request();
    request.center_mode = TerrainBackdropCenterMode::Continuous;
    const TerrainBackdropProduct product =
        make_terrain_backdrop_product(request, source_for_seed(9012U), 9012U);
    const TerrainBackdropDensityProfile density = product.diagnostics.density;
    require(product.center.has_value(),
            "continuous backdrop should publish one center mesh");
    require(product.diagnostics.source_sample_count ==
                static_cast<std::uint64_t>(density.angular_intervals) *
                    (density.center_radial_intervals + density.hidden_radial_intervals +
                     density.visible_radial_intervals + 1U),
            "continuous backdrop should sample center, transition, and outer rows once");
    const auto& center = product.center.value();
    require(std::abs(center.vertices.front().position[0]) < 0.001F &&
                std::abs(center.vertices.front().position[2]) < 0.001F,
            "continuous backdrop center fan should begin at the focus");
    require(product.diagnostics.center_vertex_count == center.vertices.size() &&
                product.diagnostics.center_triangle_count == center.indices.size() / 3U &&
                product.diagnostics.center_render_triangle_count == center.triangle_count(),
            "continuous backdrop should report its center topology exactly");
    require(product.diagnostics.maximum_sector_boundary_delta_m == 0.0F,
            "continuous center and outer sectors should share exact boundary samples");
    require(product.diagnostics.render_triangle_count >
                product.sectors.size() * product.sectors.front().triangle_count(),
            "continuous backdrop draw budget should include the center mesh");
}

} // namespace

int main() {
    try {
        test_density_profiles_publish_the_product_budget();
        test_product_is_deterministic_connected_and_outside_the_stage();
        test_product_hash_changes_with_the_source_seed();
        test_parameter_adapter_preserves_the_product();
        test_full_render_stride_retains_the_baked_topology();
        test_continuous_product_fills_the_center_and_preserves_the_outer_seam();
        std::cout << "terrain_backdrop_product_tests: ok\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "terrain_backdrop_product_tests: " << error.what() << '\n';
        return 1;
    }
}
