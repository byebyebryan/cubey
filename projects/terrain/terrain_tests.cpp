#include "terrain_patch.h"

#include <cubey/procedural/field_2d.h>

#include <cmath>
#include <cstdlib>
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
    require(product.fields.field_count() == 6U,
            "terrain source slice should publish six named fields");
    for (const std::string_view name : {
             cubey::projects::terrain::kTerrainFieldSourceHeightM,
             cubey::projects::terrain::kTerrainFieldMountainSupport,
             cubey::projects::terrain::kTerrainFieldHeightM,
             cubey::projects::terrain::kTerrainFieldSlope,
             cubey::projects::terrain::kTerrainFieldCurvature,
             cubey::projects::terrain::kTerrainFieldLocalReliefM,
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
    require(product.summary.content_hash != 0U, "terrain product should carry a content hash");
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
}

} // namespace

int main() {
    try {
        test_default_patch_contract();
        test_patch_determinism_and_seed_variation();
        test_adjacent_patch_source_seam();
        test_request_validation();
        std::cout << "terrain_tests: ok\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "terrain_tests: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
