#include "terrain_export.h"
#include "terrain_hydrology.h"
#include "terrain_landscape_evolution.h"
#include "terrain_landscape_graph.h"
#include "terrain_mesh.h"
#include "terrain_patch.h"
#include "terrain_visualization.h"
#include "upland_mountain_source.h"

#include <cubey/procedural/field_2d.h>
#include <cubey/procedural/operators.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string_view>

namespace {

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

template <typename Fn> void require_throws(Fn&& fn, std::string_view message) {
    try {
        fn();
    } catch (const std::runtime_error&) {
        return;
    }
    throw std::runtime_error(std::string(message));
}

void require_near(float actual, float expected, float tolerance, std::string_view message) {
    if (std::abs(actual - expected) > tolerance) {
        throw std::runtime_error(std::string(message));
    }
}

void test_default_patch_contract() {
    const cubey::projects::terrain::TerrainPatchRequest request =
        cubey::projects::terrain::default_terrain_patch_request();
    const cubey::projects::terrain::TerrainPatchProduct product =
        cubey::projects::terrain::generate_terrain_patch(request);

    require(product.fields.desc().width == 257U && product.fields.desc().height == 257U,
            "terrain product should publish the requested interior only");
    require(product.fields.field_count() == 15U,
            "terrain v1 should publish source and hydrology fields");
    for (const std::string_view name : {
             cubey::projects::terrain::kTerrainFieldSourceHeightM,
             cubey::projects::terrain::kTerrainFieldMountainSupport,
             cubey::projects::terrain::kTerrainFieldHeightM,
             cubey::projects::terrain::kTerrainFieldSlope,
             cubey::projects::terrain::kTerrainFieldCurvature,
             cubey::projects::terrain::kTerrainFieldLocalReliefM,
             cubey::projects::terrain::kTerrainFieldRoutingSurfaceM,
             cubey::projects::terrain::kTerrainFieldRoutingFillDeltaM,
             cubey::projects::terrain::kTerrainFieldFlowDirectionX,
             cubey::projects::terrain::kTerrainFieldFlowDirectionZ,
             cubey::projects::terrain::kTerrainFieldContributingAreaM2,
             cubey::projects::terrain::kTerrainFieldStreamOrder,
             cubey::projects::terrain::kTerrainFieldDischargeProxy,
             cubey::projects::terrain::kTerrainFieldSinkMask,
             cubey::projects::terrain::kTerrainFieldFlowBoundaryMask,
         }) {
        require(product.fields.has_field(name), "terrain product is missing a required field");
        const cubey::procedural::ScalarFieldStats stats = product.fields.summarize_field(name);
        require(stats.sample_count == 257U * 257U, "terrain field sample count is incorrect");
        require(std::isfinite(stats.min) && std::isfinite(stats.max) && std::isfinite(stats.mean),
                "terrain field stats must be finite");
    }
    require(
        product.fields.summarize_field(cubey::projects::terrain::kTerrainFieldSourceHeightM).span >
            100.0F,
        "default terrain source should carry meaningful elevation relief");
    const cubey::procedural::ScalarField2D& source_height =
        product.fields.field(cubey::projects::terrain::kTerrainFieldSourceHeightM);
    const cubey::procedural::ScalarField2D& final_height =
        product.fields.field(cubey::projects::terrain::kTerrainFieldHeightM);
    require(std::equal(source_height.values().begin(), source_height.values().end(),
                       final_height.values().begin(), final_height.values().end()),
            "hydrology diagnostics must not modify terrain height");
    require(product.fields.summarize_field(cubey::projects::terrain::kTerrainFieldRoutingFillDeltaM)
                    .min >= 0.0F,
            "routing fill delta should be non-negative");
    require(
        product.fields.summarize_field(cubey::projects::terrain::kTerrainFieldStreamOrder).max >=
            2.0F,
        "default terrain should expose a drainage hierarchy");
    const cubey::procedural::ScalarFieldStats discharge =
        product.fields.summarize_field(cubey::projects::terrain::kTerrainFieldDischargeProxy);
    require(discharge.min >= 0.0F && discharge.max <= 1.0F,
            "discharge proxy should remain normalized");
    require(product.summary.content_hash != 0U, "terrain product should carry a content hash");
}

void test_source_and_derivatives_are_halo_invariant() {
    constexpr std::uint32_t interior_size = 33U;
    constexpr std::uint32_t small_halo = 8U;
    constexpr std::uint32_t large_halo = 16U;
    const cubey::procedural::Grid2DDesc small_grid{
        .width = interior_size + (small_halo * 2U),
        .height = interior_size + (small_halo * 2U),
        .cell_size = 32.0F,
        .origin_x = 1000.0F,
        .origin_y = -2000.0F,
    };
    const cubey::procedural::Grid2DDesc large_grid{
        .width = interior_size + (large_halo * 2U),
        .height = interior_size + (large_halo * 2U),
        .cell_size = 32.0F,
        .origin_x = 1000.0F,
        .origin_y = -2000.0F,
    };
    const cubey::procedural::FieldSet2D small_source =
        cubey::projects::terrain::sample_upland_mountain_fields_v1(small_grid, 9012U);
    const cubey::procedural::FieldSet2D large_source =
        cubey::projects::terrain::sample_upland_mountain_fields_v1(large_grid, 9012U);
    const cubey::procedural::SlopeCurvature2D small_derivatives =
        cubey::procedural::compute_slope_curvature(
            small_source.field(cubey::projects::terrain::kTerrainFieldSourceHeightM));
    const cubey::procedural::SlopeCurvature2D large_derivatives =
        cubey::procedural::compute_slope_curvature(
            large_source.field(cubey::projects::terrain::kTerrainFieldSourceHeightM));

    for (std::uint32_t z = 0; z < interior_size; ++z) {
        for (std::uint32_t x = 0; x < interior_size; ++x) {
            const std::uint32_t small_x = x + small_halo;
            const std::uint32_t small_z = z + small_halo;
            const std::uint32_t large_x = x + large_halo;
            const std::uint32_t large_z = z + large_halo;
            require_near(small_source.field(cubey::projects::terrain::kTerrainFieldSourceHeightM)
                             .at(small_x, small_z),
                         large_source.field(cubey::projects::terrain::kTerrainFieldSourceHeightM)
                             .at(large_x, large_z),
                         0.0001F, "terrain source should not depend on halo size");
            require_near(small_derivatives.slope.at(small_x, small_z),
                         large_derivatives.slope.at(large_x, large_z), 0.0001F,
                         "terrain slope should not depend on halo size");
            require_near(small_derivatives.curvature.at(small_x, small_z),
                         large_derivatives.curvature.at(large_x, large_z), 0.0001F,
                         "terrain curvature should not depend on halo size");
        }
    }
}

cubey::procedural::ScalarField2D synthetic_field(std::uint32_t size, float cell_size) {
    return cubey::procedural::ScalarField2D({.width = size, .height = size, .cell_size = cell_size},
                                            0.0F);
}

void test_priority_flood_repairs_a_pit() {
    cubey::procedural::ScalarField2D height = synthetic_field(17U, 10.0F);
    for (std::uint32_t z = 0; z < 17U; ++z) {
        for (std::uint32_t x = 0; x < 17U; ++x) {
            height.at(x, z) = 100.0F - static_cast<float>(z);
        }
    }
    height.at(8U, 8U) = -100.0F;
    const cubey::projects::terrain::TerrainHydrologyResult result =
        cubey::projects::terrain::compute_regional_hydrology(height, 0U);
    require(result.routing_fill_delta_m.at(8U, 8U) > 100.0F,
            "priority flood should raise an enclosed pit");
    require(result.routing_surface_m.at(8U, 8U) >= height.at(8U, 8U),
            "priority flood must not lower terrain");
    require(result.sink_mask.at(8U, 8U) == 0.0F,
            "priority flood should leave no sink at the repaired pit");
}

void test_monotonic_plane_routes_downhill() {
    cubey::procedural::ScalarField2D height = synthetic_field(17U, 10.0F);
    for (std::uint32_t z = 0; z < 17U; ++z) {
        for (std::uint32_t x = 0; x < 17U; ++x) {
            height.at(x, z) = 1000.0F - (static_cast<float>(x) * 10.0F);
        }
    }
    const cubey::projects::terrain::TerrainHydrologyResult result =
        cubey::projects::terrain::compute_regional_hydrology(height, 0U);
    require(result.flow_direction_x.at(8U, 8U) > 0.99F,
            "a monotonic x plane should route in positive x");
    require(std::abs(result.flow_direction_z.at(8U, 8U)) < 0.01F,
            "a monotonic x plane should not route in z");
    require(result.contributing_area_m2.at(15U, 8U) > result.contributing_area_m2.at(8U, 8U),
            "contributing area should grow downstream");
}

void test_branching_surface_increases_strahler_order() {
    cubey::procedural::ScalarField2D height = synthetic_field(33U, 10.0F);
    constexpr float outlet_x = 16.0F;
    constexpr float outlet_z = 32.0F;
    for (std::uint32_t z = 0; z < 33U; ++z) {
        for (std::uint32_t x = 0; x < 33U; ++x) {
            const float dx = static_cast<float>(x) - outlet_x;
            const float dz = static_cast<float>(z) - outlet_z;
            height.at(x, z) = std::sqrt((dx * dx) + (dz * dz)) * 10.0F;
        }
    }
    const cubey::projects::terrain::TerrainHydrologyResult result =
        cubey::projects::terrain::compute_regional_hydrology(height, 0U);
    require(result.stream_order.summarize().max >= 2.0F,
            "a converging synthetic valley should produce higher stream order");
}

void test_flow_area_is_conserved() {
    cubey::procedural::ScalarField2D height = synthetic_field(33U, 20.0F);
    for (std::uint32_t z = 0; z < 33U; ++z) {
        for (std::uint32_t x = 0; x < 33U; ++x) {
            height.at(x, z) =
                static_cast<float>(33U - z) * 8.0F + std::sin(static_cast<float>(x) * 0.37F) * 3.0F;
        }
    }
    const cubey::projects::terrain::TerrainHydrologyResult result =
        cubey::projects::terrain::compute_regional_hydrology(height, 0U);
    const double error = std::abs(result.terminal_outflow_area_m2 - result.total_input_area_m2);
    require(error <= result.total_input_area_m2 * 0.00001,
            "fractional routing should conserve contributed area");
}

void test_landscape_graph_breaches_depressions_and_conserves_area() {
    cubey::procedural::ScalarField2D height = synthetic_field(33U, 100.0F);
    for (std::uint32_t y = 0U; y < 33U; ++y) {
        for (std::uint32_t x = 0U; x < 33U; ++x) {
            const float dx = static_cast<float>(x) - 16.0F;
            const float dy = static_cast<float>(y) - 16.0F;
            height.at(x, y) = 1000.0F - std::sqrt((dx * dx) + (dy * dy)) * 10.0F;
        }
    }
    height.at(16U, 16U) = -500.0F;
    const cubey::projects::terrain::TerrainLandscapeGraph graph =
        cubey::projects::terrain::build_terrain_landscape_graph(height, 9012U, 0U);
    require(graph.breach_mask.at(16U, 16U) == 1.0F,
            "landscape graph should identify the enclosed depression route");
    require(graph.unresolved_sink_count == 0U,
            "landscape graph should route every interior sample to a boundary");
    const double error = std::abs(graph.terminal_outflow_area_m2 - graph.total_input_area_m2);
    require(error <= graph.total_input_area_m2 * 0.00001,
            "landscape river trees should conserve drainage area");
}

void test_landscape_graph_is_deterministic_and_seed_sensitive() {
    cubey::procedural::ScalarField2D height = synthetic_field(33U, 100.0F);
    for (std::uint32_t y = 0U; y < 33U; ++y) {
        for (std::uint32_t x = 0U; x < 33U; ++x) {
            height.at(x, y) = 500.0F - static_cast<float>(x + y) * 2.0F;
        }
    }
    const auto first = cubey::projects::terrain::build_terrain_landscape_graph(height, 9012U, 0U);
    const auto repeat = cubey::projects::terrain::build_terrain_landscape_graph(height, 9012U, 0U);
    const auto changed =
        cubey::projects::terrain::build_terrain_landscape_graph(height, 12345U, 0U);
    require(first.receiver == repeat.receiver,
            "landscape receiver choices should be deterministic");
    require(first.receiver != changed.receiver,
            "landscape receiver choices should consume the world seed");
}

void test_landscape_graph_uses_all_lower_neighbors_for_gradient_correction() {
    cubey::procedural::ScalarField2D plane = synthetic_field(17U, 100.0F);
    cubey::procedural::ScalarField2D diagonal = synthetic_field(17U, 100.0F);
    for (std::uint32_t y = 0U; y < 17U; ++y) {
        for (std::uint32_t x = 0U; x < 17U; ++x) {
            plane.at(x, y) = 1000.0F - static_cast<float>(x) * 10.0F;
            diagonal.at(x, y) = 1000.0F - static_cast<float>(x + y) * 10.0F;
        }
    }
    const auto plane_graph =
        cubey::projects::terrain::build_terrain_landscape_graph(plane, 9012U, 0U);
    const auto diagonal_graph =
        cubey::projects::terrain::build_terrain_landscape_graph(diagonal, 9012U, 0U);
    require_near(plane_graph.slope_correction.at(8U, 8U), 1.0F, 0.0001F,
                 "single-axis landscape gradient correction should remain one");
    require_near(diagonal_graph.slope_correction.at(8U, 8U), std::sqrt(2.0F), 0.0001F,
                 "landscape gradient correction should include every lower axis");
}

void test_landscape_multigrid_resampling_preserves_constant_fields() {
    cubey::procedural::ScalarField2D field = synthetic_field(33U, 50.0F);
    field.fill(123.0F);
    const cubey::procedural::ScalarField2D downsampled =
        cubey::projects::terrain::downsample_terrain_landscape_field(field);
    require(downsampled.desc().width == 17U && downsampled.desc().height == 17U &&
                downsampled.desc().cell_size == 100.0F,
            "landscape downsampling should preserve extent with doubled spacing");
    const cubey::procedural::ScalarField2D upsampled =
        cubey::projects::terrain::upsample_terrain_landscape_field(downsampled, 9012U, 0U);
    require(cubey::procedural::same_grid_desc(field.desc(), upsampled.desc()),
            "landscape upsampling should restore the source grid");
    for (const float value : upsampled.values()) {
        require_near(value, 123.0F, 0.0001F,
                     "landscape multigrid should preserve a constant field");
    }
}

void test_landscape_upsampling_jitter_is_deterministic() {
    cubey::procedural::ScalarField2D field = synthetic_field(17U, 100.0F);
    for (std::uint32_t y = 0U; y < 17U; ++y) {
        for (std::uint32_t x = 0U; x < 17U; ++x) {
            field.at(x, y) = static_cast<float>((x * x) + (y * 3U));
        }
    }
    const auto first = cubey::projects::terrain::upsample_terrain_landscape_field(field, 9012U, 1U);
    const auto repeat =
        cubey::projects::terrain::upsample_terrain_landscape_field(field, 9012U, 1U);
    const auto changed =
        cubey::projects::terrain::upsample_terrain_landscape_field(field, 12345U, 1U);
    require(first.values().size() == repeat.values().size(),
            "landscape upsampling should preserve deterministic dimensions");
    require(std::equal(first.values().begin(), first.values().end(), repeat.values().begin()),
            "landscape upsampling jitter should be deterministic");
    require(!std::equal(first.values().begin(), first.values().end(), changed.values().begin()),
            "landscape upsampling jitter should consume the world seed");
}

void test_landscape_evolution_age_zero_preserves_initial_height() {
    cubey::procedural::ScalarField2D height = synthetic_field(17U, 100.0F);
    cubey::procedural::ScalarField2D uplift = synthetic_field(17U, 100.0F);
    uplift.fill(1.0e-3F);
    for (std::uint32_t y = 0U; y < 17U; ++y) {
        for (std::uint32_t x = 0U; x < 17U; ++x) {
            height.at(x, y) = 1000.0F - (static_cast<float>(x + y) * 5.0F);
        }
    }
    cubey::projects::terrain::TerrainLandscapeEvolutionConfig config{};
    config.seed = 9012U;
    config.age_years = 0.0;
    config.multigrid_levels = 1U;
    config.iterations_per_level = 1U;
    config.relaxation = 1.0F;
    config.altitude_correction_iterations = 0U;
    const auto result = cubey::projects::terrain::evolve_terrain_landscape(height, uplift, config);
    for (std::size_t index = 0U; index < height.sample_count(); ++index) {
        require_near(result.height_m.values()[index], height.values()[index], 0.001F,
                     "zero-age landscape evolution should preserve initial elevation");
    }
}

void test_landscape_evolution_exposes_physical_processes() {
    cubey::procedural::ScalarField2D height = synthetic_field(17U, 100.0F);
    cubey::procedural::ScalarField2D uplift = synthetic_field(17U, 100.0F);
    uplift.fill(1.0e-3F);
    for (std::uint32_t y = 0U; y < 17U; ++y) {
        for (std::uint32_t x = 0U; x < 17U; ++x) {
            height.at(x, y) =
                5000.0F - (static_cast<float>(x) * 600.0F) - (static_cast<float>(y) * 10.0F);
        }
    }
    cubey::projects::terrain::TerrainLandscapeEvolutionConfig config{};
    config.seed = 9012U;
    config.multigrid_levels = 1U;
    config.iterations_per_level = 2U;
    config.relaxation = 1.0F;
    config.altitude_correction_iterations = 0U;
    const auto result = cubey::projects::terrain::evolve_terrain_landscape(height, uplift, config);
    require(result.unresolved_sink_count == 0U,
            "landscape evolution should retain open-boundary drainage");
    require(result.process_drainage_area_m2.summarize().max > 10'000.0F,
            "landscape evolution should accumulate physical drainage area");
    require(result.fluvial_advection_rate_m_per_year.summarize().max > 0.0F,
            "landscape evolution should expose fluvial advection speed");
    require(result.hillslope_advection_rate_m_per_year.summarize().max > 0.0F,
            "landscape evolution should expose hillslope advection speed");
    require(result.thermal_active_mask.summarize().max == 1.0F,
            "steep terrain should activate thermal erosion");
    require(!std::equal(result.height_m.values().begin(), result.height_m.values().end(),
                        height.values().begin()),
            "positive-age landscape evolution should change elevation");
}

void test_landscape_evolution_recipe_contract() {
    cubey::projects::terrain::TerrainPatchRequest request =
        cubey::projects::terrain::default_terrain_patch_request();
    request.recipe_id =
        std::string(cubey::projects::terrain::kTerrainRecipeUplandLandscapeEvolutionV1);
    request.generator_revision = cubey::projects::terrain::kTerrainUplandLandscapeEvolutionRevision;
    request.domain.interior_grid.width = 17U;
    request.domain.interior_grid.height = 17U;
    request.domain.interior_grid.cell_size = 100.0F;
    request.domain.seed = 9012U;
    const auto first = cubey::projects::terrain::generate_terrain_patch(request);
    const auto repeat = cubey::projects::terrain::generate_terrain_patch(request);
    require(first.fields.field_count() == 29U,
            "landscape recipe should publish source, process, and diagnostic fields");
    for (const std::string_view name : {
             cubey::projects::terrain::kTerrainFieldUpliftRateMPerYear,
             cubey::projects::terrain::kTerrainFieldProcessDrainageAreaM2,
             cubey::projects::terrain::kTerrainFieldProcessFlowDirectionX,
             cubey::projects::terrain::kTerrainFieldProcessFlowDirectionZ,
             cubey::projects::terrain::kTerrainFieldProcessBreachMask,
             cubey::projects::terrain::kTerrainFieldFluvialAdvectionRateMPerYear,
             cubey::projects::terrain::kTerrainFieldHillslopeAdvectionRateMPerYear,
             cubey::projects::terrain::kTerrainFieldThermalActiveMask,
             cubey::projects::terrain::kTerrainFieldAnalyticalHeightM,
             cubey::projects::terrain::kTerrainFieldAltitudeCorrectionDeltaM,
             cubey::projects::terrain::kTerrainFieldProcessDeltaM,
         }) {
        require(first.fields.has_field(name),
                "landscape recipe is missing a required process field");
    }
    require(first.summary.content_hash == repeat.summary.content_hash,
            "landscape recipe should be deterministic");
    require(
        !std::equal(
            first.fields.field(cubey::projects::terrain::kTerrainFieldSourceHeightM)
                .values()
                .begin(),
            first.fields.field(cubey::projects::terrain::kTerrainFieldSourceHeightM).values().end(),
            first.fields.field(cubey::projects::terrain::kTerrainFieldHeightM).values().begin()),
        "landscape recipe should evolve rather than republish its source");

    const std::filesystem::path output_dir =
        std::filesystem::temp_directory_path() / "cubey-terrain-landscape-manifest-test";
    std::filesystem::remove_all(output_dir);
    cubey::projects::terrain::write_terrain_manifest(first, output_dir);
    std::ifstream manifest_input(output_dir / "manifest.json");
    nlohmann::json manifest;
    manifest_input >> manifest;
    require(manifest.at("generation_scope") == "regional-not-seam-safe" &&
                manifest.at("process_halo_samples") ==
                    cubey::projects::terrain::kTerrainLandscapeProcessHaloSamples,
            "landscape manifest should disclose its regional solve boundary");
    require(manifest.at("process_model").at("name") == "transient-analytical-landscape-evolution" &&
                manifest.at("review_metrics").at("process_unresolved_sink_count") == 0U,
            "landscape manifest should identify its model and process health");
    std::filesystem::remove_all(output_dir);

    request.domain.seed += 1ULL << 32U;
    const auto changed = cubey::projects::terrain::generate_terrain_patch(request);
    require(first.summary.content_hash != changed.summary.content_hash,
            "landscape recipe should consume the upper seed bits");
}

void test_patch_determinism_and_seed_variation() {
    cubey::projects::terrain::TerrainPatchRequest request =
        cubey::projects::terrain::default_terrain_patch_request();
    request.domain.interior_grid.width = 33U;
    request.domain.interior_grid.height = 33U;
    const cubey::projects::terrain::TerrainPatchProduct first =
        cubey::projects::terrain::generate_terrain_patch(request);
    const cubey::projects::terrain::TerrainPatchProduct repeat =
        cubey::projects::terrain::generate_terrain_patch(request);
    require(first.summary.content_hash == repeat.summary.content_hash,
            "matching terrain requests should have matching hashes");

    request.domain.seed += 1U;
    const cubey::projects::terrain::TerrainPatchProduct changed =
        cubey::projects::terrain::generate_terrain_patch(request);
    require(first.summary.content_hash != changed.summary.content_hash,
            "terrain seed should change the product hash");

    request.domain.seed = first.request.domain.seed + (1ULL << 32U);
    const cubey::projects::terrain::TerrainPatchProduct upper_bits_changed =
        cubey::projects::terrain::generate_terrain_patch(request);
    require(first.summary.content_hash != upper_bits_changed.summary.content_hash,
            "terrain seed upper bits should change the product hash");
}

void test_broad_noise_control_contract() {
    std::uint64_t previous_hash = 0U;
    for (const std::uint64_t seed : {0ULL, 9012ULL, 12345ULL}) {
        cubey::projects::terrain::TerrainPatchRequest request =
            cubey::projects::terrain::default_terrain_patch_request();
        request.recipe_id =
            std::string(cubey::projects::terrain::kTerrainRecipeUplandBroadNoiseControlV1);
        request.generator_revision =
            cubey::projects::terrain::kTerrainUplandBroadNoiseControlRevision;
        request.domain.seed = seed;
        const cubey::projects::terrain::TerrainPatchProduct product =
            cubey::projects::terrain::generate_terrain_patch(request);
        require(product.fields.field_count() == 18U,
                "broad-noise control should publish decomposed source fields");
        for (const std::string_view name : {
                 cubey::projects::terrain::kTerrainFieldUpliftPotential,
                 cubey::projects::terrain::kTerrainFieldMacroMass,
                 cubey::projects::terrain::kTerrainFieldBaseReliefM,
             }) {
            require(product.fields.has_field(name),
                    "broad-noise control is missing a source component field");
        }
        const cubey::procedural::ScalarFieldStats source =
            product.fields.summarize_field(cubey::projects::terrain::kTerrainFieldSourceHeightM);
        const cubey::procedural::ScalarFieldStats support =
            product.fields.summarize_field(cubey::projects::terrain::kTerrainFieldMountainSupport);
        require(source.span >= 800.0F,
                "broad-noise control should retain mountain-scale elevation relief");
        require(support.min >= 0.0F && support.max <= 1.0F,
                "broad-noise mountain support should remain in unit range");
        require(previous_hash == 0U || previous_hash != product.summary.content_hash,
                "broad-noise audit seeds should produce distinct products");
        previous_hash = product.summary.content_hash;
    }

    cubey::projects::terrain::TerrainPatchRequest base =
        cubey::projects::terrain::default_terrain_patch_request();
    base.recipe_id = std::string(cubey::projects::terrain::kTerrainRecipeUplandBroadNoiseControlV1);
    base.generator_revision = cubey::projects::terrain::kTerrainUplandBroadNoiseControlRevision;
    base.domain.interior_grid.width = 33U;
    base.domain.interior_grid.height = 33U;
    base.domain.seed = 9012U;
    cubey::projects::terrain::TerrainPatchRequest upper_seed = base;
    upper_seed.domain.seed += 1ULL << 32U;
    require(cubey::projects::terrain::generate_terrain_patch(base).summary.content_hash !=
                cubey::projects::terrain::generate_terrain_patch(upper_seed).summary.content_hash,
            "broad-noise control should consume terrain seed upper bits");

    cubey::projects::terrain::TerrainPatchRequest right = base;
    right.domain.interior_grid.origin_x +=
        static_cast<float>(base.domain.interior_grid.width - 1U) *
        base.domain.interior_grid.cell_size;
    const cubey::projects::terrain::TerrainPatchProduct left_product =
        cubey::projects::terrain::generate_terrain_patch(base);
    const cubey::projects::terrain::TerrainPatchProduct right_product =
        cubey::projects::terrain::generate_terrain_patch(right);
    for (const std::string_view name : {
             cubey::projects::terrain::kTerrainFieldUpliftPotential,
             cubey::projects::terrain::kTerrainFieldMacroMass,
             cubey::projects::terrain::kTerrainFieldBaseReliefM,
             cubey::projects::terrain::kTerrainFieldSourceHeightM,
         }) {
        const cubey::procedural::ScalarField2D& left_field = left_product.fields.field(name);
        const cubey::procedural::ScalarField2D& right_field = right_product.fields.field(name);
        for (std::uint32_t y = 0U; y < left_field.desc().height; ++y) {
            require_near(left_field.at(left_field.desc().width - 1U, y), right_field.at(0U, y),
                         0.0001F, "broad-noise source fields should seam in world space");
        }
    }
}

void test_adjacent_patch_source_seam() {
    cubey::projects::terrain::TerrainPatchRequest left =
        cubey::projects::terrain::default_terrain_patch_request();
    left.domain.interior_grid.width = 33U;
    left.domain.interior_grid.height = 33U;
    cubey::projects::terrain::TerrainPatchRequest right = left;
    const float patch_spacing = static_cast<float>(left.domain.interior_grid.width - 1U) *
                                left.domain.interior_grid.cell_size;
    right.domain.address.x = 1;
    right.domain.interior_grid.origin_x += patch_spacing;

    const cubey::projects::terrain::TerrainPatchProduct left_product =
        cubey::projects::terrain::generate_terrain_patch(left);
    const cubey::projects::terrain::TerrainPatchProduct right_product =
        cubey::projects::terrain::generate_terrain_patch(right);
    for (const std::string_view name : {
             cubey::projects::terrain::kTerrainFieldSourceHeightM,
             cubey::projects::terrain::kTerrainFieldMountainSupport,
             cubey::projects::terrain::kTerrainFieldHeightM,
             cubey::projects::terrain::kTerrainFieldSlope,
             cubey::projects::terrain::kTerrainFieldCurvature,
         }) {
        const cubey::procedural::ScalarField2D& left_field = left_product.fields.field(name);
        const cubey::procedural::ScalarField2D& right_field = right_product.fields.field(name);
        for (std::uint32_t y = 0; y < left_field.desc().height; ++y) {
            require_near(left_field.at(left_field.desc().width - 1U, y), right_field.at(0U, y),
                         0.0001F, "adjacent terrain fields should agree at their shared edge");
        }
    }
}

void test_request_validation() {
    cubey::projects::terrain::TerrainPatchRequest request =
        cubey::projects::terrain::default_terrain_patch_request();
    request.domain.interior_grid.width = 32U;
    require_throws(
        [&request] { cubey::projects::terrain::validate_terrain_patch_request(request); },
        "terrain request should reject even dimensions");

    request = cubey::projects::terrain::default_terrain_patch_request();
    request.domain.interior_grid.cell_size = std::numeric_limits<float>::infinity();
    require_throws(
        [&request] { cubey::projects::terrain::validate_terrain_patch_request(request); },
        "terrain request should reject non-finite cell size");

    request = cubey::projects::terrain::default_terrain_patch_request();
    request.domain.border_samples = 8U;
    require_throws(
        [&request] { cubey::projects::terrain::validate_terrain_patch_request(request); },
        "terrain request should reject a non-v1 halo");

    request = cubey::projects::terrain::default_terrain_patch_request();
    request.recipe_id = "unknown";
    require_throws(
        [&request] { cubey::projects::terrain::validate_terrain_patch_request(request); },
        "terrain request should reject unknown recipes");

    request = cubey::projects::terrain::default_terrain_patch_request();
    request.recipe_id =
        std::string(cubey::projects::terrain::kTerrainRecipeUplandBroadNoiseControlV1);
    require_throws(
        [&request] { cubey::projects::terrain::validate_terrain_patch_request(request); },
        "terrain request should reject a recipe revision mismatch");
}

void test_scalar_export_and_manifest() {
    cubey::projects::terrain::TerrainPatchRequest request =
        cubey::projects::terrain::default_terrain_patch_request();
    request.domain.interior_grid.width = 17U;
    request.domain.interior_grid.height = 17U;
    request.domain.interior_grid.origin_x = -512.0F;
    request.domain.interior_grid.origin_y = 256.0F;
    const cubey::projects::terrain::TerrainPatchProduct product =
        cubey::projects::terrain::generate_terrain_patch(request);
    const std::filesystem::path output_dir =
        std::filesystem::temp_directory_path() / "cubey-terrain-export-test";
    std::filesystem::remove_all(output_dir);
    cubey::projects::terrain::write_terrain_field_exports(
        product, output_dir,
        cubey::projects::terrain::TerrainExportOptions{.write_raw_float32 = true});

    for (const std::string& name : product.fields.field_names()) {
        const std::filesystem::path path = output_dir / (name + ".png");
        require(std::filesystem::exists(path) && std::filesystem::file_size(path) > 8U,
                "terrain scalar export should write a non-empty PNG");
        const std::filesystem::path raw_path = output_dir / (name + ".f32");
        require(std::filesystem::exists(raw_path) &&
                    std::filesystem::file_size(raw_path) ==
                        product.fields.field(name).sample_count() * sizeof(float),
                "terrain scalar export should write a complete raw field");
    }
    std::ifstream manifest_input(output_dir / "manifest.json");
    require(static_cast<bool>(manifest_input), "terrain export should write a manifest");
    nlohmann::json manifest;
    manifest_input >> manifest;
    require(manifest.at("schema") == "cubey.terrain.patch.v3",
            "terrain manifest should use the v3 schema");
    require(manifest.at("field_count") == product.fields.field_count(),
            "terrain manifest should report every product field");
    require(manifest.at("content_hash") == product.summary.content_hash,
            "terrain manifest should report the product hash");
    require(manifest.at("process_halo_samples") ==
                cubey::projects::terrain::kTerrainProcessHaloSamples,
            "terrain manifest should report the process halo");
    require(manifest.at("interior_grid").at("origin_x_m") == -512.0F &&
                manifest.at("interior_grid").at("origin_z_m") == 256.0F,
            "terrain manifest should report regional sampling origins");
    const nlohmann::json& source_entry =
        manifest.at("fields").at(cubey::projects::terrain::kTerrainFieldSourceHeightM);
    require(source_entry.contains("p05") && source_entry.contains("p50") &&
                source_entry.contains("p95"),
            "terrain manifest should report field distributions");
    require(source_entry.at("display").at("low") == 0.0F &&
                source_entry.at("display").at("high") == 2500.0F &&
                source_entry.at("display").at("range_scope") == "fixed",
            "terrain height exports should use a fixed physical display range");
    require(source_entry.at("raw_encoding") == "float32-le-row-major" &&
                source_entry.at("raw_byte_count") ==
                    product.fields.field(cubey::projects::terrain::kTerrainFieldSourceHeightM)
                            .sample_count() *
                        sizeof(float),
            "terrain manifest should describe the lossless raw field");
    require(manifest.at("review_metrics").contains("source_gradient_anisotropy") &&
                manifest.at("review_metrics").contains("routing_fill_coverage_gt_50m"),
            "terrain manifest should report morphology review metrics");
    std::filesystem::remove_all(output_dir);
}

void test_raw_scalar_export_is_little_endian_row_major() {
    cubey::procedural::ScalarField2D field({.width = 2U, .height = 2U, .cell_size = 1.0F}, 0.0F);
    field.at(0U, 0U) = 1.0F;
    field.at(1U, 0U) = -2.0F;
    field.at(0U, 1U) = 0.5F;
    field.at(1U, 1U) = 4.0F;
    const std::vector<std::uint8_t> bytes =
        cubey::projects::terrain::encode_terrain_scalar_field_f32_le(field);
    const std::vector<std::uint8_t> expected{
        0x00U, 0x00U, 0x80U, 0x3fU, 0x00U, 0x00U, 0x00U, 0xc0U,
        0x00U, 0x00U, 0x00U, 0x3fU, 0x00U, 0x00U, 0x80U, 0x40U,
    };
    require(bytes == expected, "raw scalar export should preserve row-major IEEE-754 values");
}

void test_fixed_field_display_ranges() {
    cubey::procedural::ScalarField2D first({.width = 3U, .height = 1U, .cell_size = 32.0F}, 0.0F);
    cubey::procedural::ScalarField2D second({.width = 3U, .height = 1U, .cell_size = 32.0F}, 0.0F);
    first.at(0U, 0U) = 0.0F;
    first.at(1U, 0U) = 1.0F;
    first.at(2U, 0U) = 2.0F;
    second.at(0U, 0U) = 0.5F;
    second.at(1U, 0U) = 1.0F;
    second.at(2U, 0U) = 2.5F;
    const cubey::projects::terrain::TerrainFieldDisplaySpec first_display =
        cubey::projects::terrain::terrain_field_display_spec(
            cubey::projects::terrain::kTerrainFieldSlope, first);
    const cubey::projects::terrain::TerrainFieldDisplaySpec second_display =
        cubey::projects::terrain::terrain_field_display_spec(
            cubey::projects::terrain::kTerrainFieldSlope, second);
    require(first_display.low == second_display.low && first_display.high == second_display.high &&
                !first_display.patch_relative && !second_display.patch_relative,
            "known terrain fields should not derive display ranges from patch extrema");
    require_near(cubey::projects::terrain::terrain_field_display_value(1.0F, first_display),
                 cubey::projects::terrain::terrain_field_display_value(1.0F, second_display), 0.0F,
                 "equal physical values should map to equal display values");
}

void test_terrain_mesh_consumes_product_fields() {
    cubey::projects::terrain::TerrainPatchRequest request =
        cubey::projects::terrain::default_terrain_patch_request();
    request.domain.interior_grid.width = 17U;
    request.domain.interior_grid.height = 17U;
    const cubey::projects::terrain::TerrainPatchProduct product =
        cubey::projects::terrain::generate_terrain_patch(request);
    const cubey::projects::terrain::TerrainMeshData surface =
        cubey::projects::terrain::make_terrain_mesh(product, "surface", 1.0F);
    const cubey::projects::terrain::TerrainMeshData flow =
        cubey::projects::terrain::make_terrain_mesh(product, "flow-direction", 1.0F);
    require(surface.vertices.size() == 17U * 17U,
            "terrain mesh should have one vertex per product sample");
    require(surface.indices.size() == 16U * 16U * 6U,
            "terrain mesh should triangulate every product cell");
    require(flow.vertices.size() == surface.vertices.size(),
            "terrain debug views should preserve mesh topology");
    require(
        surface.vertices.front().position[1] ==
            product.fields.field(cubey::projects::terrain::kTerrainFieldHeightM).values().front(),
        "terrain mesh height should come from the CPU product");
    require(surface.vertices.front().color != flow.vertices.front().color,
            "terrain diagnostic view should change uploaded vertex color");
    require_throws(
        [&product] {
            static_cast<void>(
                cubey::projects::terrain::make_terrain_mesh(product, "not-a-field", 1.0F));
        },
        "terrain mesh should reject unknown debug views");
}

} // namespace

int main() {
    try {
        test_default_patch_contract();
        test_source_and_derivatives_are_halo_invariant();
        test_patch_determinism_and_seed_variation();
        test_broad_noise_control_contract();
        test_adjacent_patch_source_seam();
        test_request_validation();
        test_priority_flood_repairs_a_pit();
        test_monotonic_plane_routes_downhill();
        test_branching_surface_increases_strahler_order();
        test_flow_area_is_conserved();
        test_landscape_graph_breaches_depressions_and_conserves_area();
        test_landscape_graph_is_deterministic_and_seed_sensitive();
        test_landscape_graph_uses_all_lower_neighbors_for_gradient_correction();
        test_landscape_multigrid_resampling_preserves_constant_fields();
        test_landscape_upsampling_jitter_is_deterministic();
        test_landscape_evolution_age_zero_preserves_initial_height();
        test_landscape_evolution_exposes_physical_processes();
        test_landscape_evolution_recipe_contract();
        test_scalar_export_and_manifest();
        test_raw_scalar_export_is_little_endian_row_major();
        test_fixed_field_display_ranges();
        test_terrain_mesh_consumes_product_fields();
        std::cout << "terrain_tests: ok\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "terrain_tests: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
