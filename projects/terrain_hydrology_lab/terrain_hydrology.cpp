#include "terrain_hydrology.h"

#include <cubey/procedural/operators.h>

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

inline constexpr float kPi = 3.14159265358979323846F;
inline constexpr float kTwoPi = kPi * 2.0F;
inline constexpr float kQuarterPi = kPi * 0.25F;
inline constexpr float kSqrtHalf = 0.70710678118654752440F;
inline constexpr std::array<int, 8> kOffsetX{1, 1, 0, -1, -1, -1, 0, 1};
inline constexpr std::array<int, 8> kOffsetZ{0, 1, 1, 1, 0, -1, -1, -1};

struct RoutingGraph {
    std::vector<std::array<TerrainFlowReceiver, 2>> receivers{};
    std::vector<int> primary{};
    cubey::procedural::ScalarField2D direction_x{};
    cubey::procedural::ScalarField2D direction_z{};
};

struct PriorityFloodResult {
    cubey::procedural::ScalarField2D surface{};
    cubey::procedural::ScalarField2D fill_delta{};
};

[[nodiscard]] bool in_bounds(const cubey::procedural::Grid2DDesc& desc, int x, int z) {
    return x >= 0 && z >= 0 && x < static_cast<int>(desc.width) &&
           z < static_cast<int>(desc.height);
}

[[nodiscard]] bool is_outer_boundary(const cubey::procedural::Grid2DDesc& desc, std::uint32_t x,
                                     std::uint32_t z) {
    return x == 0U || z == 0U || x + 1U == desc.width || z + 1U == desc.height;
}

[[nodiscard]] float neighbor_distance_m(const cubey::procedural::Grid2DDesc& desc, int dx, int dz) {
    return dx != 0 && dz != 0 ? desc.cell_size * 1.41421356237F : desc.cell_size;
}

void validate_hydrology_input(const cubey::procedural::ScalarField2D& height_m,
                              std::uint32_t core_border_samples) {
    const cubey::procedural::Grid2DDesc& desc = height_m.desc();
    if (desc.width < 3U || desc.height < 3U) {
        throw std::runtime_error("terrain hydrology requires dimensions of at least 3");
    }
    if (!std::isfinite(desc.cell_size) || desc.cell_size <= 0.0F) {
        throw std::runtime_error("terrain hydrology requires a finite positive cell size");
    }
    if (core_border_samples * 2U >= desc.width || core_border_samples * 2U >= desc.height) {
        throw std::runtime_error("terrain hydrology core border consumes the full grid");
    }
    if (std::any_of(height_m.values().begin(), height_m.values().end(),
                    [](float value) { return !std::isfinite(value); })) {
        throw std::runtime_error("terrain hydrology height samples must be finite");
    }
}

[[nodiscard]] PriorityFloodResult
priority_flood_open_boundary(const cubey::procedural::ScalarField2D& height_m) {
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
    PriorityFloodResult result{
        .surface = height_m,
        .fill_delta = cubey::procedural::ScalarField2D(desc, 0.0F),
    };
    std::priority_queue<QueueItem, std::vector<QueueItem>, QueueGreater> frontier;
    std::vector<bool> visited(height_m.sample_count(), false);
    const float epsilon = std::max(0.001F, desc.cell_size * 0.00001F);

    const auto push_boundary = [&](std::uint32_t x, std::uint32_t z) {
        const std::size_t index = height_m.index(x, z);
        if (visited[index]) {
            return;
        }
        visited[index] = true;
        frontier.push({.height = result.surface.values()[index], .index = index});
    };
    for (std::uint32_t x = 0; x < desc.width; ++x) {
        push_boundary(x, 0U);
        push_boundary(x, desc.height - 1U);
    }
    for (std::uint32_t z = 0; z < desc.height; ++z) {
        push_boundary(0U, z);
        push_boundary(desc.width - 1U, z);
    }

    while (!frontier.empty()) {
        const QueueItem current = frontier.top();
        frontier.pop();
        const auto current_x = static_cast<std::uint32_t>(current.index % desc.width);
        const auto current_z = static_cast<std::uint32_t>(current.index / desc.width);
        for (std::size_t direction = 0; direction < kOffsetX.size(); ++direction) {
            const int nx = static_cast<int>(current_x) + kOffsetX[direction];
            const int nz = static_cast<int>(current_z) + kOffsetZ[direction];
            if (!in_bounds(desc, nx, nz)) {
                continue;
            }
            const auto x = static_cast<std::uint32_t>(nx);
            const auto z = static_cast<std::uint32_t>(nz);
            const std::size_t index = height_m.index(x, z);
            if (visited[index]) {
                continue;
            }
            visited[index] = true;
            const float raw_height = height_m.values()[index];
            const float repaired_height = std::max(raw_height, current.height + epsilon);
            result.surface.values()[index] = repaired_height;
            result.fill_delta.values()[index] = repaired_height - raw_height;
            frontier.push({.height = repaired_height, .index = index});
        }
    }
    return result;
}

