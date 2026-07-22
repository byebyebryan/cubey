#include "terrain_backdrop_product.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

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

class AnalyticSource final : public cubey::projects::terrain::TerrainHeightSource {
  public:
    explicit AnalyticSource(std::uint64_t seed) : seed_(seed) {}

    void rename(std::string id) {
        id_ = std::move(id);
    }

    [[nodiscard]] cubey::projects::terrain::TerrainHeightSourceMetadata
    metadata() const noexcept override {
        return {
            .id = id_,
            .seed = seed_,
            .base_height_m = 600.0F,
            .relief_scale_m = 1'600.0F,
            .gradient_step_m = 16.0F,
        };
    }

    [[nodiscard]] float
    sample_height(const cubey::projects::terrain::TerrainQuery& query) const override {
        const float phase = static_cast<float>(seed_ % 10'000U) * 0.001F;
        return 1'200.0F + 520.0F * std::sin(query.world_xz.x / 5'200.0F + phase) +
               340.0F * std::cos(query.world_xz.y / 3'800.0F - phase * 0.7F) +
               120.0F * std::sin((query.world_xz.x + query.world_xz.y) / 1'300.0F);
    }

  private:
    std::string id_ = "analytic-product-test";
    std::uint64_t seed_ = 0U;
};

[[nodiscard]] AnalyticSource source_for_seed(std::uint64_t seed) {
    return AnalyticSource(seed);
}

using BoundaryEdge = std::pair<std::uint32_t, std::uint32_t>;

[[nodiscard]] std::set<BoundaryEdge>
rendered_boundary_edges(const cubey::projects::terrain::TerrainBackdropSectorMesh& mesh,
                        std::uint32_t first_vertex, std::uint32_t vertex_count,
                        std::uint32_t angular_offset = 0U) {
    std::set<BoundaryEdge> edges;
    const std::uint32_t last_vertex = first_vertex + vertex_count;
    for (std::size_t triangle = 0U; triangle < mesh.indices.size(); triangle += 3U) {
        const std::uint32_t indices[] = {
            mesh.indices[triangle],
            mesh.indices[triangle + 1U],
            mesh.indices[triangle + 2U],
        };
        for (std::size_t edge = 0U; edge < 3U; ++edge) {
            const std::uint32_t first = indices[edge];
            const std::uint32_t second = indices[(edge + 1U) % 3U];
            if (first < first_vertex || first >= last_vertex || second < first_vertex ||
                second >= last_vertex) {
                continue;
            }
            const std::uint32_t local_first = first - first_vertex + angular_offset;
            const std::uint32_t local_second = second - first_vertex + angular_offset;
            edges.emplace(std::min(local_first, local_second), std::max(local_first, local_second));
        }
    }
    return edges;
}

void test_density_profiles_publish_the_product_budget() {
    using namespace cubey::projects::terrain;
    const auto low = terrain_backdrop_density_profile(TerrainBackdropMeshDensity::Low);
    const auto medium = terrain_backdrop_density_profile(TerrainBackdropMeshDensity::Medium);
    const auto high = terrain_backdrop_density_profile(TerrainBackdropMeshDensity::High);
    require(low.angular_intervals == 1'024U && low.center_radial_intervals == 16U &&
                low.hidden_radial_intervals == 32U && low.visible_radial_intervals == 256U &&
                low.sector_count == 32U,
            "low backdrop density should remain a bounded diagnostic product");
    require(medium.angular_intervals == 2'048U && medium.center_radial_intervals == 24U &&
                medium.hidden_radial_intervals == 48U && medium.visible_radial_intervals == 512U &&
                medium.sector_count == 32U,
            "medium backdrop density should remain a review product");
    require(high.angular_intervals == 3'072U && high.center_radial_intervals == 32U &&
                high.hidden_radial_intervals == 64U && high.visible_radial_intervals == 768U &&
                high.sector_count == 48U,
            "high backdrop density should publish the v1 production budget");
    require(terrain_backdrop_mesh_density_from_name("") == TerrainBackdropMeshDensity::High &&
                terrain_backdrop_mesh_density_from_name("medium") ==
                    TerrainBackdropMeshDensity::Medium,
            "backdrop density names should preserve the high default");
}

void test_v1_product_request_publishes_the_accepted_backdrop_contract() {
    const cubey::render::TerrainBackdropStagePlan stage{
        .source_focus_xz = {1'250.0F, -2'500.0F},
        .terrain_vertical_offset_m = -720.0F,
        .stage_radius_m = 450.0F,
    };
    const cubey::render::TerrainBackdropProductRequest request =
        cubey::render::terrain_backdrop_v1_product_request(stage, 2U);

    require(request.source_focus_xz == stage.source_focus_xz &&
                request.density == cubey::render::TerrainBackdropMeshDensity::High &&
                request.center_mode == cubey::render::TerrainBackdropCenterMode::Continuous &&
                request.center_sampling ==
                    cubey::render::TerrainBackdropCenterSampling::SeamMatched,
            "v1 request should publish the accepted source and topology contract");
    require(request.render_stride == 2U && request.consumer_radius_m == stage.stage_radius_m &&
                request.visible_inner_radius_m == 3'200.0F &&
                request.outer_radius_m == 16'384.0F && request.vertical_scale == 1.0F &&
                request.vertical_offset_m == stage.terrain_vertical_offset_m,
            "v1 request should publish the accepted sampling envelope");
}

void test_product_is_deterministic_connected_and_outside_the_stage() {
    using namespace cubey::projects::terrain;
    const AnalyticSource source = source_for_seed(9012U);
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
            require(vertex.uv[0] >= 0.0F && vertex.uv[0] <= 1.0F && vertex.uv[1] >= 0.0F &&
                        vertex.uv[1] <= 1.0F,
                    "cached surface channels should remain bounded");
        }
    }
}

