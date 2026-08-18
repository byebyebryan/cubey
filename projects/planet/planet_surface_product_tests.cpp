#include "planet_config.h"
#include "planet_surface_product.h"

#include <cubey/procedural/artifact_cache.h>

#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

} // namespace

namespace cubey::projects::planet::tests {
void parses_live_options_and_preserves_typed_unset_state();
void rejects_unrelated_paths_and_flags();
void parses_debug_view_and_resolves_runtime_selection();
void template_contains_only_common_profile_and_live_planet_scope();
void layered_sources_preserve_precedence();
} // namespace cubey::projects::planet::tests

int main() {
    try {
        const std::filesystem::path cache_root =
            std::filesystem::temp_directory_path() / "cubey-planet-surface-product-tests";
        std::filesystem::remove_all(cache_root);
        cubey::procedural::ProceduralArtifactCache cache({.root = cache_root});
        const cubey::projects::planet::PlanetSurfaceProductConfig config{
            .extent = 32U,
            .seed = 9012U,
        };
        const cubey::projects::planet::PlanetSurfaceProduct first =
            cubey::projects::planet::prepare_planet_surface_product(cache, config);
        require(!first.cache_hit, "first surface product build must miss the cache");
        require(first.rgba.size() == 32U * 32U * 6U * 4U,
                "surface product must populate all cubemap faces");
        require(first.content_hash != 0U, "surface product must record a content hash");

        const cubey::projects::planet::PlanetSurfaceProduct second =
            cubey::projects::planet::prepare_planet_surface_product(cache, config);
        require(second.cache_hit, "second surface product build must hit the cache");
        require(second.rgba == first.rgba, "cached surface product must preserve texels");
        require(second.content_hash == first.content_hash,
                "cached surface product must preserve the content hash");

        const cubey::procedural::ProceduralArtifactRecipe recipe =
            cubey::projects::planet::planet_surface_product_recipe(config);
        require(recipe.space == cubey::procedural::ProceduralDomainSpace::Spherical,
                "surface product must be declared as spherical data");
        require(recipe.kind == cubey::procedural::ProceduralArtifactKind::TextureCube,
                "surface product must be a cubemap");
        std::filesystem::remove_all(cache_root);
        cubey::projects::planet::tests::parses_live_options_and_preserves_typed_unset_state();
        cubey::projects::planet::tests::rejects_unrelated_paths_and_flags();
        cubey::projects::planet::tests::parses_debug_view_and_resolves_runtime_selection();
        cubey::projects::planet::tests::
            template_contains_only_common_profile_and_live_planet_scope();
        cubey::projects::planet::tests::layered_sources_preserve_precedence();
    } catch (const std::exception& error) {
        std::cerr << "planet surface product test failure: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
