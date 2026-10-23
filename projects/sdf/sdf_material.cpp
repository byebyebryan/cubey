#include "sdf_material.h"
#include <algorithm>
#include <cmath>
#include <cubey/procedural/noise.h>
#include <stdexcept>

namespace cubey::projects::sdf {
namespace {
float fade(float footprint, float frequency) {
    return 1 - glm::smoothstep(.15F, .65F, footprint * frequency);
}
NoiseGradient fractal(math::Vec3 p, std::uint32_t seed, float footprint) {
    NoiseGradient result;
    float frequency = 1, amplitude = .5F;
    for (unsigned i = 0; i < 3; ++i) {
        const auto n = material_noise(p * frequency, seed + i * 1013U);
        const float weight = amplitude * fade(footprint, frequency) / .875F;
        result.value += weight * n.value;
        result.gradient += weight * frequency * n.gradient;
        frequency *= 2.03F;
        amplitude *= .5F;
    }
    return result;
}
void range(float value, float low, float high) {
    if (!std::isfinite(value) || value < low || value > high)
        throw std::invalid_argument("procedural material parameter outside its finite range");
}
} // namespace

int marble_detail_index(const MaterialPattern& pattern) {
    if (pattern.settings.x != 2)
        return -1;
    for (std::size_t i = 0; i < marble_details.size(); ++i) {
        const auto& d = marble_details[i];
        if (pattern.settings.z == d.scale && pattern.veins.w == d.frequency &&
            pattern.detail.z == d.width && pattern.detail.w == d.warp &&
            pattern.detail.y == d.grain)
            return static_cast<int>(i);
    }
    return -1;
}
void apply_marble_detail(MaterialPattern& pattern, std::size_t index) {
    if (pattern.settings.x != 2)
        throw std::invalid_argument("marble detail preset requires a marble material");
    const auto& d = marble_details.at(index);
    pattern.settings.z = d.scale;
    pattern.veins.w = d.frequency;
    pattern.detail.z = d.width;
    pattern.detail.w = d.warp;
    pattern.detail.y = d.grain;
}

NoiseGradient material_noise(math::Vec3 p, std::uint32_t seed) {
    const auto cell = math::Vec3{glm::floor(p)};
    const auto t = p - cell;
    const auto s = t * t * t * (t * (t * 6.0F - 15.0F) + 10.0F);
    const auto ds = 30.0F * t * t * (t - 1.0F) * (t - 1.0F);
    const auto corner = [&](int x, int y, int z) {
        const auto h =
            procedural::hash_u32(static_cast<int>(cell.x) + x, static_cast<int>(cell.y) + y,
                                 static_cast<int>(cell.z) + z, seed);
        return static_cast<float>(h >> 8U) / 16777215.0F * 2 - 1;
    };
    const float a = corner(0, 0, 0), b = corner(1, 0, 0), c = corner(0, 1, 0), d = corner(1, 1, 0),
                e = corner(0, 0, 1), f = corner(1, 0, 1), g = corner(0, 1, 1), h = corner(1, 1, 1);
    const float x00 = glm::mix(a, b, s.x), x10 = glm::mix(c, d, s.x), x01 = glm::mix(e, f, s.x),
                x11 = glm::mix(g, h, s.x);
    const float y0 = glm::mix(x00, x10, s.y), y1 = glm::mix(x01, x11, s.y);
    return {glm::mix(y0, y1, s.z),
            {glm::mix(glm::mix(b - a, d - c, s.y), glm::mix(f - e, h - g, s.y), s.z) * ds.x,
             glm::mix(x10 - x00, x11 - x01, s.z) * ds.y, (y1 - y0) * ds.z}};
}

MaterialSample sample_material(const SurfaceMaterial& base, const MaterialPattern& pattern,
                               math::Vec3 point, float footprint) {
    MaterialSample result{base, {0, 0, 0}};
    if (pattern.settings.x == 0)
        return result;
    const auto seed = static_cast<std::uint32_t>(pattern.settings.y);
    const auto p = point + math::Vec3{pattern.offset};
    const float scale = pattern.settings.z;
    const auto macro = fractal(p * scale, seed, footprint * scale);
    float mask = .5F + .5F * macro.value, mineral = 0;
    if (pattern.settings.x == 2) {
        const auto direction = glm::normalize(math::Vec3{pattern.veins});
        const float phase =
            glm::dot(p * scale, direction) * pattern.veins.w + pattern.detail.w * macro.value;
        const auto phase_gradient =
            scale * (direction * pattern.veins.w + pattern.detail.w * macro.gradient);
        const auto variation_noise =
            material_noise(p * (scale * 2.3F) + math::Vec3{3.1F, 7.2F, 1.7F}, seed + 811U);
        const float variation_fade = fade(footprint, scale * 2.3F);
        const float variation = variation_noise.value * variation_fade;
        const auto variation_gradient = variation_noise.gradient * (scale * 2.3F * variation_fade);
        const float cloud = .5F + .5F * variation;
        const float width = pattern.detail.z * (.2F + 2.5F * cloud * cloud);
        const float coverage = footprint * glm::length(phase_gradient);
        const float aa = std::max(.025F, coverage);
        const float band = std::abs(std::sin(phase));
        const float core = 1 - glm::smoothstep(width * .15F, width + aa, band);
        const float halo = 1 - glm::smoothstep(width, width * 5 + aa, band);
        const float secondary_phase = phase * 1.93F + variation * 2.5F;
        const float secondary_coverage =
            footprint * glm::length(phase_gradient * 1.93F + variation_gradient * 2.5F);
        const float secondary_aa = std::max(.025F, secondary_coverage);
        float secondary = 1 - glm::smoothstep(width * .06F, width * .4F + secondary_aa,
                                              std::abs(std::sin(secondary_phase)));
        secondary =
            glm::mix(secondary, width * .146F, glm::smoothstep(.7F, 2.0F, secondary_coverage));
        mask = std::clamp((.65F * core + .18F * halo) * (.5F + .5F * cloud) + .16F * secondary +
                              .045F * cloud,
                          0.0F, 1.0F);
        mask = glm::mix(mask, std::clamp(width * .63662F + .025F, 0.0F, 1.0F),
                        glm::smoothstep(.7F, 2.0F, coverage));
        if (pattern.offset.w > 0) {
            // Smaller mineral pockets favor the same warped strata as the
            // veins. A second scale breaks up their edges and interior color.
            const float frequency = scale * 4;
            const auto pocket = material_noise(p * frequency, seed + 9091U);
            const auto fine = material_noise(p * (frequency * 2.43F), seed + 10093U);
            const float fine_fade = fade(footprint, frequency * 2.43F);
            const float field = .7F * pocket.value + .3F * fine.value * fine_fade - .3F * band;
            const auto field_gradient =
                frequency * (.7F * pocket.gradient + .3F * 2.43F * fine_fade * fine.gradient);
            const float edge = std::max(.065F, footprint * (glm::length(field_gradient) +
                                                            .3F * glm::length(phase_gradient)));
            mineral = glm::smoothstep(.015F - edge, .015F + edge, field) *
                      (.6F + .4F * glm::smoothstep(.015F, .65F, field)) *
                      fade(footprint, frequency);
        }
    }
    auto color = glm::mix(math::Vec3{base.base_roughness}, math::Vec3{pattern.secondary},
                          pattern.settings.w * mask);
    if (pattern.settings.x == 2 && pattern.offset.w > 0) {
        const auto palette = glm::mix(math::Vec3{1}, math::Vec3{1.13F, .83F, .47F}, mineral);
        const float tint = 4 * pattern.offset.w * (1 - mask);
        color *= glm::mix(math::Vec3{1}, palette, tint);
    }
    result.material.base_roughness = {
        glm::clamp(color, math::Vec3{0}, math::Vec3{1}),
        std::clamp(base.base_roughness.w + pattern.secondary.w * macro.value, .05F, 1.0F)};
    const float grain = pattern.detail.y;
    if (pattern.detail.x > 0) {
        const auto n = material_noise(p * grain, seed + 3001U);
        const auto fine = material_noise(p * (grain * 2.03F), seed + 4013U);
        result.height_gradient = pattern.detail.x * grain *
                                 (.7F * fade(footprint, grain) * n.gradient +
                                  .3F * 2.03F * fade(footprint, grain * 2.03F) * fine.gradient);
    }
    return result;
}

math::Vec3 material_shading_normal(math::Vec3 normal, math::Vec3 gradient) {
    if (glm::dot(gradient, gradient) == 0)
        return normal;
    auto tangent = gradient - normal * glm::dot(normal, gradient);
    tangent *= std::min(1.0F, .65F / std::max(glm::length(tangent), 1e-6F));
    return glm::normalize(normal - tangent);
}

void validate_material_pattern(const MaterialPattern& p) {
    range(p.settings.x, 0, 2);
    range(p.settings.y, 0, 65535);
    if (p.settings.x != std::floor(p.settings.x) || p.settings.y != std::floor(p.settings.y))
        throw std::invalid_argument("procedural material kind and seed must be integers");
    range(p.settings.z, .25F, 64);
    range(p.settings.w, 0, 1);
    for (int i = 0; i < 3; ++i) {
        range(p.secondary[i], 0, 1);
        range(p.veins[i], -10, 10);
        range(p.offset[i], -10, 10);
    }
    if (glm::length(math::Vec3{p.veins}) < 1e-5F)
        throw std::invalid_argument("procedural vein direction must be nonzero");
    range(p.secondary.w, 0, .5F);
    range(p.veins.w, .25F, 32);
    range(p.detail.x, 0, .005F);
    range(p.detail.y, 1, 512);
    range(p.detail.z, .02F, .5F);
    range(p.detail.w, 0, 12);
    range(p.offset.w, 0, .25F);
}
} // namespace cubey::projects::sdf
