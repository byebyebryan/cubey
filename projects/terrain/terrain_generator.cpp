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

enum class FlowRoutingMode {
    D8,
    DInfinity,
};

struct FlowReceiver {
    int index = -1;
    float weight = 0.0F;
};

struct FlowRoutingResult {
    FlowRoutingMode mode = FlowRoutingMode::D8;
    cubey::procedural::ScalarField2D flow_direction{};
    std::vector<int> downstream{};
    std::vector<std::array<FlowReceiver, 2>> receivers{};
};

struct RiverFields {
    cubey::procedural::ScalarField2D drainage_potential{};
    cubey::procedural::ScalarField2D flow_direction{};
    cubey::procedural::ScalarField2D flow_accumulation{};
    cubey::procedural::ScalarField2D stream_order{};
    cubey::procedural::ScalarField2D river_mask{};
    cubey::procedural::ScalarField2D river_trunk{};
    cubey::procedural::ScalarField2D tributaries{};
    cubey::procedural::ScalarField2D sink_mask{};
    cubey::procedural::ScalarField2D channel_width{};
    cubey::procedural::ScalarField2D valley_width{};
    cubey::procedural::ScalarField2D wetness{};
    cubey::procedural::ScalarField2D deposition{};
};

struct RiverNetworkSettings {
    float trunk_core_radius_cells = 1.4F;
    float trunk_falloff_radius_cells = 3.0F;
    float trunk_offset_cells = 2.25F;
    float corridor_trunk_stream_order = 4.0F;
    float corridor_tributary_stream_order = 2.0F;
    std::size_t corridor_count = 3U;
    std::size_t corridor_render_count = 1U;
    std::size_t corridor_seed_count = 128U;
    std::size_t corridor_branch_count = 6U;
    std::size_t corridor_min_visible_samples = 1U;
    std::size_t corridor_branch_min_visible_samples = 8U;
    float corridor_branch_max_active_overlap = 0.42F;
    std::size_t secondary_trunk_count = 0U;
    int secondary_trunk_min_distance_cells = 48;
    float order_seed_min_stream_order = 4.0F;
    float order_seed_trunk_stream_order = 4.6F;
    std::size_t order_seed_count = 5U;
    int order_seed_min_distance_cells = 28;
    int order_seed_contact_distance_cells = 5;
    std::size_t order_seed_min_visible_samples = 20U;
    float order_seed_max_active_overlap = 0.38F;
    float tributary_min_accumulation = 4.0F;
    float tributary_accumulation_fraction = 0.018F;
    std::size_t min_tributary_count = 2U;
    std::size_t tributary_count_divisor = 18U;
    std::size_t min_tributary_path_samples = 5U;
    float tributary_strength = 0.75F;
    float tributary_core_radius_cells = 0.9F;
    float tributary_falloff_radius_cells = 2.1F;
    float tributary_offset_cells = 0.75F;
};

struct TerrainSourceFields {
    cubey::procedural::ScalarField2D broad_noise{};
    cubey::procedural::ScalarField2D base_elevation{};
    cubey::procedural::ScalarField2D broad_relief{};
    cubey::procedural::ScalarField2D ridge_uplift{};
    cubey::procedural::ScalarField2D detail_residual{};
    cubey::procedural::ScalarField2D height{};
};

struct RoutingDomain {
    cubey::procedural::Grid2DDesc visible_desc{};
    cubey::procedural::Grid2DDesc hidden_desc{};
    std::uint32_t padding_x = 0U;
    std::uint32_t padding_y = 0U;
};

struct RoutingContext {
    RoutingDomain domain{};
    cubey::procedural::ScalarField2D routing_surface{};
    FlowRoutingResult d8_routing{};
    FlowRoutingResult routing{};
    cubey::procedural::ScalarField2D accumulation{};
    cubey::procedural::ScalarField2D stream_order{};
    cubey::procedural::ScalarField2D sink_mask{};
};

struct HiddenIndexBounds {
    std::uint32_t x_begin = 0U;
    std::uint32_t x_end = 0U;
    std::uint32_t y_begin = 0U;
    std::uint32_t y_end = 0U;
};

struct ChannelPathPoint {
    float x = 0.0F;
    float y = 0.0F;
    float strength = 1.0F;
};

struct RiverCorridor {
    int terminal_index = -1;
    std::vector<int> cells{};
};

struct RiverCorridorSelection {
    std::vector<bool> selected{};
    std::vector<bool> trunk_support{};
    std::vector<RiverCorridor> corridors{};
};

struct RiverCorridorComponent {
    int terminal_index = -1;
    std::vector<int> cells{};
    std::size_t visible_samples = 0U;
    std::size_t visible_core_samples = 0U;
    std::size_t trunk_visible_samples = 0U;
    float max_accumulation = 0.0F;
    bool touches_left = false;
    bool touches_right = false;
    bool touches_top = false;
    bool touches_bottom = false;
    float score = 0.0F;
};

