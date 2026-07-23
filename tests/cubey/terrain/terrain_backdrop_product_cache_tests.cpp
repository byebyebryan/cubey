#include <cubey/procedural/artifact_cache.h>
#include <cubey/procedural/artifact_metadata.h>
#include <cubey/procedural/hash.h>
#include <cubey/terrain/terrain_backdrop_product_cache.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace {

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

template <typename Callable> void require_throws(Callable&& callable, std::string_view message) {
    try {
        std::forward<Callable>(callable)();
    } catch (const std::runtime_error&) {
        return;
    }
    throw std::runtime_error(std::string(message));
}

class CacheFixture {
  public:
    CacheFixture() {
        const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
        root = std::filesystem::temp_directory_path() /
               ("cubey-terrain-product-cache-" + std::to_string(suffix));
        std::filesystem::create_directories(root);
    }

    ~CacheFixture() {
        std::error_code error;
        std::filesystem::remove_all(root, error);
    }

    std::filesystem::path root{};
};

[[nodiscard]] cubey::terrain::TerrainBackdropSectorMesh test_mesh(std::uint32_t sector_index) {
    const float x = static_cast<float>(sector_index);
    cubey::terrain::TerrainBackdropSectorMesh mesh;
    mesh.vertices = {
        {.position = {x, 100.0F, 0.0F},
         .material = {0.25F, 0.5F, 0.75F},
         .normal = {0.0F, 1.0F, 0.0F},
         .surface = {0.2F, 0.8F}},
        {.position = {x + 1.0F, 120.0F, 0.0F},
         .material = {0.5F, 0.25F, 0.1F},
         .normal = {0.0F, 1.0F, 0.0F},
         .surface = {0.4F, 0.6F}},
        {.position = {x, 140.0F, 1.0F},
         .material = {0.75F, 0.1F, 0.25F},
         .normal = {0.0F, 1.0F, 0.0F},
         .surface = {0.6F, 0.4F}},
    };
    mesh.indices = {0U, 1U, 2U};
    mesh.bounds = {
        .minimum = {x, 100.0F, 0.0F},
        .maximum = {x + 1.0F, 140.0F, 1.0F},
        .center = {x + 0.5F, 120.0F, 0.5F},
    };
    mesh.begin_azimuth_radians = x * 0.1F;
    mesh.end_azimuth_radians = (x + 1.0F) * 0.1F;
    return mesh;
}

[[nodiscard]] cubey::terrain::TerrainBackdropProduct test_product() {
    using namespace cubey::terrain;
    TerrainBackdropProduct product;
    product.request = {
        .source_focus_xz = {2'500.0F, -7'500.0F},
        .density = TerrainBackdropMeshDensity::Low,
        .center_mode = TerrainBackdropCenterMode::Cutout,
        .center_sampling = TerrainBackdropCenterSampling::SeamMatched,
        .render_stride = 3U,
        .consumer_radius_m = 400.0F,
        .visible_inner_radius_m = 3'200.0F,
        .outer_radius_m = 16'384.0F,
        .vertical_scale = 1.25F,
        .vertical_offset_m = -420.0F,
    };
    product.source = {
        .id = "terrain-diffusion-default-v1",
        .seed = 0U,
        .base_height_m = -640.0F,
        .relief_scale_m = 5'600.0F,
        .gradient_step_m = 32.0F,
    };
    const TerrainBackdropDensityProfile density =
        terrain_backdrop_density_profile(product.request.density);
    product.sectors.reserve(density.sector_count);
    for (std::uint32_t sector = 0U; sector < density.sector_count; ++sector) {
        product.sectors.push_back(test_mesh(sector));
    }
    product.diagnostics = {
        .density = density,
        .source_sample_count = 42'000U,
        .sampled_vertex_count = 96U,
        .full_triangle_count = 32U,
        .render_vertex_count = 96U,
        .render_triangle_count = 32U,
        .minimum_height_m = 100.0F,
        .maximum_height_m = 140.0F,
        .maximum_sector_boundary_delta_m = 0.0F,
        .content_hash = 0x1234U,
        .geometry_hash = 0x5678U,
        .mean_rock = 0.4F,
        .mean_snow = 0.2F,
        .mean_vegetation = 0.3F,
        .mean_moisture = 0.5F,
    };
    return product;
}

[[nodiscard]] cubey::terrain::TerrainBackdropProductRecipeContext recipe_context() {
    return {
        .source_content_sha256 = "source-sha256",
        .climate_content_sha256 = "climate-sha256",
        .surface_formula_version = "terrain-surface-v4",
        .surface_parameter_hash = 0xabcdefU,
        .placement_parameter_hash = 0x13579U,
    };
}

[[nodiscard]] cubey::procedural::ProceduralArtifactMetadata
test_metadata(const cubey::procedural::ProceduralArtifactRecipe& recipe,
              std::uint64_t content_hash) {
    return cubey::procedural::make_procedural_artifact_metadata(
        cubey::procedural::make_procedural_artifact_identity(recipe.name, recipe.generator,
                                                             recipe.formula_version, recipe.domain,
                                                             recipe.seed, recipe.space),
        recipe.kind, recipe.format, recipe.extent, content_hash);
}

