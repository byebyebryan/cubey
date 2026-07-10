#include "terrain_landscape_evolution.h"

#include <cubey/procedural/field_2d.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

namespace cubey::projects::terrain {
namespace {

inline constexpr std::array<int, 4> kOffsetX{1, 0, -1, 0};
inline constexpr std::array<int, 4> kOffsetY{0, 1, 0, -1};

struct AnalyticalPass {
    cubey::procedural::ScalarField2D height_m{};
    cubey::procedural::ScalarField2D fluvial_rate_m_per_year{};
    cubey::procedural::ScalarField2D hillslope_rate_m_per_year{};
    cubey::procedural::ScalarField2D thermal_active_mask{};
};

void validate_inputs(const cubey::procedural::ScalarField2D& initial_height_m,
                     const cubey::procedural::ScalarField2D& uplift_rate_m_per_year,
                     const TerrainLandscapeEvolutionConfig& config) {
    if (!cubey::procedural::same_grid_desc(initial_height_m.desc(),
                                           uplift_rate_m_per_year.desc())) {
        throw std::runtime_error("landscape evolution inputs must share a grid");
    }
    if (initial_height_m.desc().width < 3U || initial_height_m.desc().height < 3U) {
        throw std::runtime_error("landscape evolution requires dimensions of at least 3");
    }
    if (config.multigrid_levels == 0U || config.iterations_per_level == 0U ||
        config.multigrid_levels > 16U) {
        throw std::runtime_error("landscape evolution requires bounded positive iterations");
    }
    if (!std::isfinite(config.age_years) || config.age_years < 0.0 ||
        !std::isfinite(config.stream_power_coefficient) ||
        config.stream_power_coefficient <= 0.0F ||
        !std::isfinite(config.stream_power_area_exponent) ||
        config.stream_power_area_exponent < 0.0F || !std::isfinite(config.hillslope_coefficient) ||
        config.hillslope_coefficient < 0.0F || !std::isfinite(config.hack_constant) ||
        config.hack_constant <= 0.0F || !std::isfinite(config.hack_exponent) ||
        config.hack_exponent <= 0.0F || !std::isfinite(config.thermal_coefficient) ||
        config.thermal_coefficient < 0.0F || !std::isfinite(config.critical_slope) ||
        config.critical_slope < 0.0F || !std::isfinite(config.relaxation) ||
        config.relaxation <= 0.0F || config.relaxation > 1.0F ||
        !std::isfinite(config.upsample_jitter_cells) || config.upsample_jitter_cells < 0.0F ||
        config.upsample_jitter_cells > 0.5F ||
        !std::isfinite(config.altitude_correction_learning_rate) ||
        config.altitude_correction_learning_rate < 0.0F) {
        throw std::runtime_error("landscape evolution parameters are invalid");
    }
    const auto finite = [](float value) { return std::isfinite(value); };
    if (!std::all_of(initial_height_m.values().begin(), initial_height_m.values().end(), finite) ||
        !std::all_of(uplift_rate_m_per_year.values().begin(), uplift_rate_m_per_year.values().end(),
                     finite) ||
        std::any_of(uplift_rate_m_per_year.values().begin(), uplift_rate_m_per_year.values().end(),
                    [](float value) { return value < 0.0F; })) {
        throw std::runtime_error("landscape evolution fields must be finite and nonnegative");
    }
}

[[nodiscard]] std::vector<std::vector<int>> build_jump_table(const TerrainLandscapeGraph& graph) {
    std::size_t levels = 1U;
    while ((std::size_t{1U} << levels) < graph.receiver.size()) {
        ++levels;
    }
    std::vector<std::vector<int>> jump(levels, graph.receiver);
    for (std::size_t level = 1U; level < levels; ++level) {
        for (std::size_t index = 0U; index < graph.receiver.size(); ++index) {
            const int parent = jump[level - 1U][index];
            jump[level][index] =
                parent >= 0 ? jump[level - 1U][static_cast<std::size_t>(parent)] : -1;
        }
    }
    return jump;
}

[[nodiscard]] double interpolate_tree_value(std::size_t index, double target_response,
                                            const std::vector<double>& response,
                                            const std::vector<double>& values,
                                            const TerrainLandscapeGraph& graph,
                                            const std::vector<std::vector<int>>& jump) {
    if (target_response <= 0.0 || graph.receiver[index] < 0) {
        std::size_t root = index;
        for (std::size_t level = jump.size(); level-- > 0U;) {
            const int ancestor = jump[level][root];
            if (ancestor >= 0) {
                root = static_cast<std::size_t>(ancestor);
            }
        }
        return values[root];
    }

    std::size_t upper = index;
    for (std::size_t level = jump.size(); level-- > 0U;) {
        const int ancestor = jump[level][upper];
        if (ancestor >= 0 && response[static_cast<std::size_t>(ancestor)] > target_response) {
            upper = static_cast<std::size_t>(ancestor);
        }
    }
    const int lower_index = graph.receiver[upper];
    if (lower_index < 0) {
        return values[upper];
    }
    const std::size_t lower = static_cast<std::size_t>(lower_index);
    const double span = response[upper] - response[lower];
    if (span <= std::numeric_limits<double>::epsilon()) {
        return values[lower];
    }
    const double t = std::clamp((target_response - response[lower]) / span, 0.0, 1.0);
    return std::lerp(values[lower], values[upper], t);
}

[[nodiscard]] AnalyticalPass
solve_fixed_graph(const cubey::procedural::ScalarField2D& initial_height_m,
                  const cubey::procedural::ScalarField2D& uplift_rate_m_per_year,
                  const TerrainLandscapeGraph& graph,
                  const TerrainLandscapeEvolutionConfig& config) {
    const cubey::procedural::Grid2DDesc& desc = initial_height_m.desc();
    const std::size_t count = initial_height_m.sample_count();
    AnalyticalPass result{
        .height_m = cubey::procedural::ScalarField2D(desc, 0.0F),
        .fluvial_rate_m_per_year = cubey::procedural::ScalarField2D(desc, 0.0F),
        .hillslope_rate_m_per_year = cubey::procedural::ScalarField2D(desc, 0.0F),
        .thermal_active_mask = cubey::procedural::ScalarField2D(desc, 0.0F),
    };
    std::vector<double> response_time(count, 0.0);
    std::vector<double> uplift_integral(count, 0.0);
    std::vector<double> initial(initial_height_m.values().begin(), initial_height_m.values().end());
    const std::vector<std::vector<int>> jump = build_jump_table(graph);

    for (const std::size_t index : graph.downstream_to_upstream) {
        const int receiver = graph.receiver[index];
        if (receiver >= 0) {
            initial[index] = std::max(initial[index], initial[static_cast<std::size_t>(receiver)]);
        }
    }

    for (const std::size_t index : graph.downstream_to_upstream) {
        const int receiver_index = graph.receiver[index];
        if (receiver_index < 0) {
            result.height_m.values()[index] = initial_height_m.values()[index];
            continue;
        }
        const std::size_t receiver = static_cast<std::size_t>(receiver_index);
        const double drainage_area =
            std::max(static_cast<double>(graph.drainage_area_m2.values()[index]), 1.0);
        const double fluvial =
            static_cast<double>(config.stream_power_coefficient) *
            std::pow(drainage_area, static_cast<double>(config.stream_power_area_exponent)) *
            static_cast<double>(graph.slope_correction.values()[index]);
        const double hillslope =
            config.hillslope_coefficient > 0.0F
                ? (static_cast<double>(config.hillslope_coefficient) /
                   static_cast<double>(config.hack_constant)) *
                      std::pow(drainage_area, -static_cast<double>(config.hack_exponent))
                : 0.0;
        result.fluvial_rate_m_per_year.values()[index] = static_cast<float>(fluvial);
        result.hillslope_rate_m_per_year.values()[index] = static_cast<float>(hillslope);
        const auto solve_node = [&](double speed, double effective_uplift) {
            const double travel_time =
                static_cast<double>(desc.cell_size) / std::max(speed, 1.0e-12);
            response_time[index] = response_time[receiver] + travel_time;
            uplift_integral[index] = uplift_integral[receiver] + travel_time * effective_uplift;

            const double target_response = response_time[index] - config.age_years;
            const double source_height =
                interpolate_tree_value(index, target_response, response_time, initial, graph, jump);
            const double source_uplift = interpolate_tree_value(
                index, target_response, response_time, uplift_integral, graph, jump);
            const double solved = source_height + uplift_integral[index] - source_uplift;
            result.height_m.values()[index] = static_cast<float>(
                std::max(solved, static_cast<double>(result.height_m.values()[receiver])));
        };

        const double base_speed = fluvial + hillslope;
        const double uplift = uplift_rate_m_per_year.values()[index];
        solve_node(base_speed, uplift);
        const double solved_slope = static_cast<double>(result.height_m.values()[index] -
                                                        result.height_m.values()[receiver]) /
                                    static_cast<double>(desc.cell_size);
        if (solved_slope >= static_cast<double>(config.critical_slope) &&
            config.thermal_coefficient > 0.0F) {
            result.thermal_active_mask.values()[index] = 1.0F;
            solve_node(base_speed + static_cast<double>(config.thermal_coefficient),
                       uplift + static_cast<double>(config.thermal_coefficient) *
                                    static_cast<double>(config.critical_slope));
        }
    }
    return result;
}

[[nodiscard]] cubey::procedural::ScalarField2D
relax_field(const cubey::procedural::ScalarField2D& current,
            const cubey::procedural::ScalarField2D& target, float amount) {
    cubey::procedural::ScalarField2D result = current;
    for (std::size_t index = 0U; index < result.sample_count(); ++index) {
        result.values()[index] = std::lerp(current.values()[index], target.values()[index], amount);
    }
    return result;
}

[[nodiscard]] cubey::procedural::ScalarField2D
correct_altitude_discontinuities(const cubey::procedural::ScalarField2D& height_m,
                                 const TerrainLandscapeGraph& graph, std::uint32_t iterations,
                                 float learning_rate) {
    if (iterations == 0U || learning_rate == 0.0F) {
        return height_m;
    }
    const cubey::procedural::Grid2DDesc& desc = height_m.desc();
    const std::size_t count = height_m.sample_count();
    const auto [minimum, maximum] =
        std::minmax_element(height_m.values().begin(), height_m.values().end());
    const double span = std::max(static_cast<double>(*maximum - *minimum), 1.0);
    std::vector<double> target_drop(count, 0.0);
    std::vector<double> drop(count, 0.0);
    std::vector<double> normalized_height(count, 0.0);
    std::vector<double> local_gradient(count, 0.0);
    std::vector<double> accumulated_gradient(count, 0.0);

    for (std::size_t index = 0U; index < count; ++index) {
        const int receiver = graph.receiver[index];
        if (receiver >= 0) {
            target_drop[index] = std::max(
                static_cast<double>(height_m.values()[index] -
                                    height_m.values()[static_cast<std::size_t>(receiver)]) /
                    span,
                0.0);
            drop[index] = target_drop[index];
        }
    }

    const auto reconstruct = [&] {
        for (const std::size_t index : graph.downstream_to_upstream) {
            const int receiver = graph.receiver[index];
            normalized_height[index] =
                receiver < 0 ? static_cast<double>(height_m.values()[index] - *minimum) / span
                             : normalized_height[static_cast<std::size_t>(receiver)] + drop[index];
        }
    };

    for (std::uint32_t iteration = 0U; iteration < iterations; ++iteration) {
        reconstruct();
        std::fill(local_gradient.begin(), local_gradient.end(), 0.0);
        for (std::uint32_t y = 1U; y + 1U < desc.height; ++y) {
            for (std::uint32_t x = 1U; x + 1U < desc.width; ++x) {
                const std::size_t index = height_m.index(x, y);
                for (std::size_t direction = 0U; direction < kOffsetX.size(); ++direction) {
                    const auto nx =
                        static_cast<std::uint32_t>(static_cast<int>(x) + kOffsetX[direction]);
                    const auto ny =
                        static_cast<std::uint32_t>(static_cast<int>(y) + kOffsetY[direction]);
                    const std::size_t neighbor = height_m.index(nx, ny);
                    if (graph.receiver[index] == static_cast<int>(neighbor) ||
                        graph.receiver[neighbor] == static_cast<int>(index)) {
                        continue;
                    }
                    local_gradient[index] +=
                        std::max(normalized_height[index] - normalized_height[neighbor] -
                                     target_drop[index],
                                 0.0) -
                        std::max(normalized_height[neighbor] - normalized_height[index] -
                                     target_drop[neighbor],
                                 0.0);
                }
            }
        }
        accumulated_gradient = local_gradient;
        for (const std::size_t index : graph.upstream_to_downstream) {
            const int receiver = graph.receiver[index];
            if (receiver >= 0) {
                accumulated_gradient[static_cast<std::size_t>(receiver)] +=
                    accumulated_gradient[index];
            }
        }
        const double cell_area = static_cast<double>(desc.cell_size) * desc.cell_size;
        for (std::size_t index = 0U; index < count; ++index) {
            if (graph.receiver[index] < 0) {
                continue;
            }
            const double upstream_count = std::max(
                static_cast<double>(graph.drainage_area_m2.values()[index]) / cell_area, 1.0);
            const double gradient = ((drop[index] - target_drop[index]) / 3.0) +
                                    ((2.0 / 3.0) * accumulated_gradient[index] / upstream_count);
            drop[index] =
                std::max(drop[index] - static_cast<double>(learning_rate) * gradient, 0.0);
        }
    }
    reconstruct();
    cubey::procedural::ScalarField2D result(desc, 0.0F);
    for (std::size_t index = 0U; index < count; ++index) {
        result.values()[index] = static_cast<float>(normalized_height[index] * span + *minimum);
    }
    return result;
}

} // namespace

TerrainLandscapeEvolutionResult
evolve_terrain_landscape(const cubey::procedural::ScalarField2D& initial_height_m,
                         const cubey::procedural::ScalarField2D& uplift_rate_m_per_year,
                         const TerrainLandscapeEvolutionConfig& config) {
    validate_inputs(initial_height_m, uplift_rate_m_per_year, config);

    std::vector<cubey::procedural::ScalarField2D> height_levels{initial_height_m};
    std::vector<cubey::procedural::ScalarField2D> uplift_levels{uplift_rate_m_per_year};
    for (std::uint32_t level = 1U; level < config.multigrid_levels; ++level) {
        if (height_levels.back().desc().width <= 3U || height_levels.back().desc().height <= 3U) {
            break;
        }
        height_levels.push_back(downsample_terrain_landscape_field(height_levels.back()));
        uplift_levels.push_back(downsample_terrain_landscape_field(uplift_levels.back()));
    }

    const std::size_t coarsest = height_levels.size() - 1U;
    cubey::procedural::ScalarField2D current = height_levels[coarsest];
    for (std::size_t reverse_level = height_levels.size(); reverse_level-- > 0U;) {
        if (reverse_level != coarsest) {
            current = upsample_terrain_landscape_field(current, config.seed,
                                                       static_cast<std::uint32_t>(reverse_level),
                                                       config.upsample_jitter_cells);
        }
        for (std::uint32_t iteration = 0U; iteration < config.iterations_per_level; ++iteration) {
            const TerrainLandscapeGraph graph = build_terrain_landscape_graph(
                current, config.seed, static_cast<std::uint32_t>(reverse_level));
            const AnalyticalPass pass = solve_fixed_graph(
                height_levels[reverse_level], uplift_levels[reverse_level], graph, config);
            current = relax_field(current, pass.height_m, config.relaxation);
        }
    }

    const cubey::procedural::ScalarField2D analytical_height_m = current;
    TerrainLandscapeGraph final_graph = build_terrain_landscape_graph(current, config.seed, 0U);
    AnalyticalPass diagnostics =
        solve_fixed_graph(initial_height_m, uplift_rate_m_per_year, final_graph, config);
    cubey::procedural::ScalarField2D corrected = correct_altitude_discontinuities(
        current, final_graph, config.altitude_correction_iterations,
        config.altitude_correction_learning_rate);
    cubey::procedural::ScalarField2D correction_delta(corrected.desc(), 0.0F);
    cubey::procedural::ScalarField2D process_delta(corrected.desc(), 0.0F);
    for (std::size_t index = 0U; index < corrected.sample_count(); ++index) {
        correction_delta.values()[index] =
            corrected.values()[index] - analytical_height_m.values()[index];
        process_delta.values()[index] =
            corrected.values()[index] - initial_height_m.values()[index];
    }
    const float discontinuity =
        terrain_landscape_basin_discontinuity_coverage(corrected, final_graph, 100.0F);

    return {
        .uplift_rate_m_per_year = uplift_rate_m_per_year,
        .process_drainage_area_m2 = std::move(final_graph.drainage_area_m2),
        .process_flow_direction_x = std::move(final_graph.flow_direction_x),
        .process_flow_direction_z = std::move(final_graph.flow_direction_z),
        .process_breach_mask = std::move(final_graph.breach_mask),
        .fluvial_advection_rate_m_per_year = std::move(diagnostics.fluvial_rate_m_per_year),
        .hillslope_advection_rate_m_per_year = std::move(diagnostics.hillslope_rate_m_per_year),
        .thermal_active_mask = std::move(diagnostics.thermal_active_mask),
        .analytical_height_m = analytical_height_m,
        .altitude_correction_delta_m = std::move(correction_delta),
        .process_delta_m = std::move(process_delta),
        .height_m = std::move(corrected),
        .unresolved_sink_count = final_graph.unresolved_sink_count,
        .basin_discontinuity_coverage = discontinuity,
    };
}

} // namespace cubey::projects::terrain