struct FlowVector {
    float x = 0.0F;
    float y = 0.0F;
    bool valid = false;
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

[[nodiscard]] RiverNetworkSettings river_network_settings(std::string_view recipe_id) {
    if (recipe_id == kTerrainRecipeTemperateMountainRiverStress) {
        return RiverNetworkSettings{
            .trunk_core_radius_cells = 1.55F,
            .trunk_falloff_radius_cells = 3.5F,
            .trunk_offset_cells = 2.6F,
            .corridor_trunk_stream_order = 4.0F,
            .corridor_tributary_stream_order = 2.0F,
            .corridor_count = 3U,
            .corridor_render_count = 3U,
            .corridor_seed_count = 192U,
            .corridor_branch_count = 18U,
            .corridor_min_visible_samples = 16U,
            .corridor_branch_min_visible_samples = 6U,
            .corridor_branch_max_active_overlap = 0.50F,
            .secondary_trunk_count = 3U,
            .secondary_trunk_min_distance_cells = 42,
            .order_seed_min_stream_order = 3.0F,
            .order_seed_trunk_stream_order = 4.2F,
            .order_seed_count = 18U,
            .order_seed_min_distance_cells = 16,
            .order_seed_contact_distance_cells = 6,
            .order_seed_min_visible_samples = 12U,
            .order_seed_max_active_overlap = 0.48F,
            .tributary_min_accumulation = 2.0F,
            .tributary_accumulation_fraction = 0.006F,
            .min_tributary_count = 10U,
            .tributary_count_divisor = 5U,
            .min_tributary_path_samples = 4U,
            .tributary_strength = 0.78F,
            .tributary_core_radius_cells = 0.85F,
            .tributary_falloff_radius_cells = 2.35F,
            .tributary_offset_cells = 1.2F,
        };
    }
    return {};
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

[[nodiscard]] TerrainSourceFields make_terrain_source_fields(cubey::procedural::Grid2DDesc desc,
                                                             std::uint64_t seed) {
    TerrainSourceFields fields{
        .broad_noise = unit_source_field(desc, seed + 101U, 0.00018F, 4U),
    };
    fields.base_elevation = make_base_elevation_field(desc, fields.broad_noise);
    fields.broad_relief =
        scale_unit_field(unit_source_field(desc, seed + 202U, 0.00042F, 5U), -80.0F, 360.0F);
    fields.ridge_uplift =
        make_ridge_uplift_field(ridge_source_field(desc, seed + 303U), fields.broad_relief);
    fields.detail_residual =
        scale_unit_field(unit_source_field(desc, seed + 404U, 0.0018F, 4U), -38.0F, 52.0F);
    fields.detail_residual = cubey::procedural::box_blur_3x3(fields.detail_residual);
    fields.height = add_height_fields(fields.base_elevation, fields.broad_relief,
                                      fields.ridge_uplift, fields.detail_residual);
    return fields;
}

[[nodiscard]] cubey::procedural::ScalarField2D make_routing_source_height(
    cubey::procedural::Grid2DDesc desc, std::uint64_t seed) {
    const cubey::procedural::ScalarField2D broad_noise =
        unit_source_field(desc, seed + 101U, 0.00018F, 4U);
    cubey::procedural::ScalarField2D broad_relief =
        scale_unit_field(unit_source_field(desc, seed + 202U, 0.00042F, 5U), -80.0F, 360.0F);
    const cubey::procedural::ScalarField2D ridge_uplift =
        make_ridge_uplift_field(ridge_source_field(desc, seed + 303U), broad_relief);
    cubey::procedural::ScalarField2D result(desc, 0.0F);
    const cubey::procedural::ScalarField2D base_elevation =
        make_base_elevation_field(desc, broad_noise);
    for (std::uint32_t y = 0; y < desc.height; ++y) {
        for (std::uint32_t x = 0; x < desc.width; ++x) {
            result.at(x, y) =
                base_elevation.at(x, y) + broad_relief.at(x, y) + (ridge_uplift.at(x, y) * 0.55F);
        }
    }
    return result;
}

void add_field(cubey::procedural::FieldSet2D& fields, std::string_view name,
               cubey::procedural::ScalarField2D field) {
    fields.add_field(std::string(name), std::move(field));
}

[[nodiscard]] std::uint32_t routing_padding_cells(
    const cubey::procedural::Grid2DDesc& visible_desc) {
    const std::uint32_t base = std::min(visible_desc.width, visible_desc.height) / 4U;
    return std::clamp(base, 32U, 128U);
}

[[nodiscard]] RoutingDomain make_routing_domain(cubey::procedural::Grid2DDesc visible_desc) {
    const std::uint32_t padding = routing_padding_cells(visible_desc);
    cubey::procedural::Grid2DDesc hidden_desc = visible_desc;
    hidden_desc.width = visible_desc.width + (padding * 2U);
    hidden_desc.height = visible_desc.height + (padding * 2U);
    hidden_desc.origin_x = visible_desc.origin_x -
                           (static_cast<float>(padding) * visible_desc.cell_size);
    hidden_desc.origin_y = visible_desc.origin_y -
                           (static_cast<float>(padding) * visible_desc.cell_size);
    return RoutingDomain{
        .visible_desc = visible_desc,
        .hidden_desc = hidden_desc,
        .padding_x = padding,
        .padding_y = padding,
    };
}

[[nodiscard]] bool is_inside_visible_crop(const RoutingDomain& domain, int hidden_index) {
    if (hidden_index < 0) {
        return false;
    }
    const auto index = static_cast<std::uint32_t>(hidden_index);
    const std::uint32_t x = index % domain.hidden_desc.width;
    const std::uint32_t y = index / domain.hidden_desc.width;
    return x >= domain.padding_x && y >= domain.padding_y &&
           x < domain.padding_x + domain.visible_desc.width &&
           y < domain.padding_y + domain.visible_desc.height;
}

[[nodiscard]] cubey::procedural::ScalarField2D crop_hidden_field_to_visible(
    const cubey::procedural::ScalarField2D& hidden_field, const RoutingDomain& domain) {
    cubey::procedural::ScalarField2D visible(domain.visible_desc, 0.0F);
    for (std::uint32_t y = 0; y < domain.visible_desc.height; ++y) {
        for (std::uint32_t x = 0; x < domain.visible_desc.width; ++x) {
            visible.at(x, y) = hidden_field.at(x + domain.padding_x, y + domain.padding_y);
        }
    }
    return visible;
}

[[nodiscard]] HiddenIndexBounds visible_seed_bounds(const RoutingDomain& domain) {
    const std::uint32_t visible_min = std::min(domain.visible_desc.width, domain.visible_desc.height);
    std::uint32_t inset = std::max(8U, visible_min / 16U);
    if (domain.visible_desc.width <= (inset * 2U) + 1U ||
        domain.visible_desc.height <= (inset * 2U) + 1U) {
        inset = 0U;
    }
    return HiddenIndexBounds{
        .x_begin = domain.padding_x + inset,
        .x_end = domain.padding_x + domain.visible_desc.width - inset,
        .y_begin = domain.padding_y + inset,
        .y_end = domain.padding_y + domain.visible_desc.height - inset,
    };
}

[[nodiscard]] HiddenIndexBounds visible_crop_bounds(const RoutingDomain& domain) {
    return HiddenIndexBounds{
        .x_begin = domain.padding_x,
        .x_end = domain.padding_x + domain.visible_desc.width,
        .y_begin = domain.padding_y,
        .y_end = domain.padding_y + domain.visible_desc.height,
    };
}

inline constexpr float kPi = 3.14159265358979323846F;
inline constexpr float kTwoPi = kPi * 2.0F;
inline constexpr float kQuarterPi = kPi * 0.25F;
inline constexpr std::array<int, 8> kAngleOffsetX{1, 1, 0, -1, -1, -1, 0, 1};
inline constexpr std::array<int, 8> kAngleOffsetY{0, 1, 1, 1, 0, -1, -1, -1};

[[nodiscard]] bool neighbor_in_bounds(const cubey::procedural::Grid2DDesc& desc, int x, int y) {
    return x >= 0 && y >= 0 && x < static_cast<int>(desc.width) &&
           y < static_cast<int>(desc.height);
}

[[nodiscard]] float neighbor_distance_m(const cubey::procedural::Grid2DDesc& desc,
                                        int offset_x, int offset_y) {
    return offset_x != 0 && offset_y != 0 ? desc.cell_size * 1.41421356F : desc.cell_size;
}

[[nodiscard]] FlowReceiver steepest_receiver(const cubey::procedural::ScalarField2D& height,
                                             std::uint32_t x, std::uint32_t y) {
    const cubey::procedural::Grid2DDesc& desc = height.desc();
    const float center_height = height.at(x, y);
    float best_drop_per_meter = 0.0F;
    int best_neighbor = -1;
    for (int direction = 0; direction < static_cast<int>(kAngleOffsetX.size()); ++direction) {
        const auto direction_index = static_cast<std::size_t>(direction);
        const int offset_x = kAngleOffsetX[direction_index];
        const int offset_y = kAngleOffsetY[direction_index];
        const int nx = static_cast<int>(x) + offset_x;
        const int ny = static_cast<int>(y) + offset_y;
        if (!neighbor_in_bounds(desc, nx, ny)) {
            continue;
        }
        const float drop =
            center_height -
            height.at(static_cast<std::uint32_t>(nx), static_cast<std::uint32_t>(ny));
        const float drop_per_meter = drop / neighbor_distance_m(desc, offset_x, offset_y);
        if (drop_per_meter > best_drop_per_meter) {
            best_drop_per_meter = drop_per_meter;
            best_neighbor = static_cast<int>(cubey::procedural::grid_index(
                static_cast<std::uint32_t>(nx), static_cast<std::uint32_t>(ny), desc.width));
        }
    }
    return FlowReceiver{
        .index = best_neighbor,
        .weight = best_neighbor >= 0 ? 1.0F : 0.0F,
    };
}

void assign_fractional_receivers(FlowRoutingResult& result,
                                 const cubey::procedural::ScalarField2D& height,
                                 std::uint32_t x, std::uint32_t y, float angle) {
    const cubey::procedural::Grid2DDesc& desc = height.desc();
    const std::size_t index = height.index(x, y);
    const float normalized_angle = angle < 0.0F ? angle + kTwoPi : angle;
    const float sector_position = normalized_angle / kQuarterPi;
    const int sector = static_cast<int>(std::floor(sector_position)) % 8;
    const int next_sector = (sector + 1) % 8;
    const float next_weight = sector_position - std::floor(sector_position);
    const float sector_weight = 1.0F - next_weight;
    const float center_height = height.at(x, y);

    std::array<FlowReceiver, 2> receivers{};
    int receiver_count = 0;
    const auto try_add_receiver = [&](int direction, float weight) {
        if (weight <= 0.0001F || receiver_count >= 2) {
            return;
        }
        const auto direction_index = static_cast<std::size_t>(direction);
        const int nx = static_cast<int>(x) + kAngleOffsetX[direction_index];
        const int ny = static_cast<int>(y) + kAngleOffsetY[direction_index];
        if (!neighbor_in_bounds(desc, nx, ny)) {
            return;
        }
        const float target_height =
            height.at(static_cast<std::uint32_t>(nx), static_cast<std::uint32_t>(ny));
        if (target_height >= center_height) {
            return;
        }
        receivers[static_cast<std::size_t>(receiver_count)] = FlowReceiver{
            .index = static_cast<int>(cubey::procedural::grid_index(
                static_cast<std::uint32_t>(nx), static_cast<std::uint32_t>(ny), desc.width)),
            .weight = weight,
        };
        ++receiver_count;
    };

    try_add_receiver(sector, sector_weight);
    try_add_receiver(next_sector, next_weight);

    if (receiver_count == 0) {
        receivers[0] = steepest_receiver(height, x, y);
    } else if (receiver_count == 1) {
        receivers[0].weight = 1.0F;
    } else {
        const float sum = receivers[0].weight + receivers[1].weight;
        receivers[0].weight /= sum;
        receivers[1].weight /= sum;
    }

    result.receivers[index] = receivers;
    result.downstream[index] =
        receivers[1].weight > receivers[0].weight ? receivers[1].index : receivers[0].index;
}

[[nodiscard]] FlowRoutingResult route_steepest_descent(
    const cubey::procedural::ScalarField2D& height) {
    const cubey::procedural::Grid2DDesc& desc = height.desc();
    FlowRoutingResult result{
        .mode = FlowRoutingMode::D8,
        .flow_direction = cubey::procedural::ScalarField2D(desc, -1.0F),
        .downstream = std::vector<int>(height.sample_count(), -1),
        .receivers = std::vector<std::array<FlowReceiver, 2>>(height.sample_count()),
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
            const std::size_t index = height.index(x, y);
            result.downstream[index] = best_neighbor;
            if (best_neighbor >= 0) {
                result.receivers[index][0] = FlowReceiver{
                    .index = best_neighbor,
                    .weight = 1.0F,
                };
            }
        }
    }

    return result;
}

[[nodiscard]] FlowRoutingResult route_dinfinity_descent(
    const cubey::procedural::ScalarField2D& height) {
    const cubey::procedural::Grid2DDesc& desc = height.desc();
    FlowRoutingResult result{
        .mode = FlowRoutingMode::DInfinity,
        .flow_direction = cubey::procedural::ScalarField2D(desc, -1.0F),
        .downstream = std::vector<int>(height.sample_count(), -1),
        .receivers = std::vector<std::array<FlowReceiver, 2>>(height.sample_count()),
    };

    for (std::uint32_t y = 0; y < desc.height; ++y) {
        const std::uint32_t y0 = y == 0U ? y : y - 1U;
        const std::uint32_t y1 = std::min(y + 1U, desc.height - 1U);
        for (std::uint32_t x = 0; x < desc.width; ++x) {
            const std::uint32_t x0 = x == 0U ? x : x - 1U;
            const std::uint32_t x1 = std::min(x + 1U, desc.width - 1U);
            const float gradient_x = (height.at(x1, y) - height.at(x0, y)) /
                                     (static_cast<float>(x1 - x0 + (x1 == x0 ? 1U : 0U)) *
                                      desc.cell_size);
            const float gradient_y = (height.at(x, y1) - height.at(x, y0)) /
                                     (static_cast<float>(y1 - y0 + (y1 == y0 ? 1U : 0U)) *
                                      desc.cell_size);
            const float downhill_x = -gradient_x;
            const float downhill_y = -gradient_y;
            const float downhill_length =
                std::sqrt((downhill_x * downhill_x) + (downhill_y * downhill_y));
            if (downhill_length <= 0.000001F) {
                const FlowReceiver fallback = steepest_receiver(height, x, y);
                result.receivers[height.index(x, y)][0] = fallback;
                result.downstream[height.index(x, y)] = fallback.index;
                if (fallback.index >= 0) {
                    const auto receiver_index = static_cast<std::uint32_t>(fallback.index);
                    const float receiver_x = static_cast<float>(receiver_index % desc.width);
                    const float receiver_y = static_cast<float>(receiver_index / desc.width);
                    float fallback_angle =
                        std::atan2(receiver_y - static_cast<float>(y),
                                   receiver_x - static_cast<float>(x));
                    if (fallback_angle < 0.0F) {
                        fallback_angle += kTwoPi;
                    }
                    result.flow_direction.at(x, y) = fallback_angle;
                }
                continue;
            }

            float angle = std::atan2(downhill_y, downhill_x);
            if (angle < 0.0F) {
                angle += kTwoPi;
            }
            result.flow_direction.at(x, y) = angle;
            assign_fractional_receivers(result, height, x, y, angle);
        }
    }

    return result;
}

[[nodiscard]] cubey::procedural::ScalarField2D make_drainage_routing_surface(
    const cubey::procedural::ScalarField2D& height, std::uint64_t seed) {
    const cubey::procedural::Grid2DDesc& desc = height.desc();
    cubey::procedural::ScalarField2D smoothed = cubey::procedural::box_blur_3x3(height);
    for (int pass = 0; pass < 17; ++pass) {
        smoothed = cubey::procedural::box_blur_3x3(smoothed);
    }
    cubey::procedural::ScalarField2D drainage_shape =
        unit_source_field(desc, seed + 707U, 0.00008F, 4U);
    for (int pass = 0; pass < 17; ++pass) {
        drainage_shape = cubey::procedural::box_blur_3x3(drainage_shape);
    }

    cubey::procedural::ScalarField2D result(desc, 0.0F);
    for (std::uint32_t y = 0; y < desc.height; ++y) {
        const float ny = static_cast<float>(y) / static_cast<float>(desc.height - 1U);
        for (std::uint32_t x = 0; x < desc.width; ++x) {
            const float nx = static_cast<float>(x) / static_cast<float>(desc.width - 1U);
            const float base_level_fall = ((nx * 0.30F) + (ny * 0.70F)) * 4200.0F;
            const float drainage_variation = (drainage_shape.at(x, y) - 0.5F) * 950.0F;
            result.at(x, y) = (smoothed.at(x, y) * 0.16F) + drainage_variation -
                              base_level_fall;
        }
    }
    return result;
}

[[nodiscard]] cubey::procedural::ScalarField2D accumulate_flow(
    const cubey::procedural::ScalarField2D& height,
    const std::vector<std::array<FlowReceiver, 2>>& receivers) {
    const cubey::procedural::Grid2DDesc& desc = height.desc();
    cubey::procedural::ScalarField2D accumulation(desc, 1.0F);
    std::vector<std::size_t> order(height.sample_count());
    std::iota(order.begin(), order.end(), 0U);
    std::sort(order.begin(), order.end(), [&height](std::size_t lhs, std::size_t rhs) {
        return height.values()[lhs] > height.values()[rhs];
    });

    std::span<float> accumulation_values = accumulation.values();
    for (std::size_t index : order) {
        for (const FlowReceiver& receiver : receivers[index]) {
            if (receiver.index >= 0 && receiver.weight > 0.0F) {
                accumulation_values[static_cast<std::size_t>(receiver.index)] +=
                    accumulation_values[index] * receiver.weight;
            }
        }
    }

    return accumulation;
}

[[nodiscard]] std::vector<std::vector<int>> make_upstream_adjacency(
    const std::vector<int>& downstream) {
    std::vector<std::vector<int>> upstream(downstream.size());
    for (std::size_t index = 0; index < downstream.size(); ++index) {
        const int target = downstream[index];
        if (target >= 0) {
            upstream[static_cast<std::size_t>(target)].push_back(static_cast<int>(index));
        }
    }
    return upstream;
}

[[nodiscard]] int select_best_accumulation_in_bounds(
    const cubey::procedural::ScalarField2D& accumulation, const RoutingDomain& domain,
    const HiddenIndexBounds& bounds) {
    int best_index = -1;
    float best_accumulation = -1.0F;
    for (std::uint32_t y = bounds.y_begin; y < bounds.y_end; ++y) {
        for (std::uint32_t x = bounds.x_begin; x < bounds.x_end; ++x) {
            const std::size_t index =
                cubey::procedural::grid_index(x, y, domain.hidden_desc.width);
            const float value = accumulation.values()[index];
            if (value > best_accumulation) {
                best_accumulation = value;
                best_index = static_cast<int>(index);
            }
        }
    }
    return best_index;
}

[[nodiscard]] int select_main_channel_seed(const cubey::procedural::ScalarField2D& accumulation,
                                           const RoutingDomain& domain) {
    int seed = select_best_accumulation_in_bounds(accumulation, domain,
                                                  visible_seed_bounds(domain));
    if (seed >= 0) {
        return seed;
    }
    seed = select_best_accumulation_in_bounds(accumulation, domain, visible_crop_bounds(domain));
    if (seed >= 0) {
        return seed;
    }

    int best_index = -1;
    float best_accumulation = -1.0F;
    for (std::size_t index = 0; index < accumulation.sample_count(); ++index) {
        const float value = accumulation.values()[index];
        if (value > best_accumulation) {
            best_accumulation = value;
            best_index = static_cast<int>(index);
        }
    }
    return best_index;
}

[[nodiscard]] std::vector<int> collect_seed_candidates(
    const cubey::procedural::ScalarField2D& accumulation, const RoutingDomain& domain,
    const HiddenIndexBounds& bounds, std::size_t max_count) {
    std::vector<int> candidates;
    candidates.reserve(static_cast<std::size_t>((bounds.x_end - bounds.x_begin) *
                                                (bounds.y_end - bounds.y_begin)));
    for (std::uint32_t y = bounds.y_begin; y < bounds.y_end; ++y) {
        for (std::uint32_t x = bounds.x_begin; x < bounds.x_end; ++x) {
            candidates.push_back(static_cast<int>(
                cubey::procedural::grid_index(x, y, domain.hidden_desc.width)));
        }
    }
    std::sort(candidates.begin(), candidates.end(), [&accumulation](int lhs, int rhs) {
        return accumulation.values()[static_cast<std::size_t>(lhs)] >
               accumulation.values()[static_cast<std::size_t>(rhs)];
    });
    if (candidates.size() > max_count) {
        candidates.resize(max_count);
    }
    return candidates;
}

void append_unique_candidates(std::vector<int>& candidates, std::vector<int> extra,
                              std::size_t max_total) {
    for (const int candidate : extra) {
        if (candidates.size() >= max_total) {
            break;
        }
        if (std::find(candidates.begin(), candidates.end(), candidate) == candidates.end()) {
            candidates.push_back(candidate);
        }
    }
}

[[nodiscard]] bool is_inside_visible_crop(const RoutingDomain& domain,
                                          const ChannelPathPoint& point) {
    return point.x >= static_cast<float>(domain.padding_x) &&
           point.y >= static_cast<float>(domain.padding_y) &&
           point.x < static_cast<float>(domain.padding_x + domain.visible_desc.width) &&
           point.y < static_cast<float>(domain.padding_y + domain.visible_desc.height);
}

[[nodiscard]] std::size_t visible_path_sample_count(const RoutingDomain& domain,
                                                    const std::vector<ChannelPathPoint>& path) {
    return static_cast<std::size_t>(std::count_if(path.begin(), path.end(), [&domain](auto point) {
        return is_inside_visible_crop(domain, point);
    }));
}

[[nodiscard]] std::size_t visible_path_edge_touch_count(
    const RoutingDomain& domain, const std::vector<ChannelPathPoint>& path) {
    bool left = false;
    bool right = false;
    bool top = false;
    bool bottom = false;
    const float x_min = static_cast<float>(domain.padding_x);
    const float x_max = static_cast<float>(domain.padding_x + domain.visible_desc.width - 1U);
    const float y_min = static_cast<float>(domain.padding_y);
    const float y_max = static_cast<float>(domain.padding_y + domain.visible_desc.height - 1U);
    for (const ChannelPathPoint& point : path) {
        const bool inside_y = point.y >= y_min && point.y <= y_max;
        const bool inside_x = point.x >= x_min && point.x <= x_max;
        left = left || (inside_y && point.x <= x_min + 1.5F);
        right = right || (inside_y && point.x >= x_max - 1.5F);
        top = top || (inside_x && point.y <= y_min + 1.5F);
        bottom = bottom || (inside_x && point.y >= y_max - 1.5F);
    }
    return static_cast<std::size_t>(left) + static_cast<std::size_t>(right) +
           static_cast<std::size_t>(top) + static_cast<std::size_t>(bottom);
}

[[nodiscard]] FlowVector sample_flow_vector_bilinear(
    const cubey::procedural::ScalarField2D& flow_direction, float x, float y) {
    const cubey::procedural::Grid2DDesc& desc = flow_direction.desc();
    x = std::clamp(x, 0.0F, static_cast<float>(desc.width - 1U));
    y = std::clamp(y, 0.0F, static_cast<float>(desc.height - 1U));
    const std::uint32_t x0 = static_cast<std::uint32_t>(std::floor(x));
    const std::uint32_t y0 = static_cast<std::uint32_t>(std::floor(y));
    const std::uint32_t x1 = std::min(x0 + 1U, desc.width - 1U);
    const std::uint32_t y1 = std::min(y0 + 1U, desc.height - 1U);
    const float tx = x - static_cast<float>(x0);
    const float ty = y - static_cast<float>(y0);

    float vector_x = 0.0F;
    float vector_y = 0.0F;
    float weight_sum = 0.0F;
    const auto accumulate = [&](std::uint32_t sx, std::uint32_t sy, float weight) {
        const float angle = flow_direction.at(sx, sy);
        if (angle < 0.0F || weight <= 0.0F) {
            return;
        }
        vector_x += std::cos(angle) * weight;
        vector_y += std::sin(angle) * weight;
        weight_sum += weight;
    };

    accumulate(x0, y0, (1.0F - tx) * (1.0F - ty));
    accumulate(x1, y0, tx * (1.0F - ty));
    accumulate(x0, y1, (1.0F - tx) * ty);
    accumulate(x1, y1, tx * ty);

    const float length = std::sqrt((vector_x * vector_x) + (vector_y * vector_y));
    if (weight_sum <= 0.0001F || length <= 0.0001F) {
        return {};
    }
    return FlowVector{
        .x = vector_x / length,
        .y = vector_y / length,
        .valid = true,
    };
}

[[nodiscard]] bool is_inside_hidden_domain(const cubey::procedural::Grid2DDesc& desc, float x,
                                           float y) {
    return x >= 0.0F && y >= 0.0F && x <= static_cast<float>(desc.width - 1U) &&
           y <= static_cast<float>(desc.height - 1U);
}

[[nodiscard]] int hidden_index_from_point(const cubey::procedural::Grid2DDesc& desc,
                                          const ChannelPathPoint& point) {
    const int x = static_cast<int>(std::round(point.x));
    const int y = static_cast<int>(std::round(point.y));
    if (!neighbor_in_bounds(desc, x, y)) {
        return -1;
    }
    return static_cast<int>(cubey::procedural::grid_index(static_cast<std::uint32_t>(x),
                                                         static_cast<std::uint32_t>(y),
                                                         desc.width));
}

[[nodiscard]] std::vector<int> path_point_indices(
    const cubey::procedural::Grid2DDesc& desc, const std::vector<ChannelPathPoint>& path) {
    std::vector<int> indices;
    indices.reserve(path.size());
    std::vector<bool> seen(static_cast<std::size_t>(desc.width) *
                               static_cast<std::size_t>(desc.height),
                           false);
    for (const ChannelPathPoint& point : path) {
        const int index = hidden_index_from_point(desc, point);
        if (index < 0 || seen[static_cast<std::size_t>(index)]) {
            continue;
        }
        seen[static_cast<std::size_t>(index)] = true;
        indices.push_back(index);
    }
    return indices;
}

[[nodiscard]] std::vector<ChannelPathPoint> make_channel_path_points(
    const cubey::procedural::Grid2DDesc& desc, const std::vector<int>& path, float strength) {
    std::vector<ChannelPathPoint> points;
    points.reserve(path.size());
    for (const int index : path) {
        if (index < 0) {
            continue;
        }
        const auto uindex = static_cast<std::uint32_t>(index);
        points.push_back(ChannelPathPoint{
            .x = static_cast<float>(uindex % desc.width),
            .y = static_cast<float>(uindex / desc.width),
            .strength = strength,
        });
    }
    return points;
}

[[nodiscard]] std::vector<ChannelPathPoint> trace_flow_streamline(
    int seed, const cubey::procedural::ScalarField2D& flow_direction, float direction_sign,
    float strength) {
    std::vector<ChannelPathPoint> points;
    if (seed < 0) {
        return points;
    }

    const cubey::procedural::Grid2DDesc& desc = flow_direction.desc();
    float x = static_cast<float>(static_cast<std::uint32_t>(seed) % desc.width);
    float y = static_cast<float>(static_cast<std::uint32_t>(seed) / desc.width);
    points.push_back(ChannelPathPoint{.x = x, .y = y, .strength = strength});

    constexpr float kStepSizeCells = 1.15F;
    const int max_steps = static_cast<int>((desc.width + desc.height) * 3U);
    for (int step = 0; step < max_steps; ++step) {
        const FlowVector flow = sample_flow_vector_bilinear(flow_direction, x, y);
        if (!flow.valid) {
            break;
        }
        const float next_x = x + (flow.x * direction_sign * kStepSizeCells);
        const float next_y = y + (flow.y * direction_sign * kStepSizeCells);
        if (!is_inside_hidden_domain(desc, next_x, next_y)) {
            break;
        }
        x = next_x;
        y = next_y;
        points.push_back(ChannelPathPoint{.x = x, .y = y, .strength = strength});
    }
    return points;
}

[[nodiscard]] std::vector<ChannelPathPoint> trace_channel_streamline(
    int seed, const cubey::procedural::ScalarField2D& flow_direction, float strength) {
    std::vector<ChannelPathPoint> upstream =
        trace_flow_streamline(seed, flow_direction, -1.0F, strength);
    std::vector<ChannelPathPoint> downstream =
        trace_flow_streamline(seed, flow_direction, 1.0F, strength);
    if (upstream.size() + downstream.size() < 5U) {
        return {};
    }

    std::reverse(upstream.begin(), upstream.end());
    std::vector<ChannelPathPoint> path = std::move(upstream);
    for (std::size_t index = downstream.empty() ? 0U : 1U; index < downstream.size(); ++index) {
        path.push_back(downstream[index]);
    }
    return path;
}

[[nodiscard]] bool path_intersects_visible_crop(const RoutingDomain& domain,
                                                const std::vector<int>& path) {
    return std::any_of(path.begin(), path.end(), [&domain](int index) {
        return is_inside_visible_crop(domain, index);
    });
}

[[nodiscard]] std::vector<int> trace_downstream_path(int start, const std::vector<int>& downstream) {
    std::vector<int> path;
    if (start < 0) {
        return path;
    }

    std::vector<bool> visited(downstream.size(), false);
    int current = start;
    while (current >= 0 && !visited[static_cast<std::size_t>(current)]) {
        visited[static_cast<std::size_t>(current)] = true;
        path.push_back(current);
        current = downstream[static_cast<std::size_t>(current)];
    }
    return path;
}

[[nodiscard]] std::size_t visible_grid_path_sample_count(const RoutingDomain& domain,
                                                         const std::vector<int>& path) {
    return static_cast<std::size_t>(std::count_if(path.begin(), path.end(), [&domain](int index) {
        return is_inside_visible_crop(domain, index);
    }));
}

[[nodiscard]] bool is_inside_visible_core(const RoutingDomain& domain, int hidden_index) {
    if (hidden_index < 0) {
        return false;
    }
    const auto index = static_cast<std::uint32_t>(hidden_index);
    const std::uint32_t x = index % domain.hidden_desc.width;
    const std::uint32_t y = index / domain.hidden_desc.width;
    const float x_min =
        static_cast<float>(domain.padding_x) + (static_cast<float>(domain.visible_desc.width) *
                                                0.18F);
    const float x_max =
        static_cast<float>(domain.padding_x) + (static_cast<float>(domain.visible_desc.width) *
                                                0.82F);
    const float y_min =
        static_cast<float>(domain.padding_y) + (static_cast<float>(domain.visible_desc.height) *
                                                0.18F);
    const float y_max =
        static_cast<float>(domain.padding_y) + (static_cast<float>(domain.visible_desc.height) *
                                                0.82F);
    return static_cast<float>(x) >= x_min && static_cast<float>(x) <= x_max &&
           static_cast<float>(y) >= y_min && static_cast<float>(y) <= y_max;
}

[[nodiscard]] bool is_inside_visible_core(const RoutingDomain& domain,
                                          const ChannelPathPoint& point) {
    const float x_min =
        static_cast<float>(domain.padding_x) + (static_cast<float>(domain.visible_desc.width) *
                                                0.18F);
    const float x_max =
        static_cast<float>(domain.padding_x) + (static_cast<float>(domain.visible_desc.width) *
                                                0.82F);
    const float y_min =
        static_cast<float>(domain.padding_y) + (static_cast<float>(domain.visible_desc.height) *
                                                0.18F);
    const float y_max =
        static_cast<float>(domain.padding_y) + (static_cast<float>(domain.visible_desc.height) *
                                                0.82F);
    return point.x >= x_min && point.x <= x_max && point.y >= y_min && point.y <= y_max;
}

[[nodiscard]] std::size_t visible_grid_path_core_sample_count(const RoutingDomain& domain,
                                                              const std::vector<int>& path) {
    return static_cast<std::size_t>(std::count_if(path.begin(), path.end(), [&domain](int index) {
        return is_inside_visible_core(domain, index);
    }));
}

[[nodiscard]] std::size_t visible_path_core_sample_count(
    const RoutingDomain& domain, const std::vector<ChannelPathPoint>& path) {
    return static_cast<std::size_t>(std::count_if(path.begin(), path.end(), [&domain](auto point) {
        return is_inside_visible_core(domain, point);
    }));
}

[[nodiscard]] std::size_t visible_grid_path_edge_touch_count(const RoutingDomain& domain,
                                                             const std::vector<int>& path) {
    bool left = false;
    bool right = false;
    bool top = false;
    bool bottom = false;
    for (const int path_index : path) {
        if (path_index < 0) {
            continue;
        }
        const auto index = static_cast<std::uint32_t>(path_index);
        const std::uint32_t x = index % domain.hidden_desc.width;
        const std::uint32_t y = index / domain.hidden_desc.width;
        const bool inside_y =
            y >= domain.padding_y && y < domain.padding_y + domain.visible_desc.height;
        const bool inside_x =
            x >= domain.padding_x && x < domain.padding_x + domain.visible_desc.width;
        left = left || (inside_y && x == domain.padding_x);
        right = right ||
                (inside_y && x == domain.padding_x + domain.visible_desc.width - 1U);
        top = top || (inside_x && y == domain.padding_y);
        bottom = bottom ||
                 (inside_x && y == domain.padding_y + domain.visible_desc.height - 1U);
    }
    return static_cast<std::size_t>(left) + static_cast<std::size_t>(right) +
           static_cast<std::size_t>(top) + static_cast<std::size_t>(bottom);
}

[[nodiscard]] std::size_t visible_grid_path_aligned_run(const RoutingDomain& domain,
                                                        const std::vector<int>& path) {
    if (path.size() < 2U) {
        return 0U;
    }

    int last_dx = 0;
    int last_dy = 0;
    std::size_t current = 0U;
    std::size_t best = 0U;
    for (std::size_t index = 1U; index < path.size(); ++index) {
        const int previous = path[index - 1U];
        const int next = path[index];
        if (!is_inside_visible_crop(domain, previous) && !is_inside_visible_crop(domain, next)) {
            current = 0U;
            last_dx = 0;
            last_dy = 0;
            continue;
        }

        const auto previous_index = static_cast<std::uint32_t>(previous);
        const auto next_index = static_cast<std::uint32_t>(next);
        const int previous_x = static_cast<int>(previous_index % domain.hidden_desc.width);
        const int previous_y = static_cast<int>(previous_index / domain.hidden_desc.width);
        const int next_x = static_cast<int>(next_index % domain.hidden_desc.width);
        const int next_y = static_cast<int>(next_index / domain.hidden_desc.width);
        const int step_x = next_x - previous_x;
        const int step_y = next_y - previous_y;
        if (std::abs(step_x) > 1 || std::abs(step_y) > 1 || (step_x == 0 && step_y == 0)) {
            current = 0U;
            last_dx = 0;
            last_dy = 0;
            continue;
        }

        const int dx = step_x == 0 ? 0 : (step_x > 0 ? 1 : -1);
        const int dy = step_y == 0 ? 0 : (step_y > 0 ? 1 : -1);
        if (dx == last_dx && dy == last_dy) {
            ++current;
        } else {
            current = 1U;
            last_dx = dx;
            last_dy = dy;
        }
        best = std::max(best, current);
    }
    return best;
}

[[nodiscard]] int strongest_upstream_candidate(
    int index, const std::vector<std::vector<int>>& upstream,
    const cubey::procedural::ScalarField2D& accumulation, const std::vector<bool>& excluded) {
    int best_index = -1;
    float best_accumulation = -1.0F;
    for (const int candidate : upstream[static_cast<std::size_t>(index)]) {
        if (excluded[static_cast<std::size_t>(candidate)]) {
            continue;
        }
        const float value = accumulation.values()[static_cast<std::size_t>(candidate)];
        if (value > best_accumulation) {
            best_accumulation = value;
            best_index = candidate;
        }
    }
    return best_index;
}

[[nodiscard]] std::vector<int> trace_strongest_upstream_path(
    int start, const std::vector<std::vector<int>>& upstream,
    const cubey::procedural::ScalarField2D& accumulation, float min_accumulation,
    const std::vector<bool>& excluded) {
    std::vector<int> path;
    if (start < 0) {
        return path;
    }

    std::vector<bool> visited(accumulation.sample_count(), false);
    int current = start;
    while (current >= 0 && !visited[static_cast<std::size_t>(current)]) {
        const float value = accumulation.values()[static_cast<std::size_t>(current)];
        if (value < min_accumulation) {
            break;
        }
        visited[static_cast<std::size_t>(current)] = true;
        path.push_back(current);
        current = strongest_upstream_candidate(current, upstream, accumulation, excluded);
    }
    return path;
}

[[nodiscard]] std::vector<int> trace_trunk_from_seed(
    int seed, const cubey::procedural::ScalarField2D& accumulation,
    const std::vector<std::vector<int>>& upstream, const std::vector<int>& downstream,
    const std::vector<bool>& excluded) {
    const std::vector<int> upstream_path =
        trace_strongest_upstream_path(seed, upstream, accumulation, 1.0F, excluded);
    std::vector<int> downstream_path = trace_downstream_path(seed, downstream);
    std::reverse(downstream_path.begin(), downstream_path.end());

    std::vector<int> trunk = std::move(downstream_path);
    const std::size_t upstream_start = trunk.empty() ? 0U : 1U;
    for (std::size_t index = upstream_start; index < upstream_path.size(); ++index) {
        trunk.push_back(upstream_path[index]);
    }
    return trunk;
}

[[nodiscard]] float score_grid_trunk(const cubey::procedural::ScalarField2D& accumulation,
                                     const RoutingDomain& domain,
                                     const std::vector<int>& trunk, int seed) {
    const std::size_t visible_samples = visible_grid_path_sample_count(domain, trunk);
    const float visible_length_gate =
        cubey::procedural::smoothstep(24.0F, 96.0F, static_cast<float>(visible_samples));
    const float edge_score =
        static_cast<float>(visible_grid_path_edge_touch_count(domain, trunk)) * 500'000.0F *
        visible_length_gate;
    const float length_score = static_cast<float>(visible_samples) * 20'000.0F;
    const float core_score =
        static_cast<float>(visible_grid_path_core_sample_count(domain, trunk)) * 15'000.0F;
    const float straight_penalty =
        std::pow(static_cast<float>(
                     std::max<std::size_t>(visible_grid_path_aligned_run(domain, trunk), 10U) -
                     10U),
                 2.0F) *
        20'000.0F;
    const float accumulation_score =
        seed >= 0 ? std::log1p(accumulation.values()[static_cast<std::size_t>(seed)]) : 0.0F;
    return edge_score + length_score + core_score + accumulation_score - straight_penalty;
}

[[nodiscard]] bool path_is_near_indices(const cubey::procedural::Grid2DDesc& desc,
                                        const std::vector<int>& path,
                                        const std::vector<int>& indices,
                                        int min_distance_cells) {
    if (min_distance_cells <= 0 || indices.empty()) {
        return false;
    }
    const int min_distance_sq = min_distance_cells * min_distance_cells;
    for (const int path_index : path) {
        if (path_index < 0) {
            continue;
        }
        const auto path_uindex = static_cast<std::uint32_t>(path_index);
        const int path_x = static_cast<int>(path_uindex % desc.width);
        const int path_y = static_cast<int>(path_uindex / desc.width);
        for (const int index : indices) {
            if (index < 0) {
                continue;
            }
            const auto uindex = static_cast<std::uint32_t>(index);
            const int dx = path_x - static_cast<int>(uindex % desc.width);
            const int dy = path_y - static_cast<int>(uindex / desc.width);
            if ((dx * dx) + (dy * dy) <= min_distance_sq) {
                return true;
            }
        }
    }
    return false;
}

void append_unique_indices(std::vector<int>& target, const std::vector<int>& indices) {
    for (const int index : indices) {
        if (std::find(target.begin(), target.end(), index) == target.end()) {
            target.push_back(index);
        }
    }
}

[[nodiscard]] std::vector<int> trace_main_trunk_graph(
    const cubey::procedural::ScalarField2D& accumulation,
    const std::vector<std::vector<int>>& upstream, const std::vector<int>& downstream,
    const RoutingDomain& domain) {
    const std::vector<bool> no_exclusions(accumulation.sample_count(), false);
    std::vector<int> candidates =
        collect_seed_candidates(accumulation, domain, visible_seed_bounds(domain), 768U);
    append_unique_candidates(candidates,
                             collect_seed_candidates(accumulation, domain,
                                                     visible_crop_bounds(domain), 768U),
                             1536U);
    if (candidates.empty()) {
        candidates = collect_seed_candidates(accumulation, domain, visible_crop_bounds(domain),
                                             1536U);
    }
    const int fallback_seed = select_main_channel_seed(accumulation, domain);
    if (candidates.empty() && fallback_seed >= 0) {
        candidates.push_back(fallback_seed);
    }

    std::vector<int> best_trunk;
    float best_score = -1.0F;
    for (const int seed : candidates) {
        std::vector<int> trunk =
            trace_trunk_from_seed(seed, accumulation, upstream, downstream, no_exclusions);
        if (trunk.empty()) {
            continue;
        }
        const float score = score_grid_trunk(accumulation, domain, trunk, seed);
        if (score > best_score) {
            best_score = score;
            best_trunk = std::move(trunk);
        }
    }
    return best_trunk;
}

[[nodiscard]] std::vector<std::vector<int>> trace_secondary_trunk_graphs(
    const cubey::procedural::ScalarField2D& accumulation,
    const std::vector<std::vector<int>>& upstream, const std::vector<int>& downstream,
    const RoutingDomain& domain, const std::vector<int>& initial_selected_indices,
    std::size_t trunk_count, int min_distance_cells) {
    std::vector<std::vector<int>> trunks;
    if (trunk_count == 0U) {
        return trunks;
    }

    const std::vector<bool> no_exclusions(accumulation.sample_count(), false);
    std::vector<int> candidates =
        collect_seed_candidates(accumulation, domain, visible_seed_bounds(domain), 768U);
    append_unique_candidates(candidates,
                             collect_seed_candidates(accumulation, domain,
                                                     visible_crop_bounds(domain), 768U),
                             1536U);

    std::vector<int> selected_indices = initial_selected_indices;
    for (std::size_t trunk_index = 0U; trunk_index < trunk_count; ++trunk_index) {
        std::vector<int> best_trunk;
        float best_score = -1.0F;
        for (const int seed : candidates) {
            std::vector<int> trunk =
                trace_trunk_from_seed(seed, accumulation, upstream, downstream, no_exclusions);
            if (visible_grid_path_sample_count(domain, trunk) < 40U) {
                continue;
            }
            if (path_is_near_indices(domain.hidden_desc, trunk, selected_indices,
                                     min_distance_cells)) {
                continue;
            }
            const float score = score_grid_trunk(accumulation, domain, trunk, seed);
            if (score > best_score) {
                best_score = score;
                best_trunk = std::move(trunk);
            }
        }
        if (best_trunk.empty()) {
            break;
        }
        append_unique_indices(selected_indices, best_trunk);
        trunks.push_back(std::move(best_trunk));
    }
    return trunks;
}

[[nodiscard]] std::vector<ChannelPathPoint> trace_main_trunk(
    const cubey::procedural::ScalarField2D& accumulation, const FlowRoutingResult& vector_routing,
    const FlowRoutingResult& graph_routing, const RoutingDomain& domain) {
    std::vector<int> candidates =
        collect_seed_candidates(accumulation, domain, visible_seed_bounds(domain), 768U);
    append_unique_candidates(candidates,
                             collect_seed_candidates(accumulation, domain,
                                                     visible_crop_bounds(domain), 768U),
                             1536U);
    if (candidates.empty()) {
        candidates = collect_seed_candidates(accumulation, domain, visible_crop_bounds(domain),
                                             1536U);
    }
    const int fallback_seed = select_main_channel_seed(accumulation, domain);
    if (candidates.empty() && fallback_seed >= 0) {
        candidates.push_back(fallback_seed);
    }

    std::vector<ChannelPathPoint> best_trunk;
    float best_score = -1.0F;
    for (const int seed : candidates) {
        std::vector<ChannelPathPoint> trunk =
            trace_channel_streamline(seed, vector_routing.flow_direction, 1.0F);
        if (trunk.empty()) {
            continue;
        }
        const std::size_t visible_samples = visible_path_sample_count(domain, trunk);
        const float visible_length_gate =
            cubey::procedural::smoothstep(24.0F, 96.0F, static_cast<float>(visible_samples));
        const float edge_score =
            static_cast<float>(visible_path_edge_touch_count(domain, trunk)) * 500'000.0F *
            visible_length_gate;
        const float length_score = static_cast<float>(visible_samples) * 20'000.0F;
        const float core_score =
            static_cast<float>(visible_path_core_sample_count(domain, trunk)) * 15'000.0F;
        const float hidden_length_score = static_cast<float>(trunk.size()) * 12.0F;
        const float accumulation_score =
            std::log1p(accumulation.values()[static_cast<std::size_t>(seed)]);
        const float score =
            edge_score + length_score + core_score + hidden_length_score + accumulation_score;
        if (score > best_score) {
            best_score = score;
            best_trunk = std::move(trunk);
        }
    }
    if (visible_path_edge_touch_count(domain, best_trunk) >= 2U &&
        visible_path_sample_count(domain, best_trunk) >= 32U) {
        return best_trunk;
    }
    const std::vector<std::vector<int>> upstream = make_upstream_adjacency(graph_routing.downstream);
    return make_channel_path_points(domain.hidden_desc,
                                    trace_main_trunk_graph(accumulation, upstream,
                                                           graph_routing.downstream, domain),
                                    1.0F);
}

[[nodiscard]] cubey::procedural::NoiseSource2D channel_offset_source(std::uint64_t seed) {
    return cubey::procedural::NoiseSource2D{
        .backend = cubey::procedural::NoiseSource2DBackend::CoherentNoise,
        .output = cubey::procedural::NoiseSource2DOutput::Signed,
        .seed = seed,
        .coherent =
            {
                .frequency = 0.075F,
                .noise_type = cubey::procedural::CoherentNoiseType::OpenSimplex2S,
                .fractal_type = cubey::procedural::CoherentFractalType::Fbm,
                .octaves = 2U,
                .lacunarity = 2.0F,
                .gain = 0.45F,
            },
    };
}

[[nodiscard]] float channel_point_distance(const ChannelPathPoint& lhs,
                                           const ChannelPathPoint& rhs) {
    const float dx = rhs.x - lhs.x;
    const float dy = rhs.y - lhs.y;
    return std::sqrt((dx * dx) + (dy * dy));
}

[[nodiscard]] ChannelPathPoint lerp_channel_point(const ChannelPathPoint& lhs,
                                                  const ChannelPathPoint& rhs, float t) {
    return ChannelPathPoint{
        .x = cubey::procedural::lerp(lhs.x, rhs.x, t),
        .y = cubey::procedural::lerp(lhs.y, rhs.y, t),
        .strength = cubey::procedural::lerp(lhs.strength, rhs.strength, t),
    };
}

[[nodiscard]] std::vector<ChannelPathPoint> resample_channel_path(
    const std::vector<ChannelPathPoint>& points, float spacing_cells) {
    if (points.size() < 2U || spacing_cells <= 0.0F) {
        return points;
    }

    std::vector<ChannelPathPoint> result;
    result.reserve(points.size() * 2U);
    result.push_back(points.front());
    for (std::size_t index = 0; index + 1U < points.size(); ++index) {
        const ChannelPathPoint& a = points[index];
        const ChannelPathPoint& b = points[index + 1U];
        const float length = channel_point_distance(a, b);
        if (length <= 0.0001F) {
            continue;
        }
        const int steps = std::max(1, static_cast<int>(std::ceil(length / spacing_cells)));
        for (int step = 1; step <= steps; ++step) {
            const float t = static_cast<float>(step) / static_cast<float>(steps);
            result.push_back(lerp_channel_point(a, b, t));
        }
    }
    return result;
}

void smooth_channel_path(std::vector<ChannelPathPoint>& points, int passes) {
    if (points.size() < 3U) {
        return;
    }
    for (int pass = 0; pass < passes; ++pass) {
        std::vector<ChannelPathPoint> next = points;
        for (std::size_t index = 1; index + 1U < points.size(); ++index) {
            const ChannelPathPoint& prev = points[index - 1U];
            const ChannelPathPoint& cur = points[index];
            const ChannelPathPoint& post = points[index + 1U];
            next[index].x = (prev.x * 0.22F) + (cur.x * 0.56F) + (post.x * 0.22F);
            next[index].y = (prev.y * 0.22F) + (cur.y * 0.56F) + (post.y * 0.22F);
            next[index].strength =
                cubey::procedural::saturate((prev.strength * 0.18F) + (cur.strength * 0.64F) +
                                            (post.strength * 0.18F));
        }
        points.swap(next);
    }
}

[[nodiscard]] float sample_field_bilinear(const cubey::procedural::ScalarField2D& field, float x,
                                          float y) {
    const cubey::procedural::Grid2DDesc& desc = field.desc();
    x = std::clamp(x, 0.0F, static_cast<float>(desc.width - 1U));
    y = std::clamp(y, 0.0F, static_cast<float>(desc.height - 1U));
    const std::uint32_t x0 = static_cast<std::uint32_t>(std::floor(x));
    const std::uint32_t y0 = static_cast<std::uint32_t>(std::floor(y));
    const std::uint32_t x1 = std::min(x0 + 1U, desc.width - 1U);
    const std::uint32_t y1 = std::min(y0 + 1U, desc.height - 1U);
    const float tx = x - static_cast<float>(x0);
    const float ty = y - static_cast<float>(y0);
    const float top = cubey::procedural::lerp(field.at(x0, y0), field.at(x1, y0), tx);
    const float bottom = cubey::procedural::lerp(field.at(x0, y1), field.at(x1, y1), tx);
    return cubey::procedural::lerp(top, bottom, ty);
}

void apply_channel_lateral_offset(std::vector<ChannelPathPoint>& points, std::uint64_t seed,
                                  float max_offset_cells,
                                  const cubey::procedural::ScalarField2D& routing_surface) {
    if (points.size() < 3U || max_offset_cells <= 0.0F) {
        return;
    }

    const cubey::procedural::NoiseSource2D source = channel_offset_source(seed);
    const float denominator = static_cast<float>(points.size() - 1U);
    for (std::size_t index = 1; index + 1U < points.size(); ++index) {
        const float t = static_cast<float>(index) / denominator;
        const float end_taper = cubey::procedural::smoothstep(0.0F, 0.10F, t) *
                                (1.0F - cubey::procedural::smoothstep(0.90F, 1.0F, t));
        if (end_taper <= 0.0F) {
            continue;
        }

        const ChannelPathPoint& prev = points[index - 1U];
        const ChannelPathPoint& post = points[index + 1U];
        const float tx = post.x - prev.x;
        const float ty = post.y - prev.y;
        const float length = std::sqrt((tx * tx) + (ty * ty));
        if (length <= 0.0001F) {
            continue;
        }

        const float nx = -ty / length;
        const float ny = tx / length;
        const float noise =
            cubey::procedural::sample_noise_source_2d(points[index].x, points[index].y, source);
        const float offset = noise * max_offset_cells * end_taper;
        float next_x = points[index].x + (nx * offset);
        float next_y = points[index].y + (ny * offset);
        const float current_potential =
            sample_field_bilinear(routing_surface, points[index].x, points[index].y);
        const float next_potential = sample_field_bilinear(routing_surface, next_x, next_y);
        if (next_potential > current_potential + 35.0F) {
            next_x = cubey::procedural::lerp(points[index].x, next_x, 0.25F);
            next_y = cubey::procedural::lerp(points[index].y, next_y, 0.25F);
        }
        points[index].x = next_x;
        points[index].y = next_y;
    }
}

void relax_channel_path(std::vector<ChannelPathPoint>& points,
                        const cubey::procedural::ScalarField2D& routing_surface, int passes) {
    if (points.size() < 3U) {
        return;
    }

    for (int pass = 0; pass < passes; ++pass) {
        std::vector<ChannelPathPoint> next = points;
        for (std::size_t index = 1; index + 1U < points.size(); ++index) {
            const ChannelPathPoint& point = points[index];
            const float left = sample_field_bilinear(routing_surface, point.x - 1.0F, point.y);
            const float right = sample_field_bilinear(routing_surface, point.x + 1.0F, point.y);
            const float up = sample_field_bilinear(routing_surface, point.x, point.y - 1.0F);
            const float down = sample_field_bilinear(routing_surface, point.x, point.y + 1.0F);
            const float gradient_x = (right - left) * 0.5F;
            const float gradient_y = (down - up) * 0.5F;
            const float gradient_length =
                std::sqrt((gradient_x * gradient_x) + (gradient_y * gradient_y));
            if (gradient_length <= 0.0001F) {
                continue;
            }

            const float step =
                0.35F * cubey::procedural::saturate(gradient_length / 24.0F);
            const float candidate_x = point.x - (gradient_x / gradient_length) * step;
            const float candidate_y = point.y - (gradient_y / gradient_length) * step;
            const float current_potential =
                sample_field_bilinear(routing_surface, point.x, point.y);
            const float candidate_potential =
                sample_field_bilinear(routing_surface, candidate_x, candidate_y);
            if (candidate_potential <= current_potential + 8.0F) {
                next[index].x = candidate_x;
                next[index].y = candidate_y;
            }
        }
        points.swap(next);
    }
}

[[nodiscard]] float point_segment_distance_cells(float px, float py, const ChannelPathPoint& a,
                                                 const ChannelPathPoint& b, float& segment_t) {
    const float abx = b.x - a.x;
    const float aby = b.y - a.y;
    const float apx = px - a.x;
    const float apy = py - a.y;
    const float ab_len_sq = (abx * abx) + (aby * aby);
    segment_t = ab_len_sq <= 0.000001F ? 0.0F
                                       : cubey::procedural::saturate(((apx * abx) + (apy * aby)) /
                                                                     ab_len_sq);
    const float cx = a.x + abx * segment_t;
    const float cy = a.y + aby * segment_t;
    const float dx = px - cx;
    const float dy = py - cy;
    return std::sqrt((dx * dx) + (dy * dy));
}

void rasterize_channel_segments(cubey::procedural::ScalarField2D& field,
                                const std::vector<ChannelPathPoint>& points, float core_radius_cells,
                                float falloff_radius_cells, const RoutingDomain& domain) {
    if (points.empty()) {
        return;
    }
    if (points.size() == 1U) {
        const int cx = static_cast<int>(
            std::round(points.front().x - static_cast<float>(domain.padding_x)));
        const int cy = static_cast<int>(
            std::round(points.front().y - static_cast<float>(domain.padding_y)));
        if (cx >= 0 && cy >= 0 && cx < static_cast<int>(field.desc().width) &&
            cy < static_cast<int>(field.desc().height)) {
            field.at(static_cast<std::uint32_t>(cx), static_cast<std::uint32_t>(cy)) =
                std::max(field.at(static_cast<std::uint32_t>(cx), static_cast<std::uint32_t>(cy)),
                         points.front().strength);
        }
        return;
    }

    const float influence_radius = std::max(core_radius_cells, falloff_radius_cells);
    for (std::size_t index = 0; index + 1U < points.size(); ++index) {
        const ChannelPathPoint a{
            .x = points[index].x - static_cast<float>(domain.padding_x),
            .y = points[index].y - static_cast<float>(domain.padding_y),
            .strength = points[index].strength,
        };
        const ChannelPathPoint b{
            .x = points[index + 1U].x - static_cast<float>(domain.padding_x),
            .y = points[index + 1U].y - static_cast<float>(domain.padding_y),
            .strength = points[index + 1U].strength,
        };
        const float min_x = std::min(a.x, b.x) - influence_radius;
        const float max_x = std::max(a.x, b.x) + influence_radius;
        const float min_y = std::min(a.y, b.y) - influence_radius;
        const float max_y = std::max(a.y, b.y) + influence_radius;
        if (max_x < 0.0F || max_y < 0.0F ||
            min_x > static_cast<float>(field.desc().width - 1U) ||
            min_y > static_cast<float>(field.desc().height - 1U)) {
            continue;
        }
        const std::uint32_t x0 = static_cast<std::uint32_t>(std::max(0.0F, std::floor(min_x)));
        const std::uint32_t y0 = static_cast<std::uint32_t>(std::max(0.0F, std::floor(min_y)));
        const std::uint32_t x1 = static_cast<std::uint32_t>(
            std::min(static_cast<float>(field.desc().width - 1U), std::ceil(max_x)));
        const std::uint32_t y1 = static_cast<std::uint32_t>(
            std::min(static_cast<float>(field.desc().height - 1U), std::ceil(max_y)));

        for (std::uint32_t y = y0; y <= y1; ++y) {
            for (std::uint32_t x = x0; x <= x1; ++x) {
                float segment_t = 0.0F;
                const float distance = point_segment_distance_cells(static_cast<float>(x),
                                                                    static_cast<float>(y), a, b,
                                                                    segment_t);
                if (distance > falloff_radius_cells) {
                    continue;
                }
                const float local_strength =
                    cubey::procedural::lerp(a.strength, b.strength, segment_t);
                const float core =
                    1.0F - cubey::procedural::smoothstep(0.0F, core_radius_cells, distance);
                const float shoulder =
                    1.0F - cubey::procedural::smoothstep(core_radius_cells * 0.65F,
                                                         falloff_radius_cells, distance);
                const float profile = cubey::procedural::saturate((core * 0.82F) +
                                                                  (shoulder * 0.34F));
                field.at(x, y) = std::max(field.at(x, y), local_strength * profile);
            }
        }
    }
}

void paint_channel_path(cubey::procedural::ScalarField2D& field,
                        std::vector<ChannelPathPoint> points, const RoutingDomain& domain,
                        float core_radius_cells, float falloff_radius_cells,
                        const cubey::procedural::ScalarField2D& routing_surface,
                        std::uint64_t seed, float max_offset_cells) {
    if (points.empty()) {
        return;
    }
    points = resample_channel_path(points, 1.6F);
    smooth_channel_path(points, 2);
    apply_channel_lateral_offset(points, seed, max_offset_cells, routing_surface);
    relax_channel_path(points, routing_surface, 4);
    smooth_channel_path(points, 1);
    rasterize_channel_segments(field, points, core_radius_cells, falloff_radius_cells, domain);
}

[[nodiscard]] std::vector<int> collect_stream_order_candidates(const RoutingContext& context,
                                                               float min_stream_order,
                                                               std::size_t max_count) {
    const HiddenIndexBounds bounds = visible_crop_bounds(context.domain);
    std::vector<int> candidates;
    candidates.reserve(static_cast<std::size_t>((bounds.x_end - bounds.x_begin) *
                                                (bounds.y_end - bounds.y_begin)));
    for (std::uint32_t y = bounds.y_begin; y < bounds.y_end; ++y) {
        for (std::uint32_t x = bounds.x_begin; x < bounds.x_end; ++x) {
            const std::size_t index =
                cubey::procedural::grid_index(x, y, context.domain.hidden_desc.width);
            if (context.stream_order.values()[index] >= min_stream_order) {
                candidates.push_back(static_cast<int>(index));
            }
        }
    }
    std::sort(candidates.begin(), candidates.end(), [&context](int lhs, int rhs) {
        const float lhs_accumulation = context.accumulation.values()[static_cast<std::size_t>(lhs)];
        const float rhs_accumulation = context.accumulation.values()[static_cast<std::size_t>(rhs)];
        if (lhs_accumulation == rhs_accumulation) {
            return context.stream_order.values()[static_cast<std::size_t>(lhs)] >
                   context.stream_order.values()[static_cast<std::size_t>(rhs)];
        }
        return lhs_accumulation > rhs_accumulation;
    });
    if (candidates.size() > max_count) {
        candidates.resize(max_count);
    }
    return candidates;
}

[[nodiscard]] float active_path_overlap_fraction(const std::vector<int>& indices,
                                                 const std::vector<bool>& active) {
    if (indices.empty()) {
        return 1.0F;
    }
    std::size_t active_count = 0U;
    for (const int index : indices) {
        if (index >= 0 && active[static_cast<std::size_t>(index)]) {
            ++active_count;
        }
    }
    return static_cast<float>(active_count) / static_cast<float>(indices.size());
}

[[nodiscard]] bool path_is_near_active(const cubey::procedural::Grid2DDesc& desc,
                                       const std::vector<int>& indices,
                                       const std::vector<bool>& active,
                                       int min_distance_cells) {
    if (min_distance_cells <= 0) {
        return false;
    }
    const int min_distance_sq = min_distance_cells * min_distance_cells;
    for (const int path_index : indices) {
        if (path_index < 0) {
            continue;
        }
        const auto path_uindex = static_cast<std::uint32_t>(path_index);
        const int path_x = static_cast<int>(path_uindex % desc.width);
        const int path_y = static_cast<int>(path_uindex / desc.width);
        for (int oy = -min_distance_cells; oy <= min_distance_cells; ++oy) {
            for (int ox = -min_distance_cells; ox <= min_distance_cells; ++ox) {
                if ((ox * ox) + (oy * oy) > min_distance_sq) {
                    continue;
                }
                const int x = path_x + ox;
                const int y = path_y + oy;
                if (!neighbor_in_bounds(desc, x, y)) {
                    continue;
                }
                const std::size_t sample = cubey::procedural::grid_index(
                    static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y), desc.width);
                if (active[sample]) {
                    return true;
                }
            }
        }
    }
    return false;
}