[[nodiscard]] TerrainFlowReceiver steepest_receiver(const cubey::procedural::ScalarField2D& surface,
                                                    std::uint32_t x, std::uint32_t z) {
    const cubey::procedural::Grid2DDesc& desc = surface.desc();
    const float center = surface.at(x, z);
    float best_drop_per_m = 0.0F;
    int best_index = -1;
    for (std::size_t direction = 0; direction < kOffsetX.size(); ++direction) {
        const int nx = static_cast<int>(x) + kOffsetX[direction];
        const int nz = static_cast<int>(z) + kOffsetZ[direction];
        if (!in_bounds(desc, nx, nz)) {
            continue;
        }
        const float drop =
            center - surface.at(static_cast<std::uint32_t>(nx), static_cast<std::uint32_t>(nz));
        const float drop_per_m =
            drop / neighbor_distance_m(desc, kOffsetX[direction], kOffsetZ[direction]);
        if (drop_per_m > best_drop_per_m) {
            best_drop_per_m = drop_per_m;
            best_index = static_cast<int>(cubey::procedural::grid_index(
                static_cast<std::uint32_t>(nx), static_cast<std::uint32_t>(nz), desc.width));
        }
    }
    return {.index = best_index, .weight = best_index >= 0 ? 1.0F : 0.0F};
}

void set_direction_from_receivers(RoutingGraph& graph, const cubey::procedural::Grid2DDesc& desc,
                                  std::uint32_t x, std::uint32_t z) {
    const std::size_t index = cubey::procedural::grid_index(x, z, desc.width);
    float direction_x = 0.0F;
    float direction_z = 0.0F;
    for (const TerrainFlowReceiver& receiver : graph.receivers[index]) {
        if (receiver.index < 0 || receiver.weight <= 0.0F) {
            continue;
        }
        const auto target = static_cast<std::uint32_t>(receiver.index);
        const int dx = static_cast<int>(target % desc.width) - static_cast<int>(x);
        const int dz = static_cast<int>(target / desc.width) - static_cast<int>(z);
        const float scale = dx != 0 && dz != 0 ? kSqrtHalf : 1.0F;
        direction_x += static_cast<float>(dx) * scale * receiver.weight;
        direction_z += static_cast<float>(dz) * scale * receiver.weight;
    }
    const float length = std::sqrt((direction_x * direction_x) + (direction_z * direction_z));
    if (length > 0.0F) {
        direction_x /= length;
        direction_z /= length;
    }
    graph.direction_x.at(x, z) = direction_x;
    graph.direction_z.at(x, z) = direction_z;
}

void assign_fractional_receivers(RoutingGraph& graph,
                                 const cubey::procedural::ScalarField2D& surface, std::uint32_t x,
                                 std::uint32_t z, float angle) {
    const cubey::procedural::Grid2DDesc& desc = surface.desc();
    const std::size_t index = surface.index(x, z);
    const float sector_position = angle / kQuarterPi;
    const int sector = static_cast<int>(std::floor(sector_position)) % 8;
    const int next_sector = (sector + 1) % 8;
    const float next_weight = sector_position - std::floor(sector_position);
    const std::array<int, 2> directions{sector, next_sector};
    const std::array<float, 2> weights{1.0F - next_weight, next_weight};
    std::size_t receiver_count = 0U;

    for (std::size_t candidate = 0; candidate < directions.size(); ++candidate) {
        if (weights[candidate] <= 0.0001F) {
            continue;
        }
        const std::size_t direction = static_cast<std::size_t>(directions[candidate]);
        const int nx = static_cast<int>(x) + kOffsetX[direction];
        const int nz = static_cast<int>(z) + kOffsetZ[direction];
        if (!in_bounds(desc, nx, nz)) {
            continue;
        }
        const auto target_x = static_cast<std::uint32_t>(nx);
        const auto target_z = static_cast<std::uint32_t>(nz);
        if (surface.at(target_x, target_z) >= surface.at(x, z)) {
            continue;
        }
        graph.receivers[index][receiver_count] = {
            .index = static_cast<int>(surface.index(target_x, target_z)),
            .weight = weights[candidate],
        };
        ++receiver_count;
    }

    if (receiver_count == 0U) {
        graph.receivers[index][0] = steepest_receiver(surface, x, z);
    } else if (receiver_count == 1U) {
        graph.receivers[index][0].weight = 1.0F;
    } else {
        const float total = graph.receivers[index][0].weight + graph.receivers[index][1].weight;
        graph.receivers[index][0].weight /= total;
        graph.receivers[index][1].weight /= total;
    }
    graph.primary[index] = graph.receivers[index][1].weight > graph.receivers[index][0].weight
                               ? graph.receivers[index][1].index
                               : graph.receivers[index][0].index;
    set_direction_from_receivers(graph, desc, x, z);
}