void test_surface_models_preserve_geometry_and_change_only_surface_channels() {
    using namespace cubey::projects::terrain;
    TerrainBackdropProductRequest mineral_request = product_request();
    TerrainBackdropProductRequest landform_request = mineral_request;

    const AnalyticSource source = source_for_seed(9012U);
    const TerrainBackdropProduct mineral = make_project_terrain_backdrop_product(
        mineral_request, source, TerrainSurfaceModel::MineralControl);
    const TerrainBackdropProduct landform = make_project_terrain_backdrop_product(
        landform_request, source, TerrainSurfaceModel::LandformTransition);

    require(mineral.diagnostics.geometry_hash == landform.diagnostics.geometry_hash,
            "surface studies must preserve identical terrain geometry");
    require(mineral.diagnostics.content_hash != landform.diagnostics.content_hash,
            "surface studies should produce distinct semantic products");
    require(mineral.diagnostics.source_sample_count == landform.diagnostics.source_sample_count &&
                mineral.diagnostics.render_triangle_count ==
                    landform.diagnostics.render_triangle_count,
            "surface studies must preserve source and topology budgets");
    require(mineral.diagnostics.mean_vegetation == 0.0F &&
                landform.diagnostics.mean_vegetation > 0.0F,
            "landform transition should populate vegetation without changing geometry");
}