void mark_active_indices(std::vector<bool>& active, const std::vector<int>& indices) {
    for (const int index : indices) {
        if (index >= 0) {
            active[static_cast<std::size_t>(index)] = true;
        }
    }
}

void update_component_visibility(RiverCorridorComponent& component,
                                 const RoutingContext& context, int index,
                                 float trunk_stream_order) {
    if (index < 0) {
        return;
    }
    const auto uindex = static_cast<std::uint32_t>(index);
    const std::uint32_t x = uindex % context.domain.hidden_desc.width;
    const std::uint32_t y = uindex / context.domain.hidden_desc.width;
    const bool inside_y = y >= context.domain.padding_y &&
                          y < context.domain.padding_y + context.domain.visible_desc.height;
    const bool inside_x = x >= context.domain.padding_x &&
                          x < context.domain.padding_x + context.domain.visible_desc.width;

    if (inside_x && inside_y) {
        ++component.visible_samples;
        component.visible_core_samples += is_inside_visible_core(context.domain, index) ? 1U : 0U;
        component.trunk_visible_samples +=
            context.stream_order.values()[static_cast<std::size_t>(index)] >= trunk_stream_order
                ? 1U
                : 0U;
    }
    constexpr std::uint32_t kComponentEdgeBandCells = 8U;
    component.touches_left =
        component.touches_left ||
        (inside_y && x <= context.domain.padding_x + kComponentEdgeBandCells);
    component.touches_right =
        component.touches_right ||
        (inside_y &&
         x + kComponentEdgeBandCells >= context.domain.padding_x + context.domain.visible_desc.width -
                                           1U);
    component.touches_top =
        component.touches_top ||
        (inside_x && y <= context.domain.padding_y + kComponentEdgeBandCells);
    component.touches_bottom =
        component.touches_bottom ||
        (inside_x &&
         y + kComponentEdgeBandCells >= context.domain.padding_y + context.domain.visible_desc.height -
                                           1U);
    component.max_accumulation =
        std::max(component.max_accumulation,
                 context.accumulation.values()[static_cast<std::size_t>(index)]);
}

