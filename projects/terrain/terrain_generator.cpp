#include "terrain_generator.h"

#include <cubey/procedural/operators.h>
#include <cubey/procedural/source_fields.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <utility>

namespace cubey::projects::terrain {
namespace {

[[nodiscard]] cubey::procedural::NoiseSource2D coherent_source(std::uint64_t seed,
                                                               float frequency,
                                                               std::uint32_t octaves) {
    return cubey::procedural::NoiseSource2D{
        .backend = cubey::procedural::NoiseSource2DBackend::CoherentNoise,
        .output = cubey::procedural::NoiseSource2DOutput::Unit,
        .seed = seed,
        .coherent =
            {
                .frequency = frequency,
                .noise_type = cubey::procedural::CoherentNoiseType::OpenSimplex2S,
                .fractal_type = cubey::procedural::CoherentFractalType::Fbm,
                .octaves = octaves,
                .lacunarity = 2.0F,
                .gain = 0.48F,
            },
        .warp =
            {
                .enabled = true,
                .seed_offset = 7211U,
                .coherent =
                    {
                        .frequency = frequency * 0.42F,
                        .warp_type = cubey::procedural::CoherentDomainWarpType::OpenSimplex2,
                        .fractal_type =
                            cubey::procedural::CoherentDomainWarpFractalType::Progressive,
                        .octaves = 2U,
                        .lacunarity = 2.0F,
                        .gain = 0.5F,
                        .amplitude = 420.0F,
                    },
            },
    };
}

[[nodiscard]] cubey::procedural::ScalarField2D unit_source_field(
    cubey::procedural::Grid2DDesc desc, std::uint64_t seed, float frequency,
    std::uint32_t octaves) {
    cubey::procedural::ScalarField2D field =
        cubey::procedural::sample_noise_source_field_2d(desc,
                                                        coherent_source(seed, frequency, octaves));
    return cubey::procedural::percentile_remap_field(field, 0.03F, 0.97F, 0.0F, 1.0F);
}

[[nodiscard]] cubey::procedural::ScalarField2D ridge_source_field(
    cubey::procedural::Grid2DDesc desc, std::uint64_t seed) {
    cubey::procedural::ScalarField2D field = cubey::procedural::sample_noise_source_field_2d(
        desc,
        cubey::procedural::NoiseSource2D{
            .backend = cubey::procedural::NoiseSource2DBackend::CoherentNoise,
            .output = cubey::procedural::NoiseSource2DOutput::Unit,
            .seed = seed,
            .coherent =
                {
                    .frequency = 0.00062F,
                    .noise_type = cubey::procedural::CoherentNoiseType::OpenSimplex2,
                    .fractal_type = cubey::procedural::CoherentFractalType::Ridged,
                    .octaves = 5U,
                    .lacunarity = 2.12F,
                    .gain = 0.47F,
                    .weighted_strength = 0.18F,
                },
            .warp =
                {
                    .enabled = true,
                    .seed_offset = 9137U,
                    .coherent =
                        {
                            .frequency = 0.00018F,
                            .warp_type =
                                cubey::procedural::CoherentDomainWarpType::OpenSimplex2Reduced,
                            .fractal_type =
                                cubey::procedural::CoherentDomainWarpFractalType::Progressive,
                            .octaves = 3U,
                            .lacunarity = 2.0F,
                            .gain = 0.5F,
                            .amplitude = 900.0F,
                        },
                },
        });
    field = cubey::procedural::percentile_remap_field(field, 0.08F, 0.98F, 0.0F, 1.0F);
    return cubey::procedural::pow_unit_field(field, 1.55F);
}

[[nodiscard]] cubey::procedural::ScalarField2D make_base_elevation_field(
    cubey::procedural::Grid2DDesc desc, const cubey::procedural::ScalarField2D& broad_noise) {
    cubey::procedural::ScalarField2D result(desc, 0.0F);
    for (std::uint32_t y = 0; y < desc.height; ++y) {
        const float ny = static_cast<float>(y) / static_cast<float>(desc.height - 1U);
        for (std::uint32_t x = 0; x < desc.width; ++x) {
            const float nx = static_cast<float>(x) / static_cast<float>(desc.width - 1U);
            const float regional_tilt = 1.0F - cubey::procedural::saturate((nx * 0.45F) +
                                                                           (ny * 0.68F));
            const float coarse_variation = (broad_noise.at(x, y) - 0.5F) * 260.0F;
            result.at(x, y) = 180.0F + (regional_tilt * 720.0F) + coarse_variation;
        }
    }
    return cubey::procedural::box_blur_3x3(result);
}

[[nodiscard]] cubey::procedural::ScalarField2D scale_unit_field(
    const cubey::procedural::ScalarField2D& field, float min_value, float max_value) {
    cubey::procedural::ScalarField2D result(field.desc(), 0.0F);
    for (std::uint32_t y = 0; y < field.desc().height; ++y) {
        for (std::uint32_t x = 0; x < field.desc().width; ++x) {
            result.at(x, y) =
                cubey::procedural::lerp(min_value, max_value,
                                        cubey::procedural::saturate(field.at(x, y)));
        }
    }
    return result;
}

[[nodiscard]] cubey::procedural::ScalarField2D make_ridge_uplift_field(
    const cubey::procedural::ScalarField2D& ridge_support,
    const cubey::procedural::ScalarField2D& broad_relief) {
    cubey::procedural::ScalarField2D result(ridge_support.desc(), 0.0F);
    for (std::uint32_t y = 0; y < ridge_support.desc().height; ++y) {
        for (std::uint32_t x = 0; x < ridge_support.desc().width; ++x) {
            const float relief_gate = cubey::procedural::smoothstep(80.0F, 360.0F,
                                                                    broad_relief.at(x, y));
            result.at(x, y) = ridge_support.at(x, y) * relief_gate * 420.0F;
        }
    }
    return result;
}

[[nodiscard]] cubey::procedural::ScalarField2D add_height_fields(
    const cubey::procedural::ScalarField2D& base,
    const cubey::procedural::ScalarField2D& broad_relief,
    const cubey::procedural::ScalarField2D& ridge_uplift,
    const cubey::procedural::ScalarField2D& detail_residual) {
    cubey::procedural::ScalarField2D result(base.desc(), 0.0F);
    for (std::uint32_t y = 0; y < base.desc().height; ++y) {
        for (std::uint32_t x = 0; x < base.desc().width; ++x) {
            result.at(x, y) = base.at(x, y) + broad_relief.at(x, y) + ridge_uplift.at(x, y) +
                              detail_residual.at(x, y);
        }
    }
    return result;
}

void add_field(cubey::procedural::FieldSet2D& fields, std::string_view name,
               cubey::procedural::ScalarField2D field) {
    fields.add_field(std::string(name), std::move(field));
}

} // namespace

TerrainRegionProduct generate_terrain_region(const TerrainRegionConfig& config) {
    TerrainRegionProduct product = make_empty_terrain_region_product(config);
    const cubey::procedural::Grid2DDesc desc = product.fields.desc();

    const cubey::procedural::ScalarField2D broad_noise =
        unit_source_field(desc, config.seed + 101U, 0.00018F, 4U);
    cubey::procedural::ScalarField2D base_elevation =
        make_base_elevation_field(desc, broad_noise);
    cubey::procedural::ScalarField2D broad_relief =
        scale_unit_field(unit_source_field(desc, config.seed + 202U, 0.00042F, 5U), -80.0F, 360.0F);
    cubey::procedural::ScalarField2D ridge_uplift =
        make_ridge_uplift_field(ridge_source_field(desc, config.seed + 303U), broad_relief);
    cubey::procedural::ScalarField2D detail_residual =
        scale_unit_field(unit_source_field(desc, config.seed + 404U, 0.0018F, 4U), -38.0F, 52.0F);
    detail_residual = cubey::procedural::box_blur_3x3(detail_residual);
    cubey::procedural::ScalarField2D height =
        add_height_fields(base_elevation, broad_relief, ridge_uplift, detail_residual);
    const cubey::procedural::SlopeCurvature2D slope_curvature =
        cubey::procedural::compute_slope_curvature(height);
    const cubey::procedural::LocalRelief2D local_relief =
        cubey::procedural::compute_local_relief(height, 4U);

    add_field(product.fields, kTerrainFieldBaseElevation, std::move(base_elevation));
    add_field(product.fields, kTerrainFieldBroadRelief, std::move(broad_relief));
    add_field(product.fields, kTerrainFieldRidgeUplift, std::move(ridge_uplift));
    add_field(product.fields, kTerrainFieldDetailResidual, std::move(detail_residual));
    add_field(product.fields, kTerrainFieldHeightM, std::move(height));
    add_field(product.fields, kTerrainFieldSlope, slope_curvature.slope);
    add_field(product.fields, kTerrainFieldCurvature, slope_curvature.curvature);
    add_field(product.fields, kTerrainFieldLocalRelief, local_relief.local_span);

    product.summary = summarize_terrain_region_product(product.config, product.fields);
    return product;
}

} // namespace cubey::projects::terrain