void test_climate_surface_requires_a_bound_climate_source() {
    using namespace cubey::projects::terrain;
    TerrainBackdropProductRequest request = product_request();
    bool rejected = false;
    try {
        static_cast<void>(make_project_terrain_backdrop_product(
            request, source_for_seed(9012U), TerrainSurfaceModel::ClimateTransition));
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    require(rejected, "climate transition should reject an unbound climate source");
}

void test_product_hash_changes_with_the_source_seed() {
    using namespace cubey::projects::terrain;
    const TerrainBackdropProduct first =
        make_terrain_backdrop_product(product_request(), source_for_seed(0U));
    const TerrainBackdropProduct second =
        make_terrain_backdrop_product(product_request(), source_for_seed(12345U));
    require(first.diagnostics.content_hash != second.diagnostics.content_hash,
            "cached backdrop hash should track source content");
}

void test_product_retains_source_metadata() {
    using namespace cubey::projects::terrain;
    AnalyticSource source = source_for_seed(9012U);
    const TerrainBackdropProduct product = make_terrain_backdrop_product(product_request(), source);
    source.rename("mutated-source-id");
    require(product.source.seed == 9012U && product.source.id == "analytic-product-test",
            "cached backdrop should own its source metadata snapshot");
}

void test_full_render_stride_retains_the_baked_topology() {
    using namespace cubey::projects::terrain;
    TerrainBackdropProductRequest request = product_request();
    request.density = TerrainBackdropMeshDensity::Medium;
    request.render_stride = 1U;
    const TerrainBackdropProduct product =
        make_terrain_backdrop_product(request, source_for_seed(9012U));
    require(product.diagnostics.render_triangle_count == product.diagnostics.visible_triangle_count,
            "explicit stride one should retain the full baked topology for source studies");
}

void test_continuous_product_fills_the_center_and_preserves_the_outer_seam() {
    using namespace cubey::projects::terrain;
    TerrainBackdropProductRequest request = product_request();
    request.center_mode = TerrainBackdropCenterMode::Continuous;
    const TerrainBackdropProduct product =
        make_terrain_backdrop_product(request, source_for_seed(9012U));
    const TerrainBackdropDensityProfile density = product.diagnostics.density;
    require(product.center.has_value(), "continuous backdrop should publish one center mesh");
    require(product.diagnostics.source_sample_count ==
                static_cast<std::uint64_t>(density.angular_intervals) *
                    (density.center_radial_intervals + density.hidden_radial_intervals +
                     density.visible_radial_intervals + 1U),
            "continuous backdrop should sample center, transition, and outer rows once");
    const auto& center = product.center.value();
    require(std::abs(center.vertices.front().position[0]) < 0.001F &&
                std::abs(center.vertices.front().position[2]) < 0.001F,
            "continuous backdrop center fan should begin at the focus");
    require(std::abs(center.vertices.front().normal[0]) +
                    std::abs(center.vertices.front().normal[2]) >
                0.01F &&
                center.vertices.front().normal[1] < 0.9999F,
            "continuous backdrop center should retain the source gradient");
    require(product.diagnostics.center_vertex_count == center.vertices.size() &&
                product.diagnostics.center_index_count ==
                    product.diagnostics.center_triangle_count * 3U &&
                product.diagnostics.center_render_triangle_count == center.triangle_count(),
            "continuous backdrop should report its center topology exactly");
    require(product.diagnostics.maximum_sector_boundary_delta_m == 0.0F,
            "continuous center and outer sectors should share exact boundary samples");
    require(product.diagnostics.render_triangle_count >
                product.sectors.size() * product.sectors.front().triangle_count(),
            "continuous backdrop draw budget should include the center mesh");
}

void test_uniform_center_sampling_redistributes_the_existing_budget() {
    using namespace cubey::projects::terrain;
    TerrainBackdropProductRequest split_request = product_request();
    split_request.center_mode = TerrainBackdropCenterMode::Continuous;
    const TerrainBackdropProduct split =
        make_terrain_backdrop_product(split_request, source_for_seed(9012U));

    TerrainBackdropProductRequest uniform_request = split_request;
    uniform_request.center_sampling = TerrainBackdropCenterSampling::Uniform;
    const TerrainBackdropProduct uniform =
        make_terrain_backdrop_product(uniform_request, source_for_seed(9012U));
    const TerrainBackdropDensityProfile density = uniform.diagnostics.density;
    const std::uint32_t center_intervals =
        density.center_radial_intervals + density.hidden_radial_intervals;
    const float expected_spacing =
        uniform_request.visible_inner_radius_m / static_cast<float>(center_intervals);
    const auto& center = uniform.center.value();
    const std::uint32_t angular_vertex_count = density.angular_intervals + 1U;
    float previous_radius = 0.0F;
    for (std::uint32_t ring = 1U; ring <= center_intervals; ++ring) {
        const auto& vertex = center.vertices[1U + (ring - 1U) * angular_vertex_count];
        const float radius = std::sqrt(vertex.position[0] * vertex.position[0] +
                                       vertex.position[2] * vertex.position[2]);
        require(std::abs(radius - expected_spacing * static_cast<float>(ring)) < 0.01F,
                "uniform center rings should use one source-scale interval");
        require(radius > previous_radius, "uniform center rings should remain strictly monotonic");
        previous_radius = radius;
    }
    require(std::abs(previous_radius - uniform_request.visible_inner_radius_m) < 0.01F,
            "uniform center should terminate at the visible outer seam");
    require(uniform.diagnostics.source_sample_count == split.diagnostics.source_sample_count &&
                uniform.diagnostics.center_vertex_count == split.diagnostics.center_vertex_count &&
                uniform.diagnostics.center_index_count == split.diagnostics.center_index_count &&
                uniform.diagnostics.render_triangle_count ==
                    split.diagnostics.render_triangle_count,
            "uniform center sampling should only redistribute the existing product budget");
    require(uniform.diagnostics.maximum_sector_boundary_delta_m == 0.0F,
            "uniform center should preserve the exact outer seam");
    require(uniform.diagnostics.content_hash != split.diagnostics.content_hash,
            "uniform center sampling should produce distinct geometry");

    const auto medium = terrain_backdrop_density_profile(TerrainBackdropMeshDensity::Medium);
    const auto high = terrain_backdrop_density_profile(TerrainBackdropMeshDensity::High);
    require(std::abs(3'200.0F / static_cast<float>(medium.center_radial_intervals +
                                                   medium.hidden_radial_intervals) -
                     44.4444F) < 0.001F &&
                std::abs(3'200.0F / static_cast<float>(high.center_radial_intervals +
                                                       high.hidden_radial_intervals) -
                         33.3333F) < 0.001F,
            "uniform spacing should scale with every published density budget");
}

