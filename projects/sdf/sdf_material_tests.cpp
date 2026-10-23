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
    // Detail choices alter only five pattern controls, retaining the artist's
    // palette, bump amplitude, seed, orientation and color variation.
    p.offset.w = .08F;
    for (std::size_t i = 0; i < marble_details.size(); ++i) {
        auto preset = p;
        apply_marble_detail(preset, i);
        validate_material_pattern(preset);
        require(marble_detail_index(preset) == static_cast<int>(i));
        require(preset.settings.x == p.settings.x && preset.settings.y == p.settings.y &&
                preset.settings.w == p.settings.w && preset.secondary == p.secondary &&
                Vec3{preset.veins} == Vec3{p.veins} && preset.offset == p.offset &&
                preset.detail.x == p.detail.x);
        preset.veins.w += .01F;
        require(marble_detail_index(preset) == -1);
    }
    auto neutral = p;
    neutral.offset.w = 0;
    bool warm = false, untinted = false;
    for (unsigned i = 0; i < 200; ++i) {
        const Vec3 point{coordinate(random), coordinate(random), coordinate(random)};
        const auto a = sample_material(base, neutral, point, .0005F);
        const auto b = sample_material(base, p, point, .0005F);
        require(a.height_gradient == b.height_gradient &&
                a.material.base_roughness.w == b.material.base_roughness.w);
        const auto delta = Vec3{b.material.base_roughness - a.material.base_roughness};
        require(glm::length(delta) < .1F);
        // The uncolored end is the original material, never a cooler/darker tint.
        require(delta.x >= -1e-7F && delta.y <= 1e-7F && delta.z <= 1e-7F);
        warm |= delta.x > 0 && delta.z < 0;
        untinted |= glm::length(delta) < 1e-7F;
        require(sample_material(base, neutral, point, 100).material.base_roughness ==
                sample_material(base, p, point, 100).material.base_roughness);
        // Pocket boundaries broaden as they approach a pixel footprint. This
        // must retain the color-only, bounded, one-sided palette contract.
        for (float footprint : {.005F, .02F, .06F, .2F}) {
            const auto flat = sample_material(base, neutral, point, footprint);
            const auto filtered = sample_material(base, p, point, footprint);
            require(flat.height_gradient == filtered.height_gradient &&
                    flat.material.base_roughness.w == filtered.material.base_roughness.w);
            const auto change =
                Vec3{filtered.material.base_roughness - flat.material.base_roughness};
            require(glm::length(change) < .1F && change.x >= -1e-7F && change.y <= 1e-7F &&
                    change.z <= 1e-7F);
            for (int channel = 0; channel < 3; ++channel) {
                const float value = filtered.material.base_roughness[channel];
                require(std::isfinite(value) && value >= 0 && value <= 1);
            }
        }
    }
    require(warm && untinted);
}
