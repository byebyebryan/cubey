#include "terrain_generator.h"

#include <cubey/procedural/operators.h>
#include <cubey/procedural/source_fields.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numeric>
#include <string>
#include <utility>
#include <vector>

namespace cubey::projects::terrain {
namespace {

struct FlowRoutingResult {
    cubey::procedural::ScalarField2D flow_direction{};
    std::vector<int> downstream{};
};

struct RiverFields {
    cubey::procedural::ScalarField2D flow_direction{};
    cubey::procedural::ScalarField2D flow_accumulation{};
    cubey::procedural::ScalarField2D stream_order{};
    cubey::procedural::ScalarField2D river_mask{};
    cubey::procedural::ScalarField2D channel_width{};
    cubey::procedural::ScalarField2D valley_width{};
    cubey::procedural::ScalarField2D wetness{};
    cubey::procedural::ScalarField2D deposition{};
};

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

[[nodiscard]] FlowRoutingResult route_steepest_descent(
    const cubey::procedural::ScalarField2D& height) {
    const cubey::procedural::Grid2DDesc& desc = height.desc();
    FlowRoutingResult result{
        .flow_direction = cubey::procedural::ScalarField2D(desc, -1.0F),
        .downstream = std::vector<int>(height.sample_count(), -1),
    };

    constexpr std::array<int, 8> kOffsetX{-1, 0, 1, -1, 1, -1, 0, 1};
    constexpr std::array<int, 8> kOffsetY{-1, -1, -1, 0, 0, 1, 1, 1};

    for (std::uint32_t y = 0; y < desc.height; ++y) {
        for (std::uint32_t x = 0; x < desc.width; ++x) {
            float best_drop_per_meter = 0.0F;
            int best_neighbor = -1;
            int best_direction = -1;
            const float center_height = height.at(x, y);

            for (std::size_t direction = 0; direction < kOffsetX.size(); ++direction) {
                const int nx = static_cast<int>(x) + kOffsetX[direction];
                const int ny = static_cast<int>(y) + kOffsetY[direction];
                if (nx < 0 || ny < 0 || nx >= static_cast<int>(desc.width) ||
                    ny >= static_cast<int>(desc.height)) {
                    continue;
                }
                const bool diagonal = kOffsetX[direction] != 0 && kOffsetY[direction] != 0;
                const float distance = diagonal ? desc.cell_size * 1.41421356F : desc.cell_size;
                const float drop =
                    center_height -
                    height.at(static_cast<std::uint32_t>(nx), static_cast<std::uint32_t>(ny));
                const float drop_per_meter = drop / distance;
                if (drop_per_meter > best_drop_per_meter) {
                    best_drop_per_meter = drop_per_meter;
                    best_neighbor = static_cast<int>(cubey::procedural::grid_index(
                        static_cast<std::uint32_t>(nx), static_cast<std::uint32_t>(ny),
                        desc.width));
                    best_direction = static_cast<int>(direction);
                }
            }

            result.flow_direction.at(x, y) = static_cast<float>(best_direction);
            result.downstream[height.index(x, y)] = best_neighbor;
        }
    }

    return result;
}

[[nodiscard]] cubey::procedural::ScalarField2D accumulate_flow(
    const cubey::procedural::ScalarField2D& height, const std::vector<int>& downstream) {
    const cubey::procedural::Grid2DDesc& desc = height.desc();
    cubey::procedural::ScalarField2D accumulation(desc, 1.0F);
    std::vector<std::size_t> order(height.sample_count());
    std::iota(order.begin(), order.end(), 0U);
    std::sort(order.begin(), order.end(), [&height](std::size_t lhs, std::size_t rhs) {
        return height.values()[lhs] > height.values()[rhs];
    });

    std::span<float> accumulation_values = accumulation.values();
    for (std::size_t index : order) {
        const int target = downstream[index];
        if (target >= 0) {
            accumulation_values[static_cast<std::size_t>(target)] += accumulation_values[index];
        }
    }

    return accumulation;
}

[[nodiscard]] RiverFields make_river_fields(const cubey::procedural::ScalarField2D& height,
                                            const cubey::procedural::ScalarField2D& slope) {
    const cubey::procedural::Grid2DDesc& desc = height.desc();
    const FlowRoutingResult routing = route_steepest_descent(height);
    RiverFields fields{
        .flow_direction = routing.flow_direction,
        .flow_accumulation = accumulate_flow(height, routing.downstream),
        .stream_order = cubey::procedural::ScalarField2D(desc, 1.0F),
        .river_mask = cubey::procedural::ScalarField2D(desc, 0.0F),
        .channel_width = cubey::procedural::ScalarField2D(desc, 0.0F),
        .valley_width = cubey::procedural::ScalarField2D(desc, 0.0F),
        .wetness = cubey::procedural::ScalarField2D(desc, 0.0F),
        .deposition = cubey::procedural::ScalarField2D(desc, 0.0F),
    };
    fields.flow_accumulation =
        cubey::procedural::box_blur_3x3(cubey::procedural::box_blur_3x3(fields.flow_accumulation));

    const float max_accumulation = std::max(fields.flow_accumulation.summarize().max, 1.0F);
    const float log_max_accumulation = std::log1p(max_accumulation);

    for (std::uint32_t y = 0; y < desc.height; ++y) {
        for (std::uint32_t x = 0; x < desc.width; ++x) {
            const float accumulation = fields.flow_accumulation.at(x, y);
            const float discharge =
                log_max_accumulation <= 0.0F ? 0.0F : std::log1p(accumulation) /
                                                        log_max_accumulation;
            const float river_strength = cubey::procedural::smoothstep(0.62F, 0.82F, discharge);
            const float order = 1.0F + (discharge > 0.42F ? 1.0F : 0.0F) +
                                (discharge > 0.56F ? 1.0F : 0.0F) +
                                (discharge > 0.70F ? 1.0F : 0.0F) +
                                (discharge > 0.84F ? 1.0F : 0.0F);
            fields.stream_order.at(x, y) = order;
            fields.river_mask.at(x, y) = river_strength;
            fields.channel_width.at(x, y) =
                river_strength * cubey::procedural::lerp(3.0F, 42.0F, discharge * discharge);
            fields.valley_width.at(x, y) =
                river_strength * cubey::procedural::lerp(45.0F, 310.0F,
                                                         std::pow(discharge, 1.45F));
        }
    }

    constexpr int kWetnessRadius = 12;
    for (std::uint32_t y = 0; y < desc.height; ++y) {
        for (std::uint32_t x = 0; x < desc.width; ++x) {
            float river_influence = 0.0F;
            float nearest_valley_width = 0.0F;
            for (int oy = -kWetnessRadius; oy <= kWetnessRadius; ++oy) {
                const int sy = static_cast<int>(y) + oy;
                if (sy < 0 || sy >= static_cast<int>(desc.height)) {
                    continue;
                }
                for (int ox = -kWetnessRadius; ox <= kWetnessRadius; ++ox) {
                    const int sx = static_cast<int>(x) + ox;
                    if (sx < 0 || sx >= static_cast<int>(desc.width)) {
                        continue;
                    }
                    const float river = fields.river_mask.at(static_cast<std::uint32_t>(sx),
                                                             static_cast<std::uint32_t>(sy));
                    if (river <= 0.0F) {
                        continue;
                    }
                    const float valley_width =
                        std::max(fields.valley_width.at(static_cast<std::uint32_t>(sx),
                                                        static_cast<std::uint32_t>(sy)),
                                 desc.cell_size * 1.5F);
                    const float distance = std::sqrt(static_cast<float>((ox * ox) + (oy * oy))) *
                                           desc.cell_size;
                    const float influence = river * std::exp(-distance / valley_width);
                    if (influence > river_influence) {
                        river_influence = influence;
                        nearest_valley_width = valley_width;
                    }
                }
            }

            const float slope_dryness = cubey::procedural::smoothstep(0.08F, 0.42F, slope.at(x, y));
            const float background_wetness = cubey::procedural::smoothstep(
                0.55F, 0.88F,
                std::log1p(fields.flow_accumulation.at(x, y)) / log_max_accumulation);
            const float wetness =
                cubey::procedural::saturate((river_influence * 0.85F) +
                                            (background_wetness * 0.25F) - (slope_dryness * 0.22F));
            fields.wetness.at(x, y) = wetness;
            fields.deposition.at(x, y) =
                wetness * (1.0F - cubey::procedural::smoothstep(0.04F, 0.30F, slope.at(x, y))) *
                cubey::procedural::saturate(nearest_valley_width / 260.0F);
        }
    }

    return RiverFields{
        .flow_direction = std::move(fields.flow_direction),
        .flow_accumulation = std::move(fields.flow_accumulation),
        .stream_order = std::move(fields.stream_order),
        .river_mask = std::move(fields.river_mask),
        .channel_width = std::move(fields.channel_width),
        .valley_width = std::move(fields.valley_width),
        .wetness = std::move(fields.wetness),
        .deposition = std::move(fields.deposition),
    };
}

[[nodiscard]] cubey::procedural::ScalarField2D make_material_field(
    const cubey::procedural::ScalarField2D& height, const cubey::procedural::ScalarField2D& slope,
    const cubey::procedural::ScalarField2D& wetness,
    const cubey::procedural::ScalarField2D& ridge_uplift, std::string_view material) {
    const cubey::procedural::Grid2DDesc& desc = height.desc();
    const cubey::procedural::ScalarFieldStats height_stats = height.summarize();
    const float height_span = std::max(height_stats.span, 1.0F);
    cubey::procedural::ScalarField2D result(desc, 0.0F);
    for (std::uint32_t y = 0; y < desc.height; ++y) {
        for (std::uint32_t x = 0; x < desc.width; ++x) {
            const float elevation = (height.at(x, y) - height_stats.min) / height_span;
            const float steep = cubey::procedural::smoothstep(0.16F, 0.62F, slope.at(x, y));
            const float ridge = cubey::procedural::smoothstep(80.0F, 300.0F, ridge_uplift.at(x, y));
            const float moist = wetness.at(x, y);
            const float rock = cubey::procedural::saturate((steep * 0.70F) + (ridge * 0.45F) +
                                                           (elevation * 0.18F) -
                                                           (moist * 0.20F));
            const float grass = cubey::procedural::saturate((1.0F - steep) * moist *
                                                            (1.0F - ridge * 0.65F));
            const float soil = cubey::procedural::saturate((1.0F - rock) * (1.0F - grass * 0.35F));
            const float sum = std::max(rock + soil + grass, 0.0001F);
            if (material == kTerrainFieldMaterialRock) {
                result.at(x, y) = rock / sum;
            } else if (material == kTerrainFieldMaterialGrass) {
                result.at(x, y) = grass / sum;
            } else {
                result.at(x, y) = soil / sum;
            }
        }
    }
    return result;
}

[[nodiscard]] cubey::procedural::ScalarField2D make_vegetation_potential_field(
    const cubey::procedural::ScalarField2D& slope,
    const cubey::procedural::ScalarField2D& wetness,
    const cubey::procedural::ScalarField2D& material_grass) {
    cubey::procedural::ScalarField2D result(slope.desc(), 0.0F);
    for (std::uint32_t y = 0; y < slope.desc().height; ++y) {
        for (std::uint32_t x = 0; x < slope.desc().width; ++x) {
            const float slope_limit = 1.0F - cubey::procedural::smoothstep(0.20F, 0.70F,
                                                                           slope.at(x, y));
            result.at(x, y) =
                cubey::procedural::saturate((material_grass.at(x, y) * 0.72F) +
                                            (wetness.at(x, y) * 0.22F)) *
                slope_limit;
        }
    }
    return result;
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
    RiverFields river_fields = make_river_fields(height, slope_curvature.slope);
    cubey::procedural::ScalarField2D material_rock =
        make_material_field(height, slope_curvature.slope, river_fields.wetness, ridge_uplift,
                            kTerrainFieldMaterialRock);
    cubey::procedural::ScalarField2D material_soil =
        make_material_field(height, slope_curvature.slope, river_fields.wetness, ridge_uplift,
                            kTerrainFieldMaterialSoil);
    cubey::procedural::ScalarField2D material_grass =
        make_material_field(height, slope_curvature.slope, river_fields.wetness, ridge_uplift,
                            kTerrainFieldMaterialGrass);
    cubey::procedural::ScalarField2D vegetation_potential =
        make_vegetation_potential_field(slope_curvature.slope, river_fields.wetness,
                                        material_grass);

    add_field(product.fields, kTerrainFieldBaseElevation, std::move(base_elevation));
    add_field(product.fields, kTerrainFieldBroadRelief, std::move(broad_relief));
    add_field(product.fields, kTerrainFieldRidgeUplift, std::move(ridge_uplift));
    add_field(product.fields, kTerrainFieldDetailResidual, std::move(detail_residual));
    add_field(product.fields, kTerrainFieldHeightM, std::move(height));
    add_field(product.fields, kTerrainFieldSlope, slope_curvature.slope);
    add_field(product.fields, kTerrainFieldCurvature, slope_curvature.curvature);
    add_field(product.fields, kTerrainFieldLocalRelief, local_relief.local_span);
    add_field(product.fields, kTerrainFieldFlowDirection, std::move(river_fields.flow_direction));
    add_field(product.fields, kTerrainFieldFlowAccumulation,
              std::move(river_fields.flow_accumulation));
    add_field(product.fields, kTerrainFieldStreamOrder, std::move(river_fields.stream_order));
    add_field(product.fields, kTerrainFieldRiverMask, std::move(river_fields.river_mask));
    add_field(product.fields, kTerrainFieldChannelWidth, std::move(river_fields.channel_width));
    add_field(product.fields, kTerrainFieldValleyWidth, std::move(river_fields.valley_width));
    add_field(product.fields, kTerrainFieldWetness, std::move(river_fields.wetness));
    add_field(product.fields, kTerrainFieldDeposition, std::move(river_fields.deposition));
    add_field(product.fields, kTerrainFieldMaterialRock, std::move(material_rock));
    add_field(product.fields, kTerrainFieldMaterialSoil, std::move(material_soil));
    add_field(product.fields, kTerrainFieldMaterialGrass, std::move(material_grass));
    add_field(product.fields, kTerrainFieldVegetationPotential, std::move(vegetation_potential));

    product.summary = summarize_terrain_region_product(product.config, product.fields);
    return product;
}

} // namespace cubey::projects::terrain
