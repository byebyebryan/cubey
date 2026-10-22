#include "sdf_material.h"
#include <cmath>
#include <cubey/procedural/noise.h>
#include <random>
#include <stdexcept>
using namespace cubey::projects::sdf;
using cubey::math::Vec3;
namespace {
void require(bool value) {
    if (!value)
        throw std::runtime_error("material contract failed");
}
} // namespace
int main() {
    std::mt19937 random(781);
    std::uniform_real_distribution<float> coordinate(-2, 2);
    for (unsigned i = 0; i < 400; ++i) {
        const Vec3 p{coordinate(random), coordinate(random), coordinate(random)};
        const auto n = material_noise(p, i);
        const auto value = [&](Vec3 q) {
            return cubey::procedural::value_noise_3d(q.x, q.y, q.z, i);
        };
        require(std::abs(n.value - value(p)) < 2e-6F);
        for (int axis = 0; axis < 3; ++axis) {
            auto a = p, b = p;
            a[axis] += .0005F;
            b[axis] -= .0005F;
            require(std::abs(n.gradient[axis] - (value(a) - value(b)) / .001F) < .002F);
        }
    }
    const SurfaceMaterial base;
    MaterialPattern p;
    require(sample_material(base, p, {}).material.base_roughness == base.base_roughness);
    for (float kind : {1.0F, 2.0F}) {
        p.settings.x = kind;
        validate_material_pattern(p);
        for (unsigned i = 0; i < 100; ++i) {
            const Vec3 point{coordinate(random), coordinate(random), coordinate(random)};
            const auto sample = sample_material(base, p, point, .0005F);
            for (int c = 0; c < 4; ++c)
                require(std::isfinite(sample.material.base_roughness[c]) &&
                        sample.material.base_roughness[c] >= 0 &&
                        sample.material.base_roughness[c] <= 1);
            const Vec3 normal = glm::normalize(Vec3{.2F, 1, .3F});
            const auto shaded = material_shading_normal(normal, sample.height_gradient);
            require(glm::dot(normal, shaded) > .83F && std::abs(glm::length(shaded) - 1) < 1e-6F);
            const auto distant = sample_material(base, p, point, 100);
            require(glm::length(distant.height_gradient) == 0);
            require(distant.material.base_roughness.w == base.base_roughness.w);
            auto no_bump = p;
            no_bump.detail.x = 0;
            require(sample_material(base, no_bump, point, .0005F).material.base_roughness ==
                    sample.material.base_roughness);
        }
    }
    auto seed = p;
    seed.settings.y += 1;
    require(sample_material(base, p, {.13F, .07F, .29F}).height_gradient !=
            sample_material(base, seed, {.13F, .07F, .29F}).height_gradient);
    require(material_shading_normal({0, 1, 0}, {0, 0, 0}) == Vec3(0, 1, 0));
}
