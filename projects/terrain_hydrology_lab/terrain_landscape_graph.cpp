#include "terrain_landscape_graph.h"

#include <cubey/procedural/seed.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numeric>
#include <queue>
#include <stdexcept>
#include <utility>
#include <vector>

namespace cubey::projects::terrain_hydrology_lab {
namespace {

inline constexpr std::array<int, 4> kOffsetX{1, 0, -1, 0};
inline constexpr std::array<int, 4> kOffsetY{0, 1, 0, -1};

struct FloodResult {
    cubey::procedural::ScalarField2D routing_surface_m{};
    cubey::procedural::ScalarField2D breach_mask{};
    std::vector<int> escape_parent{};
};

[[nodiscard]] bool in_bounds(const cubey::procedural::Grid2DDesc& desc, int x, int y) {
    return x >= 0 && y >= 0 && x < static_cast<int>(desc.width) &&
           y < static_cast<int>(desc.height);
}

[[nodiscard]] bool is_boundary(const cubey::procedural::Grid2DDesc& desc, std::uint32_t x,
                               std::uint32_t y) {
    return x == 0U || y == 0U || x + 1U == desc.width || y + 1U == desc.height;
}

void validate_field(const cubey::procedural::ScalarField2D& field) {
    const cubey::procedural::Grid2DDesc& desc = field.desc();
    if (desc.width < 3U || desc.height < 3U) {
        throw std::runtime_error("terrain landscape graph requires dimensions of at least 3");
    }
    if (!std::isfinite(desc.cell_size) || desc.cell_size <= 0.0F || !std::isfinite(desc.origin_x) ||
        !std::isfinite(desc.origin_y)) {
        throw std::runtime_error("terrain landscape graph requires a finite world-space grid");
    }
    if (std::any_of(field.values().begin(), field.values().end(),
                    [](float value) { return !std::isfinite(value); })) {
        throw std::runtime_error("terrain landscape graph field samples must be finite");
    }
}

[[nodiscard]] FloodResult
priority_flood_breach_routes(const cubey::procedural::ScalarField2D& height_m) {
    struct QueueItem {
        float height = 0.0F;
        std::size_t index = 0U;
    };
    struct QueueGreater {
        bool operator()(const QueueItem& lhs, const QueueItem& rhs) const {
            if (lhs.height == rhs.height) {
                return lhs.index > rhs.index;
            }
            return lhs.height > rhs.height;
        }
    };

    const cubey::procedural::Grid2DDesc& desc = height_m.desc();
    FloodResult result{
        .routing_surface_m = height_m,
        .breach_mask = cubey::procedural::ScalarField2D(desc, 0.0F),
        .escape_parent = std::vector<int>(height_m.sample_count(), -1),
    };
    std::vector<bool> visited(height_m.sample_count(), false);
    std::priority_queue<QueueItem, std::vector<QueueItem>, QueueGreater> frontier;
    const float epsilon = std::max(desc.cell_size * 0.00001F, 0.001F);

    const auto push_boundary = [&](std::uint32_t x, std::uint32_t y) {
        const std::size_t index = height_m.index(x, y);
        if (visited[index]) {
            return;
        }
        visited[index] = true;
        frontier.push({.height = height_m.values()[index], .index = index});
    };
    for (std::uint32_t x = 0U; x < desc.width; ++x) {
        push_boundary(x, 0U);
        push_boundary(x, desc.height - 1U);
    }
    for (std::uint32_t y = 0U; y < desc.height; ++y) {
        push_boundary(0U, y);
        push_boundary(desc.width - 1U, y);
    }

    while (!frontier.empty()) {
        const QueueItem current = frontier.top();
        frontier.pop();
        const auto current_x = static_cast<std::uint32_t>(current.index % desc.width);
        const auto current_y = static_cast<std::uint32_t>(current.index / desc.width);
        for (std::size_t direction = 0U; direction < kOffsetX.size(); ++direction) {
            const int neighbor_x = static_cast<int>(current_x) + kOffsetX[direction];
            const int neighbor_y = static_cast<int>(current_y) + kOffsetY[direction];
            if (!in_bounds(desc, neighbor_x, neighbor_y)) {
                continue;
            }
            const auto x = static_cast<std::uint32_t>(neighbor_x);
            const auto y = static_cast<std::uint32_t>(neighbor_y);
            const std::size_t index = height_m.index(x, y);
            if (visited[index]) {
                continue;
            }
            visited[index] = true;
            result.escape_parent[index] = static_cast<int>(current.index);
            const float raw_height = height_m.values()[index];
            const float routing_height = std::max(raw_height, current.height + epsilon);
            result.routing_surface_m.values()[index] = routing_height;
            if (routing_height > raw_height + epsilon * 0.5F) {
                result.breach_mask.values()[index] = 1.0F;
            }
            frontier.push({.height = routing_height, .index = index});
        }
    }
    return result;
}

[[nodiscard]] std::uint64_t coordinate_salt(const cubey::procedural::Grid2DDesc& desc,
                                            std::uint32_t x, std::uint32_t y, std::uint32_t level) {
    const auto world_x = static_cast<std::int64_t>(
        std::llround(cubey::procedural::grid_sample_x(desc, x) / desc.cell_size));
    const auto world_y = static_cast<std::int64_t>(
        std::llround(cubey::procedural::grid_sample_y(desc, y) / desc.cell_size));
    const std::uint64_t packed =
        (static_cast<std::uint64_t>(static_cast<std::uint32_t>(world_x)) << 32U) |
        static_cast<std::uint32_t>(world_y);
    return packed ^ (static_cast<std::uint64_t>(level) * 0x9e37'79b9'7f4a'7c15ULL);
}

[[nodiscard]] float coordinate_random01(std::uint64_t seed, std::string_view domain,
                                        const cubey::procedural::Grid2DDesc& desc, std::uint32_t x,
                                        std::uint32_t y, std::uint32_t level,
                                        std::uint64_t channel) {
    const std::uint64_t salt =
        coordinate_salt(desc, x, y, level) ^ (channel * 0xbf58'476d'1ce4'e5b9ULL);
    constexpr float kInv24Bit = 1.0F / 16'777'215.0F;
    return static_cast<float>(cubey::procedural::derive_seed(seed, domain, salt) >> 40U) *
           kInv24Bit;
}

[[nodiscard]] int choose_receiver(const FloodResult& flood, std::uint32_t x, std::uint32_t y,
                                  std::uint64_t seed, std::uint32_t level) {
    const cubey::procedural::Grid2DDesc& desc = flood.routing_surface_m.desc();
    const std::size_t index = flood.routing_surface_m.index(x, y);
    if (flood.breach_mask.values()[index] > 0.5F && flood.escape_parent[index] >= 0) {
        return flood.escape_parent[index];
    }

    std::array<int, 4> candidates{-1, -1, -1, -1};
    std::array<float, 4> weights{};
    std::size_t count = 0U;
    float total_weight = 0.0F;
    const float center = flood.routing_surface_m.values()[index];
    for (std::size_t direction = 0U; direction < kOffsetX.size(); ++direction) {
        const int neighbor_x = static_cast<int>(x) + kOffsetX[direction];
        const int neighbor_y = static_cast<int>(y) + kOffsetY[direction];
        if (!in_bounds(desc, neighbor_x, neighbor_y)) {
            continue;
        }
        const std::size_t neighbor = flood.routing_surface_m.index(
            static_cast<std::uint32_t>(neighbor_x), static_cast<std::uint32_t>(neighbor_y));
        const float drop = center - flood.routing_surface_m.values()[neighbor];
        if (drop <= 0.0F) {
            continue;
        }
        candidates[count] = static_cast<int>(neighbor);
        weights[count] = drop;
        total_weight += drop;
        ++count;
    }
    if (count == 0U) {
        return flood.escape_parent[index];
    }
    const float sample =
        coordinate_random01(seed, "terrain.landscape.receiver", desc, x, y, level, 0U) *
        total_weight;
    float cumulative = 0.0F;
    for (std::size_t candidate = 0U; candidate < count; ++candidate) {
        cumulative += weights[candidate];
        if (sample <= cumulative || candidate + 1U == count) {
            return candidates[candidate];
        }
    }
    return candidates[count - 1U];
}

[[nodiscard]] float bilinear_sample(const cubey::procedural::ScalarField2D& field, float x,
                                    float y) {
    const cubey::procedural::Grid2DDesc& desc = field.desc();
    const float clamped_x = std::clamp(x, 0.0F, static_cast<float>(desc.width - 1U));
    const float clamped_y = std::clamp(y, 0.0F, static_cast<float>(desc.height - 1U));
    const auto x0 = static_cast<std::uint32_t>(std::floor(clamped_x));
    const auto y0 = static_cast<std::uint32_t>(std::floor(clamped_y));
    const std::uint32_t x1 = std::min(x0 + 1U, desc.width - 1U);
    const std::uint32_t y1 = std::min(y0 + 1U, desc.height - 1U);
    const float tx = clamped_x - static_cast<float>(x0);
    const float ty = clamped_y - static_cast<float>(y0);
    const float a = std::lerp(field.at(x0, y0), field.at(x1, y0), tx);
    const float b = std::lerp(field.at(x0, y1), field.at(x1, y1), tx);
    return std::lerp(a, b, ty);
}

} // namespace

TerrainLandscapeGraph
build_terrain_landscape_graph(const cubey::procedural::ScalarField2D& height_m, std::uint64_t seed,
                              std::uint32_t multigrid_level) {
    validate_field(height_m);
    FloodResult flood = priority_flood_breach_routes(height_m);
    const cubey::procedural::Grid2DDesc& desc = height_m.desc();
    TerrainLandscapeGraph result{
        .routing_surface_m = flood.routing_surface_m,
        .breach_mask = flood.breach_mask,
        .drainage_area_m2 = cubey::procedural::ScalarField2D(desc, desc.cell_size * desc.cell_size),
        .flow_direction_x = cubey::procedural::ScalarField2D(desc, 0.0F),
        .flow_direction_z = cubey::procedural::ScalarField2D(desc, 0.0F),
        .slope_correction = cubey::procedural::ScalarField2D(desc, 1.0F),
        .receiver = std::vector<int>(height_m.sample_count(), -1),
        .upstream_to_downstream = std::vector<std::size_t>(height_m.sample_count()),
    };

    for (std::uint32_t y = 0U; y < desc.height; ++y) {
        for (std::uint32_t x = 0U; x < desc.width; ++x) {
            if (!is_boundary(desc, x, y)) {
                result.receiver[height_m.index(x, y)] =
                    choose_receiver(flood, x, y, seed, multigrid_level);
            }
        }
    }

    std::iota(result.upstream_to_downstream.begin(), result.upstream_to_downstream.end(), 0U);
    std::sort(result.upstream_to_downstream.begin(), result.upstream_to_downstream.end(),
              [&result](std::size_t lhs, std::size_t rhs) {
                  const float left = result.routing_surface_m.values()[lhs];
                  const float right = result.routing_surface_m.values()[rhs];
                  if (left == right) {
                      return lhs < rhs;
                  }
                  return left > right;
              });
    result.downstream_to_upstream.assign(result.upstream_to_downstream.rbegin(),
                                         result.upstream_to_downstream.rend());

    for (const std::size_t index : result.upstream_to_downstream) {
        const int receiver = result.receiver[index];
        if (receiver >= 0) {
            result.drainage_area_m2.values()[static_cast<std::size_t>(receiver)] +=
                result.drainage_area_m2.values()[index];
        }
    }

    const double cell_area = static_cast<double>(desc.cell_size) * desc.cell_size;
    result.total_input_area_m2 = cell_area * static_cast<double>(height_m.sample_count());
    for (std::uint32_t y = 0U; y < desc.height; ++y) {
        for (std::uint32_t x = 0U; x < desc.width; ++x) {
            const std::size_t index = height_m.index(x, y);
            const int receiver = result.receiver[index];
            if (receiver < 0) {
                result.terminal_outflow_area_m2 += result.drainage_area_m2.values()[index];
                if (!is_boundary(desc, x, y)) {
                    ++result.unresolved_sink_count;
                }
                continue;
            }
            const auto target = static_cast<std::size_t>(receiver);
            const int target_x = static_cast<int>(target % desc.width);
            const int target_y = static_cast<int>(target / desc.width);
            result.flow_direction_x.values()[index] =
                static_cast<float>(target_x - static_cast<int>(x));
            result.flow_direction_z.values()[index] =
                static_cast<float>(target_y - static_cast<int>(y));

            float gradient_x = 0.0F;
            float gradient_y = 0.0F;
            float lower_x_count = 0.0F;
            float lower_y_count = 0.0F;
            const float center = result.routing_surface_m.values()[index];
            for (std::size_t direction = 0U; direction < kOffsetX.size(); ++direction) {
                const int neighbor_x = static_cast<int>(x) + kOffsetX[direction];
                const int neighbor_y = static_cast<int>(y) + kOffsetY[direction];
                if (!in_bounds(desc, neighbor_x, neighbor_y)) {
                    continue;
                }
                const float drop =
                    center - result.routing_surface_m.at(static_cast<std::uint32_t>(neighbor_x),
                                                         static_cast<std::uint32_t>(neighbor_y));
                if (drop <= 0.0F) {
                    continue;
                }
                if (kOffsetX[direction] != 0) {
                    gradient_x += drop;
                    lower_x_count += 1.0F;
                } else {
                    gradient_y += drop;
                    lower_y_count += 1.0F;
                }
            }
            gradient_x /= std::max(lower_x_count, 1.0F);
            gradient_y /= std::max(lower_y_count, 1.0F);
            const float full_drop = std::hypot(gradient_x, gradient_y);
            const float receiver_drop = center - result.routing_surface_m.values()[target];
            if (receiver_drop > 0.000001F) {
                result.slope_correction.values()[index] =
                    std::clamp(full_drop / receiver_drop, 0.25F, 4.0F);
            }
        }
    }
    return result;
}

cubey::procedural::ScalarField2D
downsample_terrain_landscape_field(const cubey::procedural::ScalarField2D& field) {
    validate_field(field);
    const cubey::procedural::Grid2DDesc& source = field.desc();
    const cubey::procedural::Grid2DDesc target{
        .width = ((source.width - 1U) / 2U) + 1U,
        .height = ((source.height - 1U) / 2U) + 1U,
        .cell_size = source.cell_size * 2.0F,
        .origin_x = source.origin_x,
        .origin_y = source.origin_y,
    };
    cubey::procedural::ScalarField2D result(target, 0.0F);
    constexpr std::array<float, 3> weights{1.0F, 2.0F, 1.0F};
    for (std::uint32_t y = 0U; y < target.height; ++y) {
        for (std::uint32_t x = 0U; x < target.width; ++x) {
            const int center_x = static_cast<int>(std::min(x * 2U, source.width - 1U));
            const int center_y = static_cast<int>(std::min(y * 2U, source.height - 1U));
            float weighted_sum = 0.0F;
            float weight_sum = 0.0F;
            for (int oy = -1; oy <= 1; ++oy) {
                for (int ox = -1; ox <= 1; ++ox) {
                    const int sample_x =
                        std::clamp(center_x + ox, 0, static_cast<int>(source.width) - 1);
                    const int sample_y =
                        std::clamp(center_y + oy, 0, static_cast<int>(source.height) - 1);
                    const float weight = weights[static_cast<std::size_t>(ox + 1)] *
                                         weights[static_cast<std::size_t>(oy + 1)];
                    weighted_sum += field.at(static_cast<std::uint32_t>(sample_x),
                                             static_cast<std::uint32_t>(sample_y)) *
                                    weight;
                    weight_sum += weight;
                }
            }
            result.at(x, y) = weighted_sum / weight_sum;
        }
    }
    return result;
}

cubey::procedural::ScalarField2D
upsample_terrain_landscape_field(const cubey::procedural::ScalarField2D& field, std::uint64_t seed,
                                 std::uint32_t multigrid_level, float jitter_cells) {
    validate_field(field);
    if (!std::isfinite(jitter_cells) || jitter_cells < 0.0F || jitter_cells > 0.5F) {
        throw std::runtime_error("terrain landscape upsampling jitter must be in [0, 0.5]");
    }
    const cubey::procedural::Grid2DDesc& source = field.desc();
    const cubey::procedural::Grid2DDesc target{
        .width = ((source.width - 1U) * 2U) + 1U,
        .height = ((source.height - 1U) * 2U) + 1U,
        .cell_size = source.cell_size * 0.5F,
        .origin_x = source.origin_x,
        .origin_y = source.origin_y,
    };
    cubey::procedural::ScalarField2D result(target, 0.0F);
    for (std::uint32_t y = 0U; y < target.height; ++y) {
        for (std::uint32_t x = 0U; x < target.width; ++x) {
            float source_x = static_cast<float>(x) * 0.5F;
            float source_y = static_cast<float>(y) * 0.5F;
            if (!is_boundary(target, x, y) && jitter_cells > 0.0F) {
                source_x += (coordinate_random01(seed, "terrain.landscape.upsample", target, x, y,
                                                 multigrid_level, 0U) *
                                 2.0F -
                             1.0F) *
                            jitter_cells;
                source_y += (coordinate_random01(seed, "terrain.landscape.upsample", target, x, y,
                                                 multigrid_level, 1U) *
                                 2.0F -
                             1.0F) *
                            jitter_cells;
            }
            result.at(x, y) = bilinear_sample(field, source_x, source_y);
        }
    }
    return result;
}

float terrain_landscape_basin_discontinuity_coverage(
    const cubey::procedural::ScalarField2D& height_m, const TerrainLandscapeGraph& graph,
    float minimum_excess_m) {
    if (!cubey::procedural::same_grid_desc(height_m.desc(), graph.routing_surface_m.desc()) ||
        graph.receiver.size() != height_m.sample_count()) {
        throw std::runtime_error("terrain landscape discontinuity inputs must share a grid");
    }
    if (!std::isfinite(minimum_excess_m) || minimum_excess_m < 0.0F) {
        throw std::runtime_error("terrain landscape discontinuity threshold must be nonnegative");
    }
    const cubey::procedural::Grid2DDesc& desc = height_m.desc();
    std::size_t discontinuities = 0U;
    std::size_t interior_count = 0U;
    for (std::uint32_t y = 1U; y + 1U < desc.height; ++y) {
        for (std::uint32_t x = 1U; x + 1U < desc.width; ++x) {
            ++interior_count;
            const std::size_t index = height_m.index(x, y);
            const int receiver = graph.receiver[index];
            if (receiver < 0) {
                continue;
            }
            const float receiver_drop = std::max(
                height_m.values()[index] - height_m.values()[static_cast<std::size_t>(receiver)],
                0.0F);
            bool discontinuity = false;
            for (std::size_t direction = 0U; direction < kOffsetX.size(); ++direction) {
                const auto neighbor_x =
                    static_cast<std::uint32_t>(static_cast<int>(x) + kOffsetX[direction]);
                const auto neighbor_y =
                    static_cast<std::uint32_t>(static_cast<int>(y) + kOffsetY[direction]);
                const std::size_t neighbor = height_m.index(neighbor_x, neighbor_y);
                if (neighbor == static_cast<std::size_t>(receiver) ||
                    graph.receiver[neighbor] == static_cast<int>(index)) {
                    continue;
                }
                const float excess =
                    height_m.values()[index] - height_m.values()[neighbor] - receiver_drop;
                if (excess > minimum_excess_m) {
                    discontinuity = true;
                    break;
                }
            }
            discontinuities += static_cast<std::size_t>(discontinuity);
        }
    }
    return interior_count > 0U
               ? static_cast<float>(discontinuities) / static_cast<float>(interior_count)
               : 0.0F;
}

} // namespace cubey::projects::terrain_hydrology_lab