[[nodiscard]] RoutingGraph
route_fractional_descent(const cubey::procedural::ScalarField2D& surface) {
    const cubey::procedural::Grid2DDesc& desc = surface.desc();
    RoutingGraph graph{
        .receivers = std::vector<std::array<TerrainFlowReceiver, 2>>(surface.sample_count()),
        .primary = std::vector<int>(surface.sample_count(), -1),
        .direction_x = cubey::procedural::ScalarField2D(desc, 0.0F),
        .direction_z = cubey::procedural::ScalarField2D(desc, 0.0F),
    };
    for (std::uint32_t z = 0; z < desc.height; ++z) {
        for (std::uint32_t x = 0; x < desc.width; ++x) {
            if (is_outer_boundary(desc, x, z)) {
                continue;
            }
            const float gradient_x =
                (surface.at(x + 1U, z) - surface.at(x - 1U, z)) / (2.0F * desc.cell_size);
            const float gradient_z =
                (surface.at(x, z + 1U) - surface.at(x, z - 1U)) / (2.0F * desc.cell_size);
            const float downhill_x = -gradient_x;
            const float downhill_z = -gradient_z;
            if ((downhill_x * downhill_x) + (downhill_z * downhill_z) <= 0.000000000001F) {
                graph.receivers[surface.index(x, z)][0] = steepest_receiver(surface, x, z);
                graph.primary[surface.index(x, z)] = graph.receivers[surface.index(x, z)][0].index;
                set_direction_from_receivers(graph, desc, x, z);
                continue;
            }
            float angle = std::atan2(downhill_z, downhill_x);
            if (angle < 0.0F) {
                angle += kTwoPi;
            }
            assign_fractional_receivers(graph, surface, x, z, angle);
        }
    }
    return graph;
}

[[nodiscard]] std::vector<std::size_t>
descending_height_order(const cubey::procedural::ScalarField2D& surface) {
    std::vector<std::size_t> order(surface.sample_count());
    std::iota(order.begin(), order.end(), 0U);
    std::sort(order.begin(), order.end(), [&surface](std::size_t lhs, std::size_t rhs) {
        if (surface.values()[lhs] == surface.values()[rhs]) {
            return lhs < rhs;
        }
        return surface.values()[lhs] > surface.values()[rhs];
    });
    return order;
}

[[nodiscard]] cubey::procedural::ScalarField2D
accumulate_contributing_area(const cubey::procedural::ScalarField2D& surface,
                             const RoutingGraph& graph, const std::vector<std::size_t>& order) {
    const float cell_area = surface.desc().cell_size * surface.desc().cell_size;
    cubey::procedural::ScalarField2D area(surface.desc(), cell_area);
    for (const std::size_t index : order) {
        for (const TerrainFlowReceiver& receiver : graph.receivers[index]) {
            if (receiver.index >= 0 && receiver.weight > 0.0F) {
                area.values()[static_cast<std::size_t>(receiver.index)] +=
                    area.values()[index] * receiver.weight;
            }
        }
    }
    return area;
}

[[nodiscard]] cubey::procedural::ScalarField2D
compute_strahler_order(const cubey::procedural::ScalarField2D& surface, const RoutingGraph& graph,
                       const std::vector<std::size_t>& order) {
    cubey::procedural::ScalarField2D result(surface.desc(), 1.0F);
    std::vector<float> max_upstream(surface.sample_count(), 0.0F);
    std::vector<std::uint32_t> max_upstream_count(surface.sample_count(), 0U);
    for (const std::size_t index : order) {
        if (max_upstream[index] > 0.0F) {
            result.values()[index] =
                max_upstream[index] + (max_upstream_count[index] >= 2U ? 1.0F : 0.0F);
        }
        const int target = graph.primary[index];
        if (target < 0) {
            continue;
        }
        const auto target_index = static_cast<std::size_t>(target);
        const float value = result.values()[index];
        if (value > max_upstream[target_index]) {
            max_upstream[target_index] = value;
            max_upstream_count[target_index] = 1U;
        } else if (value == max_upstream[target_index]) {
            ++max_upstream_count[target_index];
        }
    }
    return result;
}

[[nodiscard]] cubey::procedural::ScalarField2D
make_discharge_proxy(const cubey::procedural::ScalarField2D& contributing_area) {
    cubey::procedural::ScalarField2D result(contributing_area.desc(), 0.0F);
    const float cell_area = contributing_area.desc().cell_size * contributing_area.desc().cell_size;
    const float max_cells = std::max(contributing_area.summarize().max / cell_area, 1.0F);
    const float denominator = std::log1p(max_cells);
    for (std::size_t index = 0; index < result.sample_count(); ++index) {
        const float contributing_cells = contributing_area.values()[index] / cell_area;
        result.values()[index] =
            denominator > 0.0F ? std::log1p(contributing_cells) / denominator : 0.0F;
    }
    return result;
}

