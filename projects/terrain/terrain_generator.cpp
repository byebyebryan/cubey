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
    FlowRoutingResult routing{};
    cubey::procedural::ScalarField2D accumulation{};
    cubey::procedural::ScalarField2D stream_order{};
    cubey::procedural::ScalarField2D sink_mask{};
};

struct ChannelPathPoint {
    float x = 0.0F;
    float y = 0.0F;
    float strength = 1.0F;
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

[[nodiscard]] int select_main_channel_seed(const cubey::procedural::ScalarField2D& accumulation,
                                           const RoutingDomain& domain) {
    int best_index = -1;
    float best_accumulation = -1.0F;
    for (std::uint32_t y = 0; y < domain.visible_desc.height; ++y) {
        for (std::uint32_t x = 0; x < domain.visible_desc.width; ++x) {
            const std::uint32_t hidden_x = x + domain.padding_x;
            const std::uint32_t hidden_y = y + domain.padding_y;
            const std::size_t index =
                cubey::procedural::grid_index(hidden_x, hidden_y, domain.hidden_desc.width);
            const float value = accumulation.values()[index];
            if (value > best_accumulation) {
                best_accumulation = value;
                best_index = static_cast<int>(index);
            }
        }
    }
    return best_index;
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

[[nodiscard]] std::vector<int> trace_main_trunk(
    const cubey::procedural::ScalarField2D& accumulation,
    const std::vector<std::vector<int>>& upstream, const std::vector<int>& downstream,
    const RoutingDomain& domain) {
    const int seed = select_main_channel_seed(accumulation, domain);
    const std::vector<bool> no_exclusions(accumulation.sample_count(), false);
    const std::vector<int> upstream_path =
        trace_strongest_upstream_path(seed, upstream, accumulation, 1.0F, no_exclusions);
    std::vector<int> downstream_path = trace_downstream_path(seed, downstream);
    std::reverse(downstream_path.begin(), downstream_path.end());

    std::vector<int> trunk = std::move(downstream_path);
    const std::size_t upstream_start = trunk.empty() ? 0U : 1U;
    for (std::size_t index = upstream_start; index < upstream_path.size(); ++index) {
        trunk.push_back(upstream_path[index]);
    }
    return trunk;
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

[[nodiscard]] std::vector<ChannelPathPoint> make_channel_path_points(
    const cubey::procedural::Grid2DDesc& desc, const std::vector<int>& path, float strength) {
    std::vector<ChannelPathPoint> points;
    points.reserve(path.size());
    for (const int index : path) {
        const auto uindex = static_cast<std::uint32_t>(index);
        points.push_back({
            .x = static_cast<float>(uindex % desc.width),
            .y = static_cast<float>(uindex / desc.width),
            .strength = strength,
        });
    }
    return points;
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

void apply_channel_lateral_offset(std::vector<ChannelPathPoint>& points, std::uint64_t seed,
                                  float max_offset_cells) {
    if (points.size() < 3U || max_offset_cells <= 0.0F) {
        return;
    }

    const cubey::procedural::NoiseSource2D source = channel_offset_source(seed);
    const float denominator = static_cast<float>(points.size() - 1U);
    for (std::size_t index = 1; index + 1U < points.size(); ++index) {
        const float t = static_cast<float>(index) / denominator;
        const float end_taper = cubey::procedural::smoothstep(0.0F, 0.24F, t) *
                                (1.0F - cubey::procedural::smoothstep(0.76F, 1.0F, t));
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
        points[index].x += nx * offset;
        points[index].y += ny * offset;
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

void paint_channel_path(cubey::procedural::ScalarField2D& field, const std::vector<int>& path,
                        const RoutingDomain& domain, float strength, float core_radius_cells,
                        float falloff_radius_cells, std::uint64_t seed, float max_offset_cells) {
    std::vector<ChannelPathPoint> points =
        make_channel_path_points(domain.hidden_desc, path, strength);
    smooth_channel_path(points, 3);
    apply_channel_lateral_offset(points, seed, max_offset_cells);
    rasterize_channel_segments(field, points, core_radius_cells, falloff_radius_cells, domain);
}

void activate_river_network(RiverFields& fields, const RoutingContext& context,
                            std::uint64_t seed) {
    const std::vector<int>& downstream = context.routing.downstream;
    const std::vector<std::vector<int>> upstream = make_upstream_adjacency(downstream);
    const std::vector<int> trunk =
        trace_main_trunk(context.accumulation, upstream, downstream, context.domain);
    paint_channel_path(fields.river_trunk, trunk, context.domain, 1.0F, 1.4F, 3.0F,
                       seed + 1101U, 1.25F);

    std::vector<bool> active(context.accumulation.sample_count(), false);
    for (const int index : trunk) {
        active[static_cast<std::size_t>(index)] = true;
    }

    float trunk_accumulation = 1.0F;
    for (const int index : trunk) {
        trunk_accumulation =
            std::max(trunk_accumulation,
                     context.accumulation.values()[static_cast<std::size_t>(index)]);
    }
    const float min_tributary_accumulation = std::max(4.0F, trunk_accumulation * 0.018F);
    const std::size_t max_tributary_count = std::max<std::size_t>(2U, trunk.size() / 18U);
    std::size_t accepted_tributaries = 0U;

    for (const int trunk_index : trunk) {
        if (accepted_tributaries >= max_tributary_count) {
            break;
        }
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
            if (branch.size() < 5U) {
                continue;
            }
            paint_channel_path(fields.tributaries, branch, context.domain, 0.75F, 0.9F, 2.1F,
                               seed + 2203U + (accepted_tributaries * 117U), 0.75F);
            for (const int index : branch) {
                active[static_cast<std::size_t>(index)] = true;
            }
            ++accepted_tributaries;
            break;
        }
    }

    for (std::uint32_t y = 0; y < fields.river_mask.desc().height; ++y) {
        for (std::uint32_t x = 0; x < fields.river_mask.desc().width; ++x) {
            fields.river_mask.at(x, y) =
                std::max(fields.river_trunk.at(x, y), fields.tributaries.at(x, y));
        }
    }
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
    context.routing = route_steepest_descent(context.routing_surface);
    context.accumulation = accumulate_flow(context.routing_surface, context.routing.downstream);
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
                                            std::uint64_t seed) {
    const cubey::procedural::Grid2DDesc& desc = height.desc();
    const RoutingContext context = make_routing_context(desc, seed);
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

    activate_river_network(fields, context, seed);

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
        make_river_fields(source_fields.height, slope_curvature.slope, config.seed);
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
