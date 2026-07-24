#include <cubey/terrain/terrain_backdrop_surface.h>

#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void require_near(float actual, float expected, const char* message) {
    if (std::abs(actual - expected) > 1.0e-3F) {
        throw std::runtime_error(message);
    }
}

cubey::terrain::TerrainBackdropProduct sloped_product() {
    cubey::terrain::TerrainBackdropProduct product{};
    product.center.emplace();
    product.center->vertices = {
        {.position = {0.0F, 0.0F, 0.0F}},
        {.position = {10.0F, 10.0F, 0.0F}},
        {.position = {0.0F, 0.0F, 10.0F}},
    };
    product.center->indices = {0U, 1U, 2U};
    return product;
}

void test_surface_envelope_follows_rendered_triangles() {
    const cubey::terrain::TerrainBackdropProduct product = sloped_product();
    const cubey::terrain::TerrainBackdropSurfaceEnvelope point =
        cubey::terrain::terrain_backdrop_surface_envelope(product, 0.0F);
    const cubey::terrain::TerrainBackdropSurfaceEnvelope local =
        cubey::terrain::terrain_backdrop_surface_envelope(product, 2.0F);
    const cubey::terrain::TerrainBackdropSurfaceEnvelope full =
        cubey::terrain::terrain_backdrop_surface_envelope(product, 20.0F);
    require_near(point.nominal_local_height_m, 0.0F, "point envelope should use the focus vertex");
    require_near(local.maximum_local_height_m, 2.0F,
                 "local envelope should interpolate the rendered slope at the disk boundary");
    require_near(full.maximum_local_height_m, 10.0F,
                 "wide envelope should include the triangle peak");
}

void test_surface_envelope_rejects_missing_center_geometry() {
    bool rejected = false;
    try {
        static_cast<void>(cubey::terrain::terrain_backdrop_surface_envelope({}, 1.0F));
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    require(rejected, "surface envelope should require center geometry");
}

} // namespace

int main() {
    try {
        test_surface_envelope_follows_rendered_triangles();
        test_surface_envelope_rejects_missing_center_geometry();
        std::cout << "terrain_backdrop_surface_tests: ok\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "terrain_backdrop_surface_tests: " << error.what() << '\n';
        return 1;
    }
}
