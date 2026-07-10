#include "upland_broad_noise_source.h"

#include "terrain_patch.h"

#include <cubey/procedural/operators.h>
#include <cubey/procedural/seed.h>
#include <cubey/procedural/source_fields.h>

#include <algorithm>
#include <string>
#include <string_view>
#include <utility>

namespace cubey::projects::terrain {
namespace {

[[nodiscard]] cubey::procedural::NoiseSource2D
coherent_fbm_source(std::uint64_t seed, std::string_view domain, float wavelength_m,
                    std::uint32_t octaves, float gain) {
    return {
        .backend = cubey::procedural::NoiseSource2DBackend::CoherentNoise,
        .output = cubey::procedural::NoiseSource2DOutput::Unit,
        .seed = cubey::procedural::derive_seed(seed, domain),
        .coherent =
            {
                .frequency = 1.0F / wavelength_m,
                .noise_type = cubey::procedural::CoherentNoiseType::OpenSimplex2S,
                .fractal_type = cubey::procedural::CoherentFractalType::Fbm,
                .octaves = octaves,
                .lacunarity = 2.0F,
                .gain = gain,
            },
    };
}

} // namespace

cubey::procedural::FieldSet2D
sample_upland_broad_noise_fields_v1(cubey::procedural::Grid2DDesc desc, std::uint64_t seed) {
    cubey::procedural::NoiseSource2D uplift_source =
        coherent_fbm_source(seed, "terrain.upland-broad-noise.uplift", 24'000.0F, 4U, 0.50F);
    cubey::procedural::NoiseSource2D mass_source =
        coherent_fbm_source(seed, "terrain.upland-broad-noise.mass", 9'000.0F, 5U, 0.50F);
    mass_source.warp = {
        .enabled = true,
        .seed_offset = 1'603U,
        .coherent =
            {
                .frequency = 1.0F / 18'000.0F,
                .warp_type = cubey::procedural::CoherentDomainWarpType::OpenSimplex2Reduced,
                .fractal_type = cubey::procedural::CoherentDomainWarpFractalType::Progressive,
                .octaves = 3U,
                .lacunarity = 2.0F,
                .gain = 0.5F,
                .amplitude = 1'600.0F,
            },
    };
    const cubey::procedural::NoiseSource2D relief_source =
        coherent_fbm_source(seed, "terrain.upland-broad-noise.relief", 3'500.0F, 4U, 0.47F);

    cubey::procedural::ScalarField2D uplift =
        cubey::procedural::sample_noise_source_field_2d(desc, uplift_source);
    cubey::procedural::ScalarField2D mass_noise =
        cubey::procedural::sample_noise_source_field_2d(desc, mass_source);
    cubey::procedural::ScalarField2D relief =
        cubey::procedural::sample_noise_source_field_2d(desc, relief_source);
    cubey::procedural::ScalarField2D macro_mass(desc, 0.0F);
    cubey::procedural::ScalarField2D support(desc, 0.0F);
    cubey::procedural::ScalarField2D base_relief_m(desc, 0.0F);
    cubey::procedural::ScalarField2D source_height_m(desc, 0.0F);

    for (std::size_t index = 0U; index < uplift.sample_count(); ++index) {
        const float uplift_value = uplift.values()[index];
        const float mass_value = cubey::procedural::smoothstep(
            0.20F, 0.80F, (mass_noise.values()[index] * 0.65F) + (uplift_value * 0.35F));
        const float support_value =
            cubey::procedural::smoothstep(0.25F, 0.75F, uplift_value) * mass_value;
        const float relief_m = cubey::procedural::unit_to_signed(relief.values()[index]) * 450.0F *
                               (0.30F + (0.70F * support_value));
        macro_mass.values()[index] = mass_value;
        support.values()[index] = support_value;
        base_relief_m.values()[index] = relief_m;
        source_height_m.values()[index] = std::max(
            100.0F + (500.0F * uplift_value) + (1'500.0F * support_value) + relief_m, 0.0F);
    }

    cubey::procedural::FieldSet2D result(desc);
    result.add_field(std::string(kTerrainFieldUpliftPotential), std::move(uplift));
    result.add_field(std::string(kTerrainFieldMacroMass), std::move(macro_mass));
    result.add_field(std::string(kTerrainFieldBaseReliefM), std::move(base_relief_m));
    result.add_field(std::string(kTerrainFieldMountainSupport), std::move(support));
    result.add_field(std::string(kTerrainFieldSourceHeightM), std::move(source_height_m));
    return result;
}

} // namespace cubey::projects::terrain