void test_recipe_hash_covers_source_surface_placement_and_request() {
    using namespace cubey::terrain;
    const TerrainBackdropProduct product = test_product();
    const TerrainBackdropProductRecipeContext context = recipe_context();
    const auto base =
        terrain_backdrop_product_cache_recipe(product.request, product.source, context);

    TerrainBackdropProductRecipeContext changed_context = context;
    changed_context.source_content_sha256 = "different-source-sha256";
    require(cubey::procedural::procedural_artifact_recipe_hash(base) !=
                cubey::procedural::procedural_artifact_recipe_hash(
                    terrain_backdrop_product_cache_recipe(product.request, product.source,
                                                          changed_context)),
            "terrain product recipes should invalidate when source content changes");

    changed_context = context;
    changed_context.surface_parameter_hash += 1U;
    require(cubey::procedural::procedural_artifact_recipe_hash(base) !=
                cubey::procedural::procedural_artifact_recipe_hash(
                    terrain_backdrop_product_cache_recipe(product.request, product.source,
                                                          changed_context)),
            "terrain product recipes should invalidate when surface parameters change");

    changed_context = context;
    changed_context.placement_parameter_hash += 1U;
    require(cubey::procedural::procedural_artifact_recipe_hash(base) !=
                cubey::procedural::procedural_artifact_recipe_hash(
                    terrain_backdrop_product_cache_recipe(product.request, product.source,
                                                          changed_context)),
            "terrain product recipes should invalidate when placement parameters change");

    TerrainBackdropProductRequest changed_request = product.request;
    changed_request.render_stride += 1U;
    require(
        cubey::procedural::procedural_artifact_recipe_hash(base) !=
            cubey::procedural::procedural_artifact_recipe_hash(
                terrain_backdrop_product_cache_recipe(changed_request, product.source, context)),
        "terrain product recipes should invalidate when mesh requests change");
}

void test_product_codec_round_trips_and_rejects_malformed_payloads() {
    const cubey::terrain::TerrainBackdropProduct product = test_product();
    constexpr std::array<std::uint8_t, 7U> kAuxiliary{4U, 8U, 15U, 16U, 23U, 42U, 99U};
    const std::vector<std::uint8_t> encoded =
        cubey::terrain::encode_terrain_backdrop_product(product, kAuxiliary);
    const cubey::terrain::DecodedTerrainBackdropProduct decoded =
        cubey::terrain::decode_terrain_backdrop_product(encoded);
    require(decoded.auxiliary == std::vector<std::uint8_t>(kAuxiliary.begin(), kAuxiliary.end()),
            "terrain product codec should preserve auxiliary bytes");
    require(cubey::terrain::encode_terrain_backdrop_product(decoded.product, decoded.auxiliary) ==
                encoded,
            "terrain product codec should preserve every serialized product field");

    std::vector<std::uint8_t> corrupt = encoded;
    corrupt.front() ^= 0xffU;
    require_throws(
        [&] { static_cast<void>(cubey::terrain::decode_terrain_backdrop_product(corrupt)); },
        "terrain product codec should reject incompatible headers");

    std::vector<std::uint8_t> truncated = encoded;
    truncated.resize(truncated.size() - 5U);
    require_throws(
        [&] { static_cast<void>(cubey::terrain::decode_terrain_backdrop_product(truncated)); },
        "terrain product codec should reject truncated payloads");

    std::vector<std::uint8_t> trailing = encoded;
    trailing.push_back(0U);
    require_throws(
        [&] { static_cast<void>(cubey::terrain::decode_terrain_backdrop_product(trailing)); },
        "terrain product codec should reject trailing payload bytes");
}

void test_product_codec_round_trips_through_the_shared_artifact_cache() {
    const cubey::terrain::TerrainBackdropProduct product = test_product();
    const auto recipe = cubey::terrain::terrain_backdrop_product_cache_recipe(
        product.request, product.source, recipe_context());
    constexpr std::array<std::uint8_t, 3U> kAuxiliary{2U, 4U, 8U};
    const std::vector<std::uint8_t> encoded =
        cubey::terrain::encode_terrain_backdrop_product(product, kAuxiliary);

    CacheFixture fixture;
    cubey::procedural::ProceduralArtifactCache cache({.root = fixture.root});
    const std::uint64_t content_hash = cubey::procedural::procedural_hash_bytes(encoded);
    const auto stored = cache.store(recipe, test_metadata(recipe, content_hash), encoded);
    require(stored.stored, "shared artifact cache should store encoded terrain products");

    const auto loaded = cache.load(recipe);
    require(loaded.outcome == cubey::procedural::ProceduralArtifactCacheLoadOutcome::Hit &&
                loaded.artifact.has_value() && loaded.artifact->payload == encoded &&
                loaded.artifact->metadata.content_hash == content_hash,
            "shared artifact cache should preserve encoded terrain products");
    const auto decoded = cubey::terrain::decode_terrain_backdrop_product(loaded.artifact->payload);
    require(decoded.product.diagnostics.geometry_hash == product.diagnostics.geometry_hash &&
                decoded.auxiliary ==
                    std::vector<std::uint8_t>(kAuxiliary.begin(), kAuxiliary.end()),
            "cached terrain products should remain usable after typed decode");
}

} // namespace

int main() {
    try {
        test_recipe_hash_covers_source_surface_placement_and_request();
        test_product_codec_round_trips_and_rejects_malformed_payloads();
        test_product_codec_round_trips_through_the_shared_artifact_cache();
        std::cout << "terrain_backdrop_product_cache_tests: ok\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "terrain_backdrop_product_cache_tests: " << error.what() << '\n';
        return 1;
    }
}
