#include "sdf_material.h"
#include <algorithm>
#include <cmath>
#include <cubey/procedural/noise.h>
#include <stdexcept>

namespace cubey::projects::sdf {
OpticalInterface optical_interface(math::Vec3 incident, math::Vec3 outward, bool inside,
                                   float ior) {
    const auto normal = inside ? -outward : outward;
    const float cosine = std::clamp(-glm::dot(incident, normal), 0.0F, 1.0F);
    const auto transmitted = glm::refract(incident, normal, inside ? ior : 1 / ior);
    const bool tir = glm::dot(transmitted, transmitted) < 1e-12F;
    const float f0 = std::pow((ior - 1) / (ior + 1), 2.0F);
    return {glm::reflect(incident, normal), transmitted,
            tir ? 1 : f0 + (1 - f0) * std::pow(1 - cosine, 5.0F), tir};
}
math::Vec3 optical_absorption(const MaterialOptics& optics, float distance) {
    return glm::exp(glm::log(math::Vec3{optics.attenuation}) * (distance / optics.settings.w));
}
bool default_optics(const MaterialOptics& optics) {
    const MaterialOptics defaults;
    return optics.settings == defaults.settings && optics.attenuation == defaults.attenuation;
}
void validate_material_optics(const MaterialOptics& optics) {
    for (int i = 0; i < 4; ++i)
        if (!std::isfinite(optics.settings[i]) || !std::isfinite(optics.attenuation[i]))
            throw std::invalid_argument("nonfinite material optics");
    if (optics.settings.x < 0 || optics.settings.x > 1 ||
        (optics.settings.y != 0 && optics.settings.y != 1) || optics.settings.z < 1 ||
        optics.settings.z > 2.5F || optics.settings.w <= 0 || optics.attenuation.w != 0)
        throw std::invalid_argument("invalid material optics");
    for (int i = 0; i < 3; ++i)
        if (optics.attenuation[i] <= 0 || optics.attenuation[i] > 1)
            throw std::invalid_argument("invalid material attenuation");
}

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
            // Noise selects deposits and varies their width; the existing
            // vein coordinates determine every warm core, shoulder and wisp.
            const float frequency = scale * 4;
            const auto fine = material_noise(p * frequency, seed + 9091U);
            const float fine_fade = fade(footprint, frequency);
            const auto fine_gradient = frequency * fine_fade * fine.gradient;
            const float edge_frequency = scale * 11;
            const auto edge_noise = material_noise(p * edge_frequency, seed + 10093U);
            const float edge_fade = fade(footprint, edge_frequency);
            const auto edge_gradient = edge_frequency * edge_fade * edge_noise.gradient;
            const float field = .65F * variation + .35F * fine.value * fine_fade;
            const auto field_gradient = .65F * variation_gradient + .35F * fine_gradient;
            const float selection_aa = std::max(.025F, footprint * glm::length(field_gradient));
            const float selected =
                glm::smoothstep(-.05F - selection_aa, .35F + selection_aa, field);
            const float deposit = .5F + .5F * field;
            const float growth = 1.8F + 8 * deposit * deposit * deposit * deposit;
            const float jitter = 1 + .5F * edge_noise.value * edge_fade;
            const float bank_width = std::min(.55F, width * growth * jitter);
            const auto width_gradient = 2.5F * pattern.detail.z * cloud * variation_gradient;
            const auto growth_gradient = 16 * deposit * deposit * deposit * field_gradient;
            // Using the uncapped derivative overestimates the footprint where
            // bank width is capped, conservatively broadening that boundary.
            const auto bank_gradient =
                width_gradient * growth * jitter +
                width * (growth_gradient * jitter + growth * .5F * edge_gradient);
            const float bank_aa = std::max(
                .025F, footprint * (glm::length(phase_gradient) + glm::length(bank_gradient)));
            const float shoulder =
                1 - glm::smoothstep(bank_width * .45F, bank_width + bank_aa, band);
            const float primary =
                (.85F * core + .8F * shoulder) * (1 - glm::smoothstep(.35F, 1.0F, coverage));
            const float wisps =
                .5F * secondary * (1 - glm::smoothstep(.35F, 1.0F, secondary_coverage));
            mineral = selected * std::clamp(primary + wisps, 0.0F, 1.0F) * variation_fade;
            // Region boundaries share the large-scale vein deformation, but
            // have their own support. Gray lines can cross cream or pale stone.
            const float warp = pattern.detail.w / pattern.veins.w;
            const auto q = p * scale + direction * (warp * macro.value);
            const auto region_point = q * 1.8F + direction * (1.6F * glm::dot(q, direction));
            const float region_frequency = scale * 3.4F * (1 + warp * glm::length(macro.gradient));
            const auto region_noise =
                material_noise(region_point + math::Vec3{5.3F, 1.7F, 9.1F}, seed + 12007U);
            const auto detail_point = q * 4.5F + direction * (13.5F * glm::dot(q, direction));
            const auto region_detail =
                material_noise(detail_point + math::Vec3{1.3F, 8.9F, 2.1F}, seed + 13001U);
            const float region_detail_fade = fade(footprint, region_frequency * (18.0F / 3.4F));
            const float region_field = .6F * region_noise.value +
                                       .3F * region_detail.value * region_detail_fade +
                                       .1F * edge_noise.value * edge_fade;
            const auto main_gradient =
                region_noise.gradient * 1.8F +
                direction * (1.6F * glm::dot(region_noise.gradient, direction));
            const auto detail_gradient =
                region_detail.gradient * 4.5F +
                direction * (13.5F * glm::dot(region_detail.gradient, direction));
            const auto stretched_gradient =
                .6F * main_gradient + (.3F * region_detail_fade) * detail_gradient;
            const auto region_gradient =
                scale * (stretched_gradient +
                         macro.gradient * (warp * glm::dot(stretched_gradient, direction))) +
                .1F * edge_gradient;
            const float region_aa = std::max(.025F, footprint * glm::length(region_gradient));
            const float region =
                glm::smoothstep(.08F - region_aa, .26F + region_aa, region_field) *
                (.3F + .7F * (.5F + .5F * region_detail.value * region_detail_fade)) *
                fade(footprint, region_frequency);
            mineral = 1 - (1 - .9F * region) * (1 - .55F * mineral);
        }
    }
    auto color = glm::mix(math::Vec3{base.base_roughness}, math::Vec3{pattern.secondary},
                          pattern.settings.w * mask);
    if (pattern.settings.x == 2 && pattern.offset.w > 0) {
        const auto palette = glm::mix(math::Vec3{1}, math::Vec3{1.13F, .83F, .47F}, mineral);
        const float tint = 4 * pattern.offset.w;
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