void test_decimated_center_and_sectors_share_the_same_rendered_seam_edges() {
    using namespace cubey::projects::terrain;
    TerrainBackdropProductRequest request = product_request();
    request.center_mode = TerrainBackdropCenterMode::Continuous;
    request.render_stride = 3U;
    const TerrainBackdropProduct product =
        make_terrain_backdrop_product(request, source_for_seed(9012U));
    const TerrainBackdropDensityProfile density = product.diagnostics.density;
    const std::uint32_t center_intervals =
        density.center_radial_intervals + density.hidden_radial_intervals;
    const std::uint32_t center_row_begin =
        1U + (center_intervals - 1U) * (density.angular_intervals + 1U);
    const std::set<BoundaryEdge> center_edges = rendered_boundary_edges(
        product.center.value(), center_row_begin, density.angular_intervals + 1U);

    const std::uint32_t intervals_per_sector = density.angular_intervals / density.sector_count;
    std::set<BoundaryEdge> sector_edges;
    for (std::uint32_t sector = 0U; sector < density.sector_count; ++sector) {
        const std::set<BoundaryEdge> local_edges = rendered_boundary_edges(
            product.sectors[sector], 0U, intervals_per_sector + 1U, sector * intervals_per_sector);
        sector_edges.insert(local_edges.begin(), local_edges.end());
    }
    require(center_edges == sector_edges,
            "decimated center and sectors should render identical seam partitions");
}

void test_decimated_center_fan_reaches_the_first_sampled_ring() {
    using namespace cubey::projects::terrain;
    TerrainBackdropProductRequest request = product_request();
    request.center_mode = TerrainBackdropCenterMode::Continuous;
    request.render_stride = 3U;
    const TerrainBackdropProduct product =
        make_terrain_backdrop_product(request, source_for_seed(9012U));
    const auto& indices = product.center->indices;
    require(indices.size() >= 3U && indices[0] == 0U && indices[2] == 1U,
            "continuous center fan should connect to its first sampled radial ring");
}

void test_high_product_publishes_the_center_acceptance_budget() {
    using namespace cubey::projects::terrain;
    TerrainBackdropProductRequest request = product_request();
    request.center_mode = TerrainBackdropCenterMode::Continuous;
    request.center_sampling = TerrainBackdropCenterSampling::SeamMatched;
    request.density = TerrainBackdropMeshDensity::High;
    request.render_stride = 3U;
    const TerrainBackdropProduct product =
        make_terrain_backdrop_product(request, source_for_seed(9012U));
    std::uint64_t retained_index_count = product.center->indices.size();
    for (const TerrainBackdropSectorMesh& sector : product.sectors) {
        retained_index_count += sector.indices.size();
    }
    require(product.diagnostics.center_render_triangle_count == 201'696U &&
                product.diagnostics.render_triangle_count == 742'368U,
            "high backdrop should retain the center acceptance triangle budget");
    require(retained_index_count == product.diagnostics.render_triangle_count * 3U &&
                retained_index_count < product.diagnostics.visible_index_count,
            "decimated products should retain only their draw topology");
}