[[nodiscard]] std::size_t corridor_component_edge_touch_count(
    const RiverCorridorComponent& component) {
    return static_cast<std::size_t>(component.touches_left) +
           static_cast<std::size_t>(component.touches_right) +
           static_cast<std::size_t>(component.touches_top) +
           static_cast<std::size_t>(component.touches_bottom);
}

void score_corridor_component(RiverCorridorComponent& component) {
    const std::size_t edge_touch_count = corridor_component_edge_touch_count(component);
    component.score =
        (static_cast<float>(component.visible_samples) * 4.0F) +
        (static_cast<float>(component.visible_core_samples) * 6.0F) +
        (static_cast<float>(component.trunk_visible_samples) * 12.0F) +
        (static_cast<float>(edge_touch_count) * 2500.0F) +
        (std::log1p(component.max_accumulation) * 22.0F) +
        (static_cast<float>(component.cells.size()) * 0.035F);
}

[[nodiscard]] RiverCorridorSelection select_stream_order_corridors(
    const RoutingContext& context, const RiverNetworkSettings& settings) {
    const std::size_t sample_count = context.accumulation.sample_count();
    RiverCorridorSelection selection{
        .selected = std::vector<bool>(sample_count, false),
        .trunk_support = std::vector<bool>(sample_count, false),
    };

    std::vector<bool> candidate(sample_count, false);
    for (std::size_t index = 0U; index < sample_count; ++index) {
        candidate[index] =
            context.stream_order.values()[index] >= settings.corridor_tributary_stream_order;
    }

    std::vector<bool> visited(sample_count, false);
    std::vector<RiverCorridorComponent> components;
    components.reserve(64U);

    for (std::size_t index = 0U; index < sample_count; ++index) {
        if (!candidate[index] || visited[index]) {
            continue;
        }
        RiverCorridorComponent component{.terminal_index = static_cast<int>(index)};
        std::vector<int> stack{static_cast<int>(index)};
        visited[index] = true;
        while (!stack.empty()) {
            const int current = stack.back();
            stack.pop_back();
            component.cells.push_back(current);
            update_component_visibility(component, context, current,
                                        settings.corridor_trunk_stream_order);

            const auto current_index = static_cast<std::uint32_t>(current);
            const int cx = static_cast<int>(current_index % context.domain.hidden_desc.width);
            const int cy = static_cast<int>(current_index / context.domain.hidden_desc.width);
            for (int oy = -1; oy <= 1; ++oy) {
                for (int ox = -1; ox <= 1; ++ox) {
                    if (ox == 0 && oy == 0) {
                        continue;
                    }
                    const int nx = cx + ox;
                    const int ny = cy + oy;
                    if (!neighbor_in_bounds(context.domain.hidden_desc, nx, ny)) {
                        continue;
                    }
                    const std::size_t neighbor = cubey::procedural::grid_index(
                        static_cast<std::uint32_t>(nx), static_cast<std::uint32_t>(ny),
                        context.domain.hidden_desc.width);
                    if (!candidate[neighbor] || visited[neighbor]) {
                        continue;
                    }
                    visited[neighbor] = true;
                    stack.push_back(static_cast<int>(neighbor));
                }
            }
        }
        components.push_back(std::move(component));
    }

    for (RiverCorridorComponent& component : components) {
        score_corridor_component(component);
    }
    const bool has_visible_edge_component = std::any_of(
        components.begin(), components.end(), [&settings](const RiverCorridorComponent& component) {
            return component.visible_samples >= settings.corridor_min_visible_samples &&
                   corridor_component_edge_touch_count(component) > 0U;
        });
    std::sort(components.begin(), components.end(),
              [](const RiverCorridorComponent& lhs, const RiverCorridorComponent& rhs) {
                  return lhs.score > rhs.score;
              });

    for (const RiverCorridorComponent& component : components) {
        if (selection.corridors.size() >= settings.corridor_count) {
            break;
        }
        if (component.visible_samples < settings.corridor_min_visible_samples &&
            selection.corridors.empty()) {
            continue;
        }
        if (has_visible_edge_component &&
            corridor_component_edge_touch_count(component) == 0U) {
            continue;
        }
        if (component.visible_samples == 0U) {
            continue;
        }

        RiverCorridor corridor{
            .terminal_index = component.terminal_index,
            .cells = component.cells,
        };
        for (const int index : corridor.cells) {
            const std::size_t sample = static_cast<std::size_t>(index);
            selection.selected[sample] = true;
            selection.trunk_support[sample] =
                context.stream_order.values()[sample] >= settings.corridor_trunk_stream_order;
        }
        selection.corridors.push_back(std::move(corridor));
    }

    if (selection.corridors.empty() && !components.empty()) {
        const RiverCorridorComponent& component = components.front();
        RiverCorridor corridor{
            .terminal_index = component.terminal_index,
            .cells = component.cells,
        };
        for (const int index : corridor.cells) {
            const std::size_t sample = static_cast<std::size_t>(index);
            selection.selected[sample] = true;
            selection.trunk_support[sample] =
                context.stream_order.values()[sample] >= settings.corridor_trunk_stream_order;
        }
        selection.corridors.push_back(std::move(corridor));
    }

    return selection;
}

