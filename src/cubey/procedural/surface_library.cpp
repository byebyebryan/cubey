#include <algorithm>
#include <cmath>
#include <cubey/procedural/surface_material.h>

namespace cubey::procedural {
namespace {
constexpr float pi = 3.141592654F;
float fade(float footprint, float frequency) {
    return 1 - glm::smoothstep(.15F, .65F, footprint * frequency);
}
NoiseGradient noise(math::Vec3 p, math::Vec3 frequency, std::uint32_t seed, float footprint) {
    auto n = material_noise(p * frequency, seed);
    const float weight = fade(footprint, std::max({frequency.x, frequency.y, frequency.z}));
    return {n.value * weight, n.gradient * frequency * weight};
}
NoiseGradient smooth(float lo, float hi, NoiseGradient n) {
    const float t = std::clamp((n.value - lo) / (hi - lo), 0.0F, 1.0F);
    return {t * t * (3 - 2 * t), n.gradient * (6 * t * (1 - t) / (hi - lo))};
}
} // namespace
MaterialSample sample_library_surface(const SurfaceMaterial& base, const SurfaceLibrary& s,
                                      math::Vec3 point, float footprint) {
    MaterialSample result{base};
    result.sheen = s.sheen;
    result.rough_glass = s.optical_options.x;
    if (s.settings.x == 0)
        return result;
    const auto seed = static_cast<std::uint32_t>(s.settings.y);
    const float scale = s.settings.z, relief = s.settings.w;
    const auto p = point * scale;
    footprint *= scale;
    const auto macro = noise(p, math::Vec3{4}, seed, footprint);
    auto color = math::Vec3{base.base_roughness};
    float roughness = base.base_roughness.w;
    NoiseGradient height;
    if (s.settings.x == 1) {
        const float radius = std::sqrt(p.x * p.x + p.z * p.z + 1e-8F);
        const auto radial = math::Vec3{p.x, 0, p.z} / radius;
        const float phase =
            2 * pi * (radius * s.wood.ring_frequency + s.wood.distortion * macro.value);
        const auto phase_gradient =
            2 * pi * (radial * s.wood.ring_frequency + s.wood.distortion * macro.gradient);
        const float shaped = phase + .55F * std::sin(phase);
        const float band = .5F + .5F * std::sin(shaped);
        const float filter = fade(footprint, s.wood.ring_frequency * 1.8F + s.wood.distortion * 32);
        NoiseGradient ring{glm::mix(.3F, band * band * band * band, filter),
                           phase_gradient * (2 * band * band * band * std::cos(shaped) *
                                             (1 + .55F * std::cos(phase)) * filter)};
        const auto fiber =
            noise(p, {s.wood.grain_frequency, 3, s.wood.grain_frequency}, seed + 1013, footprint);
        const auto pores =
            smooth(.15F, .6F,
                   noise(p, {s.wood.grain_frequency * .8F, 14, s.wood.grain_frequency * .8F},
                         seed + 3001, footprint));
        const float pigment = std::clamp(.75F * ring.value + .12F * (.5F + .5F * fiber.value) +
                                             s.wood.pore_strength * .3F * pores.value,
                                         0.0F, 1.0F);
        color = glm::mix(color, math::Vec3{s.secondary}, pigment);
        roughness += .07F * ring.value + .025F * fiber.value;
        height = {relief *
                      (.25F * ring.value + .35F * fiber.value - s.wood.pore_strength * pores.value),
                  relief * (.25F * ring.gradient + .35F * fiber.gradient -
                            s.wood.pore_strength * pores.gradient)};
    } else if (s.settings.x == 2) {
        const auto patch = noise(p, math::Vec3{s.rust.patch_frequency}, seed, footprint);
        const auto edge = noise(p, math::Vec3{s.rust.patch_frequency * 3}, seed + 1013, footprint);
        const NoiseGradient field{patch.value + s.rust.edge_breakup * .35F * edge.value,
                                  patch.gradient + s.rust.edge_breakup * .35F * edge.gradient};
        const float threshold = .75F - 1.5F * s.rust.coverage;
        const float aa = std::max(.06F, footprint * s.rust.patch_frequency * 2);
        auto oxide = smooth(threshold - aa, threshold + aa, field);
        if (s.rust.coverage == 0 || s.rust.coverage == 1)
            oxide = {s.rust.coverage, math::Vec3{0}};
        const float filter = fade(footprint, s.rust.patch_frequency);
        oxide.value = glm::mix(s.rust.coverage, oxide.value, filter);
        oxide.gradient *= filter;
        const auto pits = noise(p, math::Vec3{130}, seed + 4013, footprint);
        const auto oxide_color =
            math::Vec3{s.secondary} * (1 + .25F * macro.value + .15F * pits.value);
        color = glm::mix(color, oxide_color, oxide.value);
        roughness = glm::mix(s.rust_roughness.x, s.rust_roughness.y, oxide.value) +
                    s.rust_roughness.z * oxide.value * pits.value;
        result.metallic = 1 - oxide.value;
        const float crust = .5F + .5F * s.rust.pit_strength * pits.value;
        height = {relief * oxide.value * crust,
                  relief * (oxide.gradient * crust +
                            oxide.value * .5F * s.rust.pit_strength * pits.gradient)};
    } else if (s.settings.x == 3) {
        const float frequency = 1 / s.weave.yarn_spacing;
        const float u = p.x * frequency, v = p.y * frequency;
        const float power = 2 / (s.weave.yarn_width * s.weave.yarn_width);
        const float x = .5F + .5F * std::cos(2 * pi * u), y = .5F + .5F * std::cos(2 * pi * v);
        const float a = std::pow(x, power), b = std::pow(y, power);
        const math::Vec3 ga{-power * std::pow(x, power - 1) * pi * frequency * std::sin(2 * pi * u),
                            0, 0};
        const math::Vec3 gb{
            0, -power * std::pow(y, power - 1) * pi * frequency * std::sin(2 * pi * v), 0};
        const float over = .5F + .5F * std::sin(pi * u) * std::sin(pi * v);
        const math::Vec3 go{.5F * pi * frequency * std::cos(pi * u) * std::sin(pi * v),
                            .5F * pi * frequency * std::sin(pi * u) * std::cos(pi * v), 0};
        const float filter = fade(footprint, frequency);
        const float yarn = glm::mix(.4F, glm::mix(a, b, over), filter);
        const auto yarn_gradient = (glm::mix(ga, gb, over) + (b - a) * go) * filter;
        const auto fiber = noise(p, {frequency * 8, frequency * 2, 8}, seed + 4013, footprint);
        const float variation = 1 + s.weave.irregularity * macro.value;
        color = glm::mix(color * .7F, math::Vec3{s.secondary}, .25F + .5F * yarn) * variation;
        roughness += .035F * fiber.value;
        height = {relief * (yarn * variation + .2F * s.weave.fiber_strength * fiber.value),
                  relief *
                      (yarn_gradient * variation + yarn * s.weave.irregularity * macro.gradient +
                       .2F * s.weave.fiber_strength * fiber.gradient)};
    } else {
        const auto fiber =
            noise(p,
                  {s.paper.fiber_frequency,
                   s.paper.fiber_frequency * (1 - .95F * s.paper.directionality), 12},
                  seed + 1013, footprint);
        const auto fleck_noise =
            noise(p, math::Vec3{s.paper.fiber_frequency * .15F}, seed + 3001, footprint);
        const auto fleck = smooth(.25F, .65F, fleck_noise);
        const float pigment = std::clamp(.08F + .12F * s.paper.fiber_strength * fiber.value +
                                             s.paper.fleck_strength * fleck.value,
                                         0.0F, 1.0F);
        color = glm::mix(color, math::Vec3{s.secondary}, pigment) * (1 + .035F * macro.value);
        height = {relief *
                      (s.paper.fiber_strength * fiber.value - s.paper.fleck_strength * fleck.value),
                  relief * (s.paper.fiber_strength * fiber.gradient -
                            s.paper.fleck_strength * fleck.gradient)};
    }
    result.material.base_roughness = {glm::clamp(color, math::Vec3{0}, math::Vec3{1}),
                                      std::clamp(roughness, .05F, 1.0F)};
    result.height = height.value;
    result.height_gradient = height.gradient * scale;
    return result;
}
MaterialSample sample_surface_domain(const SurfaceMaterial& base, const MaterialPattern& legacy,
                                     const SurfaceLibrary& library, const math::Mat4& domain,
                                     math::Vec3 p, float footprint, bool patterns) {
    if (library.settings.x == 0 || !patterns) {
        auto result = patterns ? sample_material_domain(base, legacy, domain, p, footprint)
                               : MaterialSample{base};
        result.sheen = library.sheen;
        result.rough_glass = library.optical_options.x;
        return result;
    }
    const math::Mat3 j{domain};
    const float bound =
        domain == math::Mat4{1}
            ? 1
            : std::sqrt(glm::dot(j[0], j[0]) + glm::dot(j[1], j[1]) + glm::dot(j[2], j[2]));
    auto result = sample_library_surface(base, library, math::Vec3{domain * math::Vec4{p, 1}},
                                         footprint * bound);
    result.height_gradient = glm::transpose(j) * result.height_gradient;
    return result;
}
} // namespace cubey::procedural