void test_seam_matched_center_sampling_matches_the_outer_radial_step() {
    using namespace cubey::projects::terrain;
    TerrainBackdropProductRequest request = product_request();
    request.center_mode = TerrainBackdropCenterMode::Continuous;
    request.center_sampling = TerrainBackdropCenterSampling::SeamMatched;
    const TerrainBackdropProduct product =
        make_terrain_backdrop_product(request, source_for_seed(9012U));
    const TerrainBackdropDensityProfile density = product.diagnostics.density;
    const std::uint32_t center_intervals =
        density.center_radial_intervals + density.hidden_radial_intervals;
    const std::uint32_t angular_vertex_count = density.angular_intervals + 1U;
    const auto radius = [](const auto& vertex) {
        return std::sqrt(vertex.position[0] * vertex.position[0] +
                         vertex.position[2] * vertex.position[2]);
    };
    const auto& center = product.center.value();
    const float center_previous_radius =
        radius(center.vertices[1U + (center_intervals - 2U) * angular_vertex_count]);
    const float center_seam_radius =
        radius(center.vertices[1U + (center_intervals - 1U) * angular_vertex_count]);
    const std::uint32_t sector_row_width = density.angular_intervals / density.sector_count + 1U;
    const float outer_seam_radius = radius(product.sectors.front().vertices.front());
    const float outer_next_radius = radius(product.sectors.front().vertices[sector_row_width]);
    require(std::abs(center_seam_radius - outer_seam_radius) < 0.01F,
            "seam-matched center should terminate on the outer sector boundary");
    require(std::abs((center_seam_radius - center_previous_radius) -
                     (outer_next_radius - outer_seam_radius)) < 0.01F,
            "seam-matched center should match the first outer radial step");
}

void test_center_sampling_does_not_change_cutout_or_default_split_products() {
    using namespace cubey::projects::terrain;
    TerrainBackdropProductRequest default_request = product_request();
    const TerrainBackdropProduct default_product =
        make_terrain_backdrop_product(default_request, source_for_seed(9012U));
    TerrainBackdropProductRequest uniform_cutout_request = default_request;
    uniform_cutout_request.center_sampling = TerrainBackdropCenterSampling::Uniform;
    const TerrainBackdropProduct uniform_cutout =
        make_terrain_backdrop_product(uniform_cutout_request, source_for_seed(9012U));
    require(default_product.diagnostics.content_hash == uniform_cutout.diagnostics.content_hash,
            "center sampling policy should have no effect without a center mesh");

    TerrainBackdropProductRequest implicit_split_request = default_request;
    implicit_split_request.center_mode = TerrainBackdropCenterMode::Continuous;
    TerrainBackdropProductRequest explicit_split_request = implicit_split_request;
    explicit_split_request.center_sampling = TerrainBackdropCenterSampling::SplitLinearLog;
    const TerrainBackdropProduct implicit_split =
        make_terrain_backdrop_product(implicit_split_request, source_for_seed(9012U));
    const TerrainBackdropProduct explicit_split =
        make_terrain_backdrop_product(explicit_split_request, source_for_seed(9012U));
    require(implicit_split.diagnostics.content_hash == explicit_split.diagnostics.content_hash,
            "the product default should preserve split linear-log sampling");
}

} // namespace

int main() {
    try {
        test_density_profiles_publish_the_product_budget();
        test_v1_product_request_publishes_the_accepted_backdrop_contract();
        test_product_is_deterministic_connected_and_outside_the_stage();
        test_surface_models_preserve_geometry_and_change_only_surface_channels();
        test_climate_surface_requires_a_bound_climate_source();
        test_product_hash_changes_with_the_source_seed();
        test_product_retains_source_metadata();
        test_full_render_stride_retains_the_baked_topology();
        test_continuous_product_fills_the_center_and_preserves_the_outer_seam();
        test_uniform_center_sampling_redistributes_the_existing_budget();
        test_decimated_center_and_sectors_share_the_same_rendered_seam_edges();
        test_decimated_center_fan_reaches_the_first_sampled_ring();
        test_high_product_publishes_the_center_acceptance_budget();
        test_seam_matched_center_sampling_matches_the_outer_radial_step();
        test_center_sampling_does_not_change_cutout_or_default_split_products();
        std::cout << "terrain_backdrop_product_tests: ok\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "terrain_backdrop_product_tests: " << error.what() << '\n';
        return 1;
    }
}