[[nodiscard]] int strongest_upstream_allowed_candidate(
    int index, const std::vector<std::vector<int>>& upstream,
    const cubey::procedural::ScalarField2D& accumulation, const std::vector<bool>& allowed,
    const std::vector<bool>& excluded) {
    int best_index = -1;
    float best_accumulation = -1.0F;
    for (const int candidate : upstream[static_cast<std::size_t>(index)]) {
        const std::size_t sample = static_cast<std::size_t>(candidate);
        if (!allowed[sample] || excluded[sample]) {
            continue;
        }
        const float value = accumulation.values()[sample];
        if (value > best_accumulation) {
            best_accumulation = value;
            best_index = candidate;
        }
    }
    return best_index;
}

[[nodiscard]] std::vector<int> trace_strongest_upstream_allowed_path(
    int start, const std::vector<std::vector<int>>& upstream,
    const cubey::procedural::ScalarField2D& accumulation, const std::vector<bool>& allowed,
    const std::vector<bool>& excluded) {
    std::vector<int> path;
    if (start < 0 || !allowed[static_cast<std::size_t>(start)]) {
        return path;
    }

    std::vector<bool> visited(accumulation.sample_count(), false);
    int current = start;
    while (current >= 0 && allowed[static_cast<std::size_t>(current)] &&
           !excluded[static_cast<std::size_t>(current)] &&
           !visited[static_cast<std::size_t>(current)]) {
        visited[static_cast<std::size_t>(current)] = true;
        path.push_back(current);
        current = strongest_upstream_allowed_candidate(current, upstream, accumulation, allowed,
                                                       excluded);
    }
    return path;
}

