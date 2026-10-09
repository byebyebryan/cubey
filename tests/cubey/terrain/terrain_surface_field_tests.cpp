#include <cmath>
#include <cubey/terrain/terrain_surface_field.h>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void require(bool ok, const char* why) {
    if (!ok)
        throw std::runtime_error(why);
}
template <class F> void rejects(F f) {
    bool failed = false;
    try {
        f();
    } catch (const std::exception&) {
        failed = true;
    }
    require(failed, "invalid surface contract accepted");
}
class Source final : public cubey::asset::TerrainHeightSource {
  public:
    float slope = 0;
    bool nonfinite = false;
    cubey::asset::TerrainHeightSourceMetadata metadata() const noexcept override {
        return {
            .id = "surface-field-test", .seed = 91, .base_height_m = 100, .relief_scale_m = 2000};
    }
    float sample_height(const cubey::asset::TerrainQuery& q) const override {
        return nonfinite ? std::numeric_limits<float>::quiet_NaN() : 100 + q.world_xz.x * slope;
    }
};
} // namespace
int main() {
    try {
        using namespace cubey;
        Source source;
        const asset::TerrainHeightSourceBounds bounds{{-4096, -3072}, {4096, 3072}};
        const auto flat = terrain::make_terrain_surface_field(source, bounds, nullptr, 65);
        const auto again = terrain::make_terrain_surface_field(source, bounds, nullptr, 65);
        require(flat.weights == again.weights, "full-source product is nondeterministic");
        for (auto w : flat.weights)
            require(w.x == 0 && w.w == 0, "flat terrain invented exposed rock/snow");
        source.slope = .8F;
        const auto steep = terrain::make_terrain_surface_field(source, bounds, nullptr, 65);
        require(steep.sample({0, 0}).x > .8F, "exposed slope not classified as rock");
        for (auto w : steep.weights) {
            for (int c = 0; c < 4; ++c)
                require(std::isfinite(w[c]) && w[c] >= 0 && w[c] <= 1, "field weights invalid");
            require(w.x + w.w <= 1 && w.y <= 1 - w.x - w.w + 1e-6F, "ground budget exceeded");
        }
        // Two differently centred draw meshes/crops query identical source points.
        const math::Vec2 p{731, -540};
        const auto terrain_sample = steep.sample(p);
        const math::Vec2 crop_origin{511, -870}, cell{220, 330};
        require(terrain_sample == steep.sample(crop_origin + cell),
                "consumer coordinate mapping diverged");
        const auto expected =
            (steep.weights[0] + steep.weights[1] + steep.weights[65] + steep.weights[66]) * .25F;
        require(glm::length(steep.sample(bounds.minimum_xz + math::Vec2(64, 48)) - expected) <
                    1e-6F,
                "sample interpolation is not bilinear in source coordinates");
        require(steep.sample(bounds.minimum_xz) == steep.weights.front() &&
                    steep.sample(bounds.maximum_xz) == steep.weights.back(),
                "source edge contract changed");
        rejects([&] { (void)steep.sample({4097, 0}); });
        rejects([&] { (void)steep.sample({std::numeric_limits<float>::quiet_NaN(), 0}); });
        rejects([&] { (void)terrain::make_terrain_surface_field(source, bounds, nullptr, 1); });
        rejects([&] { (void)terrain::make_terrain_surface_field(source, bounds, nullptr, 1025); });
        rejects([&] {
            (void)terrain::make_terrain_surface_field(source, {{0, 0}, {0, 100}}, nullptr, 65);
        });
        source.nonfinite = true;
        rejects([&] { (void)terrain::make_terrain_surface_field(source, bounds, nullptr, 65); });
        std::cout << "source-coordinate surface contract PASS\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