[[nodiscard]] cubey::procedural::ScalarField2D
make_sink_mask(const cubey::procedural::ScalarField2D& surface, const RoutingGraph& graph) {
    cubey::procedural::ScalarField2D result(surface.desc(), 0.0F);
    for (std::uint32_t z = 1U; z + 1U < surface.desc().height; ++z) {
        for (std::uint32_t x = 1U; x + 1U < surface.desc().width; ++x) {
            const std::size_t index = surface.index(x, z);
            result.values()[index] = graph.primary[index] < 0 ? 1.0F : 0.0F;
        }
    }
    return result;
}

[[nodiscard]] cubey::procedural::ScalarField2D
make_flow_boundary_mask(const cubey::procedural::ScalarField2D& surface, const RoutingGraph& graph,
                        std::uint32_t core_border_samples) {
    cubey::procedural::ScalarField2D result(surface.desc(), 0.0F);
    const cubey::procedural::Grid2DDesc& desc = surface.desc();
    const std::uint32_t x_end = desc.width - core_border_samples;
    const std::uint32_t z_end = desc.height - core_border_samples;
    for (std::uint32_t z = core_border_samples; z < z_end; ++z) {
        for (std::uint32_t x = core_border_samples; x < x_end; ++x) {
            const std::size_t index = surface.index(x, z);
            for (const TerrainFlowReceiver& receiver : graph.receivers[index]) {
                if (receiver.index < 0 || receiver.weight <= 0.0F) {
                    continue;
                }
                const auto target = static_cast<std::uint32_t>(receiver.index);
                const std::uint32_t target_x = target % desc.width;
                const std::uint32_t target_z = target / desc.width;
                if (target_x < core_border_samples || target_x >= x_end ||
                    target_z < core_border_samples || target_z >= z_end) {
                    result.values()[index] = 1.0F;
                    break;
                }
            }
        }
    }
    return result;
}

[[nodiscard]] double terminal_outflow(const cubey::procedural::ScalarField2D& area,
                                      const RoutingGraph& graph) {
    double result = 0.0;
    for (std::size_t index = 0; index < graph.receivers.size(); ++index) {
        const bool has_receiver =
            std::any_of(graph.receivers[index].begin(), graph.receivers[index].end(),
                        [](const TerrainFlowReceiver& receiver) {
                            return receiver.index >= 0 && receiver.weight > 0.0F;
                        });
        if (!has_receiver) {
            result += static_cast<double>(area.values()[index]);
        }
    }
    return result;
}

} // namespace

TerrainHydrologyResult compute_regional_hydrology(const cubey::procedural::ScalarField2D& height_m,
                                                  std::uint32_t core_border_samples) {
    validate_hydrology_input(height_m, core_border_samples);
    PriorityFloodResult flood = priority_flood_open_boundary(height_m);
    RoutingGraph graph = route_fractional_descent(flood.surface);
    const std::vector<std::size_t> order = descending_height_order(flood.surface);
    cubey::procedural::ScalarField2D contributing_area =
        accumulate_contributing_area(flood.surface, graph, order);
    cubey::procedural::ScalarField2D stream_order =
        compute_strahler_order(flood.surface, graph, order);
    cubey::procedural::ScalarField2D discharge = make_discharge_proxy(contributing_area);
    cubey::procedural::ScalarField2D sink_mask = make_sink_mask(flood.surface, graph);
    cubey::procedural::ScalarField2D boundary_mask =
        make_flow_boundary_mask(flood.surface, graph, core_border_samples);
    const double cell_area = static_cast<double>(height_m.desc().cell_size) *
                             static_cast<double>(height_m.desc().cell_size);
    const double total_input = cell_area * static_cast<double>(height_m.sample_count());
    const double total_outflow = terminal_outflow(contributing_area, graph);

    return {
        .routing_surface_m = std::move(flood.surface),
        .routing_fill_delta_m = std::move(flood.fill_delta),
        .flow_direction_x = std::move(graph.direction_x),
        .flow_direction_z = std::move(graph.direction_z),
        .contributing_area_m2 = std::move(contributing_area),
        .stream_order = std::move(stream_order),
        .discharge_proxy = std::move(discharge),
        .sink_mask = std::move(sink_mask),
        .flow_boundary_mask = std::move(boundary_mask),
        .receivers = std::move(graph.receivers),
        .primary_receiver = std::move(graph.primary),
        .total_input_area_m2 = total_input,
        .terminal_outflow_area_m2 = total_outflow,
    };
}

} // namespace cubey::projects::terrain_hydrology_lab