[[nodiscard]] std::vector<int> trace_downstream_allowed_path(
    int start, const std::vector<int>& downstream, const std::vector<bool>& allowed) {
    std::vector<int> path;
    if (start < 0 || !allowed[static_cast<std::size_t>(start)]) {
        return path;
    }

    std::vector<bool> visited(downstream.size(), false);
    int current = start;
    while (current >= 0 && allowed[static_cast<std::size_t>(current)] &&
           !visited[static_cast<std::size_t>(current)]) {
        visited[static_cast<std::size_t>(current)] = true;
        path.push_back(current);
        current = downstream[static_cast<std::size_t>(current)];
    }
    return path;
}

[[nodiscard]] std::vector<int> trace_corridor_trunk_from_seed(
    int seed, const std::vector<std::vector<int>>& upstream, const std::vector<int>& downstream,
    const cubey::procedural::ScalarField2D& accumulation, const std::vector<bool>& allowed) {
    const std::vector<bool> no_exclusions(accumulation.sample_count(), false);
    std::vector<int> upstream_path =
        trace_strongest_upstream_allowed_path(seed, upstream, accumulation, allowed,
                                              no_exclusions);
    std::reverse(upstream_path.begin(), upstream_path.end());
    std::vector<int> downstream_path = trace_downstream_allowed_path(seed, downstream, allowed);

    std::vector<int> trunk = std::move(upstream_path);
    const std::size_t downstream_start = trunk.empty() ? 0U : 1U;
    for (std::size_t index = downstream_start; index < downstream_path.size(); ++index) {
        trunk.push_back(downstream_path[index]);
    }
    return trunk;
}

[[nodiscard]] std::vector<int> collect_corridor_trunk_candidates(
    const RoutingContext& context, const RiverCorridor& corridor,
    const std::vector<bool>& trunk_support, std::size_t max_count) {
    std::vector<int> candidates;
    candidates.reserve(std::min(corridor.cells.size(), max_count));
    for (const int index : corridor.cells) {
        if (trunk_support[static_cast<std::size_t>(index)] &&
            is_inside_visible_crop(context.domain, index)) {
            candidates.push_back(index);
        }
    }
    if (candidates.empty()) {
        for (const int index : corridor.cells) {
            if (is_inside_visible_crop(context.domain, index)) {
                candidates.push_back(index);
            }
        }
    }
    if (candidates.empty()) {
        candidates = corridor.cells;
    }

    std::sort(candidates.begin(), candidates.end(), [&context](int lhs, int rhs) {
        const float lhs_accumulation = context.accumulation.values()[static_cast<std::size_t>(lhs)];
        const float rhs_accumulation = context.accumulation.values()[static_cast<std::size_t>(rhs)];
        if (lhs_accumulation == rhs_accumulation) {
            return context.stream_order.values()[static_cast<std::size_t>(lhs)] >
                   context.stream_order.values()[static_cast<std::size_t>(rhs)];
        }
        return lhs_accumulation > rhs_accumulation;
    });
    if (candidates.size() > max_count) {
        candidates.resize(max_count);
    }
    return candidates;
}

[[nodiscard]] bool is_visible_crop_edge_cell(const RoutingDomain& domain, int hidden_index) {
    if (!is_inside_visible_crop(domain, hidden_index)) {
        return false;
    }
    const auto index = static_cast<std::uint32_t>(hidden_index);
    const std::uint32_t x = index % domain.hidden_desc.width;
    const std::uint32_t y = index / domain.hidden_desc.width;
    return x == domain.padding_x || x == domain.padding_x + domain.visible_desc.width - 1U ||
           y == domain.padding_y || y == domain.padding_y + domain.visible_desc.height - 1U;
}

[[nodiscard]] int select_corridor_support_seed(const RoutingContext& context,
                                               const RiverCorridor& corridor) {
    int best_index = -1;
    float best_score = -std::numeric_limits<float>::infinity();
    for (const int index : corridor.cells) {
        const float visible_score = is_inside_visible_crop(context.domain, index) ? 500.0F : 0.0F;
        const float edge_score = is_visible_crop_edge_cell(context.domain, index) ? 1400.0F : 0.0F;
        const float core_score = is_inside_visible_core(context.domain, index) ? 240.0F : 0.0F;
        const float accumulation_score =
            std::log1p(context.accumulation.values()[static_cast<std::size_t>(index)]) * 24.0F;
        const float order_score =
            context.stream_order.values()[static_cast<std::size_t>(index)] * 80.0F;
        const float score = visible_score + edge_score + core_score + accumulation_score +
                            order_score;
        if (score > best_score) {
            best_score = score;
            best_index = index;
        }
    }
    return best_index;
}

[[nodiscard]] int farthest_corridor_support_cell(
    int start, const std::vector<bool>& allowed, const RoutingContext& context,
    std::vector<int>* parent_out = nullptr) {
    if (start < 0 || !allowed[static_cast<std::size_t>(start)]) {
        return -1;
    }

    const std::size_t sample_count = context.accumulation.sample_count();
    std::vector<int> distance(sample_count, -1);
    if (parent_out != nullptr) {
        parent_out->assign(sample_count, -1);
    }

    std::vector<int> queue;
    queue.reserve(4096U);
    queue.push_back(start);
    distance[static_cast<std::size_t>(start)] = 0;
    int best_index = start;
    float best_score = -std::numeric_limits<float>::infinity();

    for (std::size_t head = 0U; head < queue.size(); ++head) {
        const int current = queue[head];
        const int current_distance = distance[static_cast<std::size_t>(current)];
        const float visible_score =
            is_inside_visible_crop(context.domain, current) ? 550.0F : 0.0F;
        const float edge_score =
            is_visible_crop_edge_cell(context.domain, current) ? 1400.0F : 0.0F;
        const float core_score = is_inside_visible_core(context.domain, current) ? 180.0F : 0.0F;
        const float accumulation_score =
            std::log1p(context.accumulation.values()[static_cast<std::size_t>(current)]) * 10.0F;
        const float score = static_cast<float>(current_distance) + visible_score + edge_score +
                            core_score + accumulation_score;
        if (score > best_score) {
            best_score = score;
            best_index = current;
        }

        const auto current_index = static_cast<std::uint32_t>(current);
        const int cx = static_cast<int>(current_index % context.domain.hidden_desc.width);
        const int cy = static_cast<int>(current_index / context.domain.hidden_desc.width);
        for (int oy = -1; oy <= 1; ++oy) {
            for (int ox = -1; ox <= 1; ++ox) {
                if (ox == 0 && oy == 0) {
                    continue;
                }
                const int nx = cx + ox;
                const int ny = cy + oy;
                if (!neighbor_in_bounds(context.domain.hidden_desc, nx, ny)) {
                    continue;
                }
                const std::size_t neighbor = cubey::procedural::grid_index(
                    static_cast<std::uint32_t>(nx), static_cast<std::uint32_t>(ny),
                    context.domain.hidden_desc.width);
                if (!allowed[neighbor] || distance[neighbor] >= 0) {
                    continue;
                }
                distance[neighbor] = current_distance + 1;
                if (parent_out != nullptr) {
                    (*parent_out)[neighbor] = current;
                }
                queue.push_back(static_cast<int>(neighbor));
            }
        }
    }

    return best_index;
}

[[nodiscard]] std::vector<int> trace_corridor_support_spine(const RoutingContext& context,
                                                            const RiverCorridor& corridor) {
    if (corridor.cells.empty()) {
        return {};
    }

    std::vector<bool> allowed(context.accumulation.sample_count(), false);
    for (const int index : corridor.cells) {
        allowed[static_cast<std::size_t>(index)] = true;
    }

    const int seed = select_corridor_support_seed(context, corridor);
    const int first = farthest_corridor_support_cell(seed, allowed, context);
    std::vector<int> parent;
    const int second = farthest_corridor_support_cell(first, allowed, context, &parent);
    if (first < 0 || second < 0) {
        return {};
    }

    std::vector<int> path;
    int current = second;
    while (current >= 0) {
        path.push_back(current);
        if (current == first) {
            break;
        }
        current = parent[static_cast<std::size_t>(current)];
    }
    if (path.empty() || path.back() != first) {
        return {};
    }
    std::reverse(path.begin(), path.end());
    return path;
}

[[nodiscard]] std::vector<int> trace_best_corridor_trunk(
    const RoutingContext& context, const RiverCorridor& corridor,
    const RiverCorridorSelection& selection, const std::vector<std::vector<int>>& upstream,
    const RiverNetworkSettings& settings) {
    const std::vector<int> candidates =
        collect_corridor_trunk_candidates(context, corridor, selection.trunk_support,
                                          settings.corridor_seed_count);
    std::vector<int> best_trunk;
    float best_score = -std::numeric_limits<float>::infinity();
    for (const int seed : candidates) {
        const std::vector<int> trunk =
            trace_corridor_trunk_from_seed(seed, upstream, context.d8_routing.downstream,
                                           context.accumulation, selection.selected);
        if (visible_grid_path_sample_count(context.domain, trunk) < 24U) {
            continue;
        }
        const float score = score_grid_trunk(context.accumulation, context.domain, trunk, seed);
        if (score > best_score) {
            best_score = score;
            best_trunk = trunk;
        }
    }
    if (best_trunk.empty() && !candidates.empty()) {
        best_trunk = trace_corridor_trunk_from_seed(candidates.front(), upstream,
                                                    context.d8_routing.downstream,
                                                    context.accumulation, selection.selected);
    }
    const std::vector<int> support_spine = trace_corridor_support_spine(context, corridor);
    if (!support_spine.empty()) {
        const bool current_crosses_crop =
            visible_grid_path_edge_touch_count(context.domain, best_trunk) >= 2U;
        const bool support_crosses_crop =
            visible_grid_path_edge_touch_count(context.domain, support_spine) >= 2U;
        const bool support_is_longer =
            visible_grid_path_sample_count(context.domain, support_spine) >
            visible_grid_path_sample_count(context.domain, best_trunk) + 16U;
        if (best_trunk.empty() || (!current_crosses_crop && support_crosses_crop) ||
            support_is_longer) {
            best_trunk = support_spine;
        }
    }
    return best_trunk;
}

[[nodiscard]] float channel_strength_for_order(const RoutingContext& context, int index,
                                               float minimum_strength) {
    if (index < 0) {
        return minimum_strength;
    }
    const std::size_t sample = static_cast<std::size_t>(index);
    const float order =
        cubey::procedural::saturate((context.stream_order.values()[sample] - 2.0F) / 3.0F);
    const float max_accumulation = std::max(context.accumulation.summarize().max, 1.0F);
    const float discharge =
        std::log1p(context.accumulation.values()[sample]) / std::log1p(max_accumulation);
    return cubey::procedural::saturate(
        std::max(minimum_strength, 0.52F + (order * 0.22F) + (discharge * 0.30F)));
}

[[nodiscard]] std::vector<ChannelPathPoint> make_corridor_channel_points(
    const RoutingContext& context, const std::vector<int>& path, float minimum_strength) {
    std::vector<ChannelPathPoint> points;
    points.reserve(path.size());
    for (const int index : path) {
        if (index < 0) {
            continue;
        }
        const auto uindex = static_cast<std::uint32_t>(index);
        points.push_back(ChannelPathPoint{
            .x = static_cast<float>(uindex % context.domain.hidden_desc.width),
            .y = static_cast<float>(uindex / context.domain.hidden_desc.width),
            .strength = channel_strength_for_order(context, index, minimum_strength),
        });
    }
    return points;
}

void snap_channel_endpoint_to_visible_edge(ChannelPathPoint& point, const RoutingDomain& domain,
                                           float max_distance_cells) {
    if (!is_inside_visible_crop(domain, point)) {
        return;
    }

    const float left = point.x - static_cast<float>(domain.padding_x);
    const float right =
        static_cast<float>(domain.padding_x + domain.visible_desc.width - 1U) - point.x;
    const float top = point.y - static_cast<float>(domain.padding_y);
    const float bottom =
        static_cast<float>(domain.padding_y + domain.visible_desc.height - 1U) - point.y;
    const float nearest = std::min(std::min(left, right), std::min(top, bottom));
    if (nearest > max_distance_cells) {
        return;
    }

    if (nearest == left) {
        point.x = static_cast<float>(domain.padding_x);
    } else if (nearest == right) {
        point.x = static_cast<float>(domain.padding_x + domain.visible_desc.width - 1U);
    } else if (nearest == top) {
        point.y = static_cast<float>(domain.padding_y);
    } else {
        point.y = static_cast<float>(domain.padding_y + domain.visible_desc.height - 1U);
    }
}

void snap_trunk_endpoints_to_visible_edges(std::vector<ChannelPathPoint>& points,
                                           const RoutingDomain& domain) {
    if (points.empty()) {
        return;
    }
    constexpr float kMaxEndpointSnapDistanceCells = 96.0F;
    snap_channel_endpoint_to_visible_edge(points.front(), domain, kMaxEndpointSnapDistanceCells);
    snap_channel_endpoint_to_visible_edge(points.back(), domain, kMaxEndpointSnapDistanceCells);
}

[[nodiscard]] std::size_t add_corridor_tributaries(
    RiverFields& fields, const RoutingContext& context, const RiverCorridorSelection& selection,
    const std::vector<std::vector<int>>& upstream, const std::vector<std::vector<int>>& trunks,
    const RiverNetworkSettings& settings, std::uint64_t seed, std::vector<bool>& active) {
    std::size_t accepted = 0U;
    for (const std::vector<int>& trunk : trunks) {
        for (const int trunk_index : trunk) {
            if (accepted >= settings.corridor_branch_count) {
                return accepted;
            }
            std::vector<int> candidates = upstream[static_cast<std::size_t>(trunk_index)];
            std::sort(candidates.begin(), candidates.end(), [&context](int lhs, int rhs) {
                return context.accumulation.values()[static_cast<std::size_t>(lhs)] >
                       context.accumulation.values()[static_cast<std::size_t>(rhs)];
            });

            for (const int candidate : candidates) {
                const std::size_t sample = static_cast<std::size_t>(candidate);
                if (!selection.selected[sample] || active[sample]) {
                    continue;
                }
                std::vector<int> branch = trace_strongest_upstream_allowed_path(
                    candidate, upstream, context.accumulation, selection.selected, active);
                if (branch.size() + 1U < settings.min_tributary_path_samples) {
                    continue;
                }
                std::reverse(branch.begin(), branch.end());
                branch.push_back(trunk_index);
                if (visible_grid_path_sample_count(context.domain, branch) <
                    settings.corridor_branch_min_visible_samples) {
                    continue;
                }
                if (active_path_overlap_fraction(branch, active) >
                    settings.corridor_branch_max_active_overlap) {
                    continue;
                }
                std::vector<ChannelPathPoint> branch_points =
                    make_corridor_channel_points(context, branch, settings.tributary_strength);
                if (visible_path_sample_count(context.domain, branch_points) == 0U) {
                    continue;
                }
                paint_channel_path(fields.tributaries, std::move(branch_points), context.domain,
                                   settings.tributary_core_radius_cells,
                                   settings.tributary_falloff_radius_cells,
                                   context.routing_surface, seed + 7103U + (accepted * 131U),
                                   settings.tributary_offset_cells);
                mark_active_indices(active, branch);
                ++accepted;
                break;
            }
        }
    }
    return accepted;
}

void combine_river_mask(RiverFields& fields) {
    for (std::uint32_t y = 0; y < fields.river_mask.desc().height; ++y) {
        for (std::uint32_t x = 0; x < fields.river_mask.desc().width; ++x) {
            fields.river_mask.at(x, y) =
                std::max(fields.river_trunk.at(x, y), fields.tributaries.at(x, y));
        }
    }
}

void add_stream_order_seed_paths(RiverFields& fields, const RoutingContext& context,
                                 std::uint64_t seed, const RiverNetworkSettings& settings,
                                 std::vector<bool>& active) {
    const std::vector<int> candidates = collect_stream_order_candidates(
        context, settings.order_seed_min_stream_order, settings.order_seed_count * 12U);
    std::size_t accepted = 0U;
    for (const int candidate : candidates) {
        if (accepted >= settings.order_seed_count) {
            break;
        }
        if (active[static_cast<std::size_t>(candidate)]) {
            continue;
        }
        const float order = context.stream_order.values()[static_cast<std::size_t>(candidate)];
        const float strength =
            cubey::procedural::saturate(0.56F + ((order - 3.0F) * 0.13F));
        std::vector<ChannelPathPoint> path =
            trace_channel_streamline(candidate, context.routing.flow_direction, strength);
        if (path.size() < settings.min_tributary_path_samples) {
            continue;
        }
        if (visible_path_sample_count(context.domain, path) <
            settings.order_seed_min_visible_samples) {
            continue;
        }
        const std::vector<int> indices = path_point_indices(context.domain.hidden_desc, path);
        if (indices.size() < settings.min_tributary_path_samples) {
            continue;
        }
        const float overlap = active_path_overlap_fraction(indices, active);
        if (overlap > settings.order_seed_max_active_overlap) {
            continue;
        }
        const bool connects_to_active =
            overlap > 0.02F ||
            path_is_near_active(context.domain.hidden_desc, indices, active,
                                settings.order_seed_contact_distance_cells);
        if (!connects_to_active) {
            continue;
        }
        if (path_is_near_active(context.domain.hidden_desc, indices, active,
                                settings.order_seed_min_distance_cells) &&
            overlap > 0.08F) {
            continue;
        }

        if (order >= settings.order_seed_trunk_stream_order) {
            paint_channel_path(fields.river_trunk, path, context.domain,
                               settings.trunk_core_radius_cells,
                               settings.trunk_falloff_radius_cells, context.routing_surface,
                               seed + 5107U + (accepted * 149U),
                               settings.trunk_offset_cells);
        } else {
            paint_channel_path(fields.tributaries, path, context.domain,
                               settings.tributary_core_radius_cells,
                               settings.tributary_falloff_radius_cells, context.routing_surface,
                               seed + 6203U + (accepted * 127U),
                               settings.tributary_offset_cells);
        }
        mark_active_indices(active, indices);
        ++accepted;
    }
}

void activate_river_network_fallback(RiverFields& fields, const RoutingContext& context,
                                     std::uint64_t seed,
                                     const RiverNetworkSettings& settings) {
    const std::vector<int>& downstream = context.d8_routing.downstream;
    const std::vector<std::vector<int>> upstream = make_upstream_adjacency(downstream);
    const std::vector<ChannelPathPoint> trunk =
        trace_main_trunk(context.accumulation, context.routing, context.d8_routing,
                         context.domain);
    if (trunk.empty()) {
        return;
    }
    paint_channel_path(fields.river_trunk, trunk, context.domain, settings.trunk_core_radius_cells,
                       settings.trunk_falloff_radius_cells, context.routing_surface, seed + 1101U,
                       settings.trunk_offset_cells);

    std::vector<bool> active(context.accumulation.sample_count(), false);
    std::vector<int> trunk_indices = path_point_indices(context.domain.hidden_desc, trunk);
    if (trunk_indices.empty()) {
        return;
    }
    for (const int index : trunk_indices) {
        active[static_cast<std::size_t>(index)] = true;
    }

    const std::vector<std::vector<int>> secondary_trunks = trace_secondary_trunk_graphs(
        context.accumulation, upstream, downstream, context.domain, trunk_indices,
        settings.secondary_trunk_count, settings.secondary_trunk_min_distance_cells);
    std::size_t secondary_index = 0U;
    for (const std::vector<int>& secondary_trunk : secondary_trunks) {
        const std::vector<ChannelPathPoint> secondary_points =
            make_channel_path_points(context.domain.hidden_desc, secondary_trunk, 0.92F);
        paint_channel_path(fields.river_trunk, secondary_points, context.domain,
                           settings.trunk_core_radius_cells,
                           settings.trunk_falloff_radius_cells, context.routing_surface,
                           seed + 3301U + (secondary_index * 131U),
                           settings.trunk_offset_cells);
        const std::vector<int> secondary_indices =
            path_point_indices(context.domain.hidden_desc, secondary_points);
        append_unique_indices(trunk_indices, secondary_indices);
        for (const int index : secondary_indices) {
            active[static_cast<std::size_t>(index)] = true;
        }
        ++secondary_index;
    }

    add_stream_order_seed_paths(fields, context, seed, settings, active);

    float trunk_accumulation = 1.0F;
    for (const int index : trunk_indices) {
        trunk_accumulation =
            std::max(trunk_accumulation,
                     context.accumulation.values()[static_cast<std::size_t>(index)]);
    }
    const float min_tributary_accumulation =
        std::max(settings.tributary_min_accumulation,
                 trunk_accumulation * settings.tributary_accumulation_fraction);
    const std::size_t max_tributary_count = std::max<std::size_t>(
        settings.min_tributary_count,
        trunk_indices.size() / std::max<std::size_t>(settings.tributary_count_divisor, 1U));
    std::size_t accepted_tributaries = 0U;

    for (const int trunk_index : trunk_indices) {
        if (accepted_tributaries >= max_tributary_count) {
            break;
        }
        const bool visible_anchor = is_inside_visible_crop(context.domain, trunk_index);
        std::vector<int> candidates = upstream[static_cast<std::size_t>(trunk_index)];
        std::sort(candidates.begin(), candidates.end(), [&context](int lhs, int rhs) {
            return context.accumulation.values()[static_cast<std::size_t>(lhs)] >
                   context.accumulation.values()[static_cast<std::size_t>(rhs)];
        });

        for (const int candidate : candidates) {
            if (active[static_cast<std::size_t>(candidate)]) {
                continue;
            }
            std::vector<int> branch = trace_strongest_upstream_path(
                candidate, upstream, context.accumulation, min_tributary_accumulation, active);
            if (branch.size() < settings.min_tributary_path_samples) {
                continue;
            }
            if (!visible_anchor && !path_intersects_visible_crop(context.domain, branch)) {
                continue;
            }
            std::vector<ChannelPathPoint> branch_points =
                trace_flow_streamline(candidate, context.routing.flow_direction, -1.0F,
                                      settings.tributary_strength);
            if (branch_points.size() < settings.min_tributary_path_samples) {
                continue;
            }
            std::reverse(branch_points.begin(), branch_points.end());
            if (!visible_anchor && visible_path_sample_count(context.domain, branch_points) == 0U) {
                continue;
            }
            paint_channel_path(fields.tributaries, branch_points, context.domain,
                               settings.tributary_core_radius_cells,
                               settings.tributary_falloff_radius_cells, context.routing_surface,
                               seed + 2203U + (accepted_tributaries * 117U),
                               settings.tributary_offset_cells);
            for (const int index : path_point_indices(context.domain.hidden_desc, branch_points)) {
                active[static_cast<std::size_t>(index)] = true;
            }
            ++accepted_tributaries;
            break;
        }
    }

    combine_river_mask(fields);
}

void activate_river_network(RiverFields& fields, const RoutingContext& context, std::uint64_t seed,
                            const RiverNetworkSettings& settings) {
    struct CorridorTrunkCandidate {
        std::size_t corridor_index = 0U;
        std::vector<int> indices{};
        std::vector<ChannelPathPoint> points{};
        float score = -std::numeric_limits<float>::infinity();
    };

    const std::vector<int>& downstream = context.d8_routing.downstream;
    const std::vector<std::vector<int>> upstream = make_upstream_adjacency(downstream);
    const RiverCorridorSelection selection = select_stream_order_corridors(context, settings);
    if (selection.corridors.empty()) {
        activate_river_network_fallback(fields, context, seed, settings);
        return;
    }

    std::vector<bool> active(context.accumulation.sample_count(), false);
    std::vector<CorridorTrunkCandidate> candidates;
    candidates.reserve(selection.corridors.size());
    for (std::size_t corridor_index = 0U; corridor_index < selection.corridors.size();
         ++corridor_index) {
        const RiverCorridor& corridor = selection.corridors[corridor_index];
        std::vector<int> trunk =
            trace_best_corridor_trunk(context, corridor, selection, upstream, settings);
        if (visible_grid_path_sample_count(context.domain, trunk) < 24U) {
            continue;
        }
        std::vector<ChannelPathPoint> trunk_points =
            make_corridor_channel_points(context, trunk, 0.82F);
        snap_trunk_endpoints_to_visible_edges(trunk_points, context.domain);
        const float edge_score =
            static_cast<float>(visible_path_edge_touch_count(context.domain, trunk_points)) *
            650'000.0F;
        const float length_score =
            static_cast<float>(visible_path_sample_count(context.domain, trunk_points)) *
            20'000.0F;
        const float grid_score =
            score_grid_trunk(context.accumulation, context.domain, trunk, trunk.front());
        candidates.push_back(CorridorTrunkCandidate{
            .corridor_index = corridor_index,
            .indices = std::move(trunk),
            .points = std::move(trunk_points),
            .score = edge_score + length_score + grid_score,
        });
    }

    if (candidates.empty()) {
        activate_river_network_fallback(fields, context, seed, settings);
        return;
    }
    std::sort(candidates.begin(), candidates.end(),
              [](const CorridorTrunkCandidate& lhs, const CorridorTrunkCandidate& rhs) {
                  return lhs.score > rhs.score;
              });

    RiverCorridorSelection rendered_selection{
        .selected = std::vector<bool>(context.accumulation.sample_count(), false),
        .trunk_support = std::vector<bool>(context.accumulation.sample_count(), false),
    };
    std::vector<std::vector<int>> trunk_paths;
    const std::size_t render_count =
        std::min(settings.corridor_render_count, candidates.size());
    trunk_paths.reserve(render_count);
    for (std::size_t candidate_index = 0U; candidate_index < render_count; ++candidate_index) {
        CorridorTrunkCandidate& candidate = candidates[candidate_index];
        const RiverCorridor& corridor = selection.corridors[candidate.corridor_index];
        for (const int index : corridor.cells) {
            const std::size_t sample = static_cast<std::size_t>(index);
            rendered_selection.selected[sample] = true;
            rendered_selection.trunk_support[sample] = selection.trunk_support[sample];
        }
        rendered_selection.corridors.push_back(corridor);
        const std::size_t trunk_ordinal = trunk_paths.size();
        paint_channel_path(fields.river_trunk, std::move(candidate.points), context.domain,
                           settings.trunk_core_radius_cells,
                           settings.trunk_falloff_radius_cells, context.routing_surface,
                           seed + 8101U + (trunk_ordinal * 149U),
                           settings.trunk_offset_cells);
        mark_active_indices(active, candidate.indices);
        trunk_paths.push_back(std::move(candidate.indices));
    }

    if (trunk_paths.empty()) {
        activate_river_network_fallback(fields, context, seed, settings);
        return;
    }

    const std::size_t accepted_tributaries =
        add_corridor_tributaries(fields, context, rendered_selection, upstream, trunk_paths,
                                 settings, seed, active);
    static_cast<void>(accepted_tributaries);
    combine_river_mask(fields);
}

void populate_sink_mask(cubey::procedural::ScalarField2D& sink_mask,
                        const std::vector<int>& downstream) {
    const cubey::procedural::Grid2DDesc& desc = sink_mask.desc();
    for (std::uint32_t y = 0; y < desc.height; ++y) {
        for (std::uint32_t x = 0; x < desc.width; ++x) {
            const std::size_t index = sink_mask.index(x, y);
            sink_mask.at(x, y) = downstream[index] < 0 ? 1.0F : 0.0F;
        }
    }
}

[[nodiscard]] cubey::procedural::ScalarField2D make_stream_order_field(
    const cubey::procedural::ScalarField2D& accumulation) {
    const cubey::procedural::Grid2DDesc& desc = accumulation.desc();
    cubey::procedural::ScalarField2D stream_order(desc, 1.0F);
    const float max_accumulation = std::max(accumulation.summarize().max, 1.0F);
    const float log_max_accumulation = std::log1p(max_accumulation);
    for (std::uint32_t y = 0; y < desc.height; ++y) {
        for (std::uint32_t x = 0; x < desc.width; ++x) {
            const float discharge =
                log_max_accumulation <= 0.0F ? 0.0F : std::log1p(accumulation.at(x, y)) /
                                                        log_max_accumulation;
            stream_order.at(x, y) = 1.0F + (discharge > 0.42F ? 1.0F : 0.0F) +
                                    (discharge > 0.56F ? 1.0F : 0.0F) +
                                    (discharge > 0.70F ? 1.0F : 0.0F) +
                                    (discharge > 0.84F ? 1.0F : 0.0F);
        }
    }
    return stream_order;
}

[[nodiscard]] RoutingContext make_routing_context(cubey::procedural::Grid2DDesc visible_desc,
                                                  std::uint64_t seed) {
    RoutingContext context{
        .domain = make_routing_domain(visible_desc),
    };
    const cubey::procedural::ScalarField2D routing_height =
        make_routing_source_height(context.domain.hidden_desc, seed);
    context.routing_surface = make_drainage_routing_surface(routing_height, seed);
    context.d8_routing = route_steepest_descent(context.routing_surface);
    context.routing = route_dinfinity_descent(context.routing_surface);
    context.accumulation = accumulate_flow(context.routing_surface, context.routing.receivers);
    context.stream_order = make_stream_order_field(context.accumulation);
    context.sink_mask = cubey::procedural::ScalarField2D(context.domain.hidden_desc, 0.0F);
    populate_sink_mask(context.sink_mask, context.routing.downstream);
    return context;
}

[[nodiscard]] cubey::procedural::ScalarField2D make_visible_sink_mask(
    const RoutingContext& context) {
    cubey::procedural::ScalarField2D sink_mask(context.domain.visible_desc, 0.0F);
    for (std::uint32_t y = 0; y < context.domain.visible_desc.height; ++y) {
        for (std::uint32_t x = 0; x < context.domain.visible_desc.width; ++x) {
            const std::uint32_t hidden_x = x + context.domain.padding_x;
            const std::uint32_t hidden_y = y + context.domain.padding_y;
            const std::size_t hidden_index =
                cubey::procedural::grid_index(hidden_x, hidden_y, context.domain.hidden_desc.width);
            const int downstream = context.routing.downstream[hidden_index];
            sink_mask.at(x, y) =
                downstream < 0 || !is_inside_visible_crop(context.domain, downstream) ? 1.0F
                                                                                      : 0.0F;
        }
    }
    return sink_mask;
}

[[nodiscard]] RiverFields make_river_fields(const cubey::procedural::ScalarField2D& height,
                                            const cubey::procedural::ScalarField2D& slope,
                                            const TerrainRegionConfig& config) {
    const cubey::procedural::Grid2DDesc& desc = height.desc();
    const RoutingContext context = make_routing_context(desc, config.seed);
    const RiverNetworkSettings settings = river_network_settings(config.recipe_id);
    RiverFields fields{
        .drainage_potential = crop_hidden_field_to_visible(context.routing_surface, context.domain),
        .flow_direction = crop_hidden_field_to_visible(context.routing.flow_direction,
                                                       context.domain),
        .flow_accumulation = crop_hidden_field_to_visible(context.accumulation, context.domain),
        .stream_order = crop_hidden_field_to_visible(context.stream_order, context.domain),
        .river_mask = cubey::procedural::ScalarField2D(desc, 0.0F),
        .river_trunk = cubey::procedural::ScalarField2D(desc, 0.0F),
        .tributaries = cubey::procedural::ScalarField2D(desc, 0.0F),
        .sink_mask = make_visible_sink_mask(context),
        .channel_width = cubey::procedural::ScalarField2D(desc, 0.0F),
        .valley_width = cubey::procedural::ScalarField2D(desc, 0.0F),
        .wetness = cubey::procedural::ScalarField2D(desc, 0.0F),
        .deposition = cubey::procedural::ScalarField2D(desc, 0.0F),
    };
    const float max_accumulation = std::max(fields.flow_accumulation.summarize().max, 1.0F);
    const float log_max_accumulation = std::log1p(max_accumulation);

    activate_river_network(fields, context, config.seed, settings);

    for (std::uint32_t y = 0; y < desc.height; ++y) {
        for (std::uint32_t x = 0; x < desc.width; ++x) {
            const float accumulation = fields.flow_accumulation.at(x, y);
            const float discharge =
                log_max_accumulation <= 0.0F ? 0.0F : std::log1p(accumulation) /
                                                        log_max_accumulation;
            const float active_river = fields.river_mask.at(x, y);
            fields.channel_width.at(x, y) =
                active_river * cubey::procedural::lerp(5.0F, 58.0F, discharge * discharge);
            fields.valley_width.at(x, y) =
                active_river * cubey::procedural::lerp(70.0F, 420.0F,
                                                       std::pow(discharge, 1.35F));
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
        .drainage_potential = std::move(fields.drainage_potential),
        .flow_direction = std::move(fields.flow_direction),
        .flow_accumulation = std::move(fields.flow_accumulation),
        .stream_order = std::move(fields.stream_order),
        .river_mask = std::move(fields.river_mask),
        .river_trunk = std::move(fields.river_trunk),
        .tributaries = std::move(fields.tributaries),
        .sink_mask = std::move(fields.sink_mask),
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

    TerrainSourceFields source_fields = make_terrain_source_fields(desc, config.seed);
    const cubey::procedural::SlopeCurvature2D slope_curvature =
        cubey::procedural::compute_slope_curvature(source_fields.height);
    const cubey::procedural::LocalRelief2D local_relief =
        cubey::procedural::compute_local_relief(source_fields.height, 4U);
    RiverFields river_fields =
        make_river_fields(source_fields.height, slope_curvature.slope, config);
    cubey::procedural::ScalarField2D material_rock =
        make_material_field(source_fields.height, slope_curvature.slope, river_fields.wetness,
                            source_fields.ridge_uplift, kTerrainFieldMaterialRock);
    cubey::procedural::ScalarField2D material_soil =
        make_material_field(source_fields.height, slope_curvature.slope, river_fields.wetness,
                            source_fields.ridge_uplift, kTerrainFieldMaterialSoil);
    cubey::procedural::ScalarField2D material_grass =
        make_material_field(source_fields.height, slope_curvature.slope, river_fields.wetness,
                            source_fields.ridge_uplift, kTerrainFieldMaterialGrass);
    cubey::procedural::ScalarField2D vegetation_potential =
        make_vegetation_potential_field(slope_curvature.slope, river_fields.wetness,
                                        material_grass);

    add_field(product.fields, kTerrainFieldBaseElevation, std::move(source_fields.base_elevation));
    add_field(product.fields, kTerrainFieldBroadRelief, std::move(source_fields.broad_relief));
    add_field(product.fields, kTerrainFieldRidgeUplift, std::move(source_fields.ridge_uplift));
    add_field(product.fields, kTerrainFieldDetailResidual, std::move(source_fields.detail_residual));
    add_field(product.fields, kTerrainFieldHeightM, std::move(source_fields.height));
    add_field(product.fields, kTerrainFieldSlope, slope_curvature.slope);
    add_field(product.fields, kTerrainFieldCurvature, slope_curvature.curvature);
    add_field(product.fields, kTerrainFieldLocalRelief, local_relief.local_span);
    add_field(product.fields, kTerrainFieldDrainagePotential,
              std::move(river_fields.drainage_potential));
    add_field(product.fields, kTerrainFieldFlowDirection, std::move(river_fields.flow_direction));
    add_field(product.fields, kTerrainFieldFlowAccumulation,
              std::move(river_fields.flow_accumulation));
    add_field(product.fields, kTerrainFieldStreamOrder, std::move(river_fields.stream_order));
    add_field(product.fields, kTerrainFieldRiverMask, std::move(river_fields.river_mask));
    add_field(product.fields, kTerrainFieldRiverTrunk, std::move(river_fields.river_trunk));
    add_field(product.fields, kTerrainFieldTributaries, std::move(river_fields.tributaries));
    add_field(product.fields, kTerrainFieldSinkMask, std::move(river_fields.sink_mask));
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
