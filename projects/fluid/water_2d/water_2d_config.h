#pragma once

#include <cubey/core/frame_clock.h>
#include <cubey/core/run_config.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string_view>

namespace cubey::projects::fluid::water_2d {

enum class Water2DDebugView : std::uint32_t {
    Surface = 0,
    Particles = 1,
    Cells = 2,
    Velocity = 3,
    Divergence = 4,
    Pressure = 5,
    Solid = 6,
};

inline constexpr std::uint32_t kWater2DComputeGroupSize = 8;
inline constexpr std::uint32_t kWater2DSimulationPushConstantFloatCount = 20;
inline constexpr std::uint32_t kWater2DRenderPushConstantFloatCount = 8;
inline constexpr std::uint32_t kWater2DDefaultGridWidth = 256;
inline constexpr std::uint32_t kWater2DDefaultGridHeight = 144;
inline constexpr float kWater2DMinFillFraction = 0.08F;
inline constexpr float kWater2DMaxFillFraction = 0.92F;

struct Water2DConfig {
    std::uint32_t grid_width = kWater2DDefaultGridWidth;
    std::uint32_t grid_height = kWater2DDefaultGridHeight;
    std::uint32_t pressure_iterations = 256;
    std::uint32_t particles_per_cell = 4;
    std::uint32_t max_particles_per_cell = 16;
    std::uint32_t active_particle_count = 51200;
    std::uint32_t particle_capacity = 124080;
    float fixed_delta_seconds = 1.0F / 60.0F;
    float gravity = -1.60F;
    float flip_ratio = 0.78F;
    float particle_radius = 0.0125F;
    float initial_fill_height = 0.70F;
    float initial_fill_width = 0.50F;
    std::array<float, 2> obstacle_center{0.58F, 0.38F};
    float obstacle_radius = 0.095F;
    bool obstacles_enabled = false;
};

[[nodiscard]] inline const char* water_2d_debug_view_name(Water2DDebugView view) {
    switch (view) {
    case Water2DDebugView::Surface:
        return "Surface";
    case Water2DDebugView::Particles:
        return "Particles";
    case Water2DDebugView::Cells:
        return "Cells";
    case Water2DDebugView::Velocity:
        return "Velocity";
    case Water2DDebugView::Divergence:
        return "Divergence";
    case Water2DDebugView::Pressure:
        return "Pressure";
    case Water2DDebugView::Solid:
        return "Solid";
    }
    return "Surface";
}

[[nodiscard]] inline Water2DDebugView water_2d_debug_view_from_name(std::string_view name) {
    if (name.empty() || name == "surface") {
        return Water2DDebugView::Surface;
    }
    if (name == "particles") {
        return Water2DDebugView::Particles;
    }
    if (name == "cells") {
        return Water2DDebugView::Cells;
    }
    if (name == "velocity") {
        return Water2DDebugView::Velocity;
    }
    if (name == "divergence") {
        return Water2DDebugView::Divergence;
    }
    if (name == "pressure") {
        return Water2DDebugView::Pressure;
    }
    if (name == "solid") {
        return Water2DDebugView::Solid;
    }
    throw std::runtime_error("water 2D debug view must be surface, particles, cells, velocity, "
                             "divergence, pressure, or solid");
}

[[nodiscard]] inline Water2DDebugView next_debug_view(Water2DDebugView view) {
    switch (view) {
    case Water2DDebugView::Surface:
        return Water2DDebugView::Particles;
    case Water2DDebugView::Particles:
        return Water2DDebugView::Cells;
    case Water2DDebugView::Cells:
        return Water2DDebugView::Velocity;
    case Water2DDebugView::Velocity:
        return Water2DDebugView::Divergence;
    case Water2DDebugView::Divergence:
        return Water2DDebugView::Pressure;
    case Water2DDebugView::Pressure:
        return Water2DDebugView::Solid;
    case Water2DDebugView::Solid:
        return Water2DDebugView::Surface;
    }
    return Water2DDebugView::Surface;
}

[[nodiscard]] inline std::size_t checked_mul(std::size_t lhs, std::size_t rhs,
                                             const char* message) {
    if (lhs != 0 && rhs > std::numeric_limits<std::size_t>::max() / lhs) {
        throw std::runtime_error(message);
    }
    return lhs * rhs;
}

[[nodiscard]] inline std::size_t cell_count(const Water2DConfig& config) {
    if (config.grid_width == 0 || config.grid_height == 0) {
        throw std::runtime_error("water grid dimensions must be positive");
    }
    return checked_mul(static_cast<std::size_t>(config.grid_width),
                       static_cast<std::size_t>(config.grid_height),
                       "water grid dimensions are too large");
}

[[nodiscard]] inline std::size_t u_face_count(const Water2DConfig& config) {
    if (config.grid_width == std::numeric_limits<std::uint32_t>::max()) {
        throw std::runtime_error("water grid width is too large");
    }
    return checked_mul(static_cast<std::size_t>(config.grid_width) + 1U,
                       static_cast<std::size_t>(config.grid_height),
                       "water U-face grid is too large");
}

[[nodiscard]] inline std::size_t v_face_count(const Water2DConfig& config) {
    if (config.grid_height == std::numeric_limits<std::uint32_t>::max()) {
        throw std::runtime_error("water grid height is too large");
    }
    return checked_mul(static_cast<std::size_t>(config.grid_width),
                       static_cast<std::size_t>(config.grid_height) + 1U,
                       "water V-face grid is too large");
}

[[nodiscard]] inline std::size_t fill_cell_count(const Water2DConfig& config, float fill_width,
                                                 float fill_height) {
    const float clamped_width =
        std::clamp(fill_width, kWater2DMinFillFraction, kWater2DMaxFillFraction);
    const float clamped_height =
        std::clamp(fill_height, kWater2DMinFillFraction, kWater2DMaxFillFraction);
    const auto fill_cols = static_cast<std::size_t>(
        std::max(1.0F, std::floor(static_cast<float>(config.grid_width) * clamped_width)));
    const auto fill_rows = static_cast<std::size_t>(
        std::max(1.0F, std::floor(static_cast<float>(config.grid_height) * clamped_height)));
    return checked_mul(fill_cols, fill_rows, "water initial fill area is too large");
}

[[nodiscard]] inline std::uint32_t particle_count_for_fill(const Water2DConfig& config,
                                                           float fill_width, float fill_height) {
    if (config.particles_per_cell == 0) {
        throw std::runtime_error("water particles-per-cell must be positive");
    }
    const std::size_t count = checked_mul(fill_cell_count(config, fill_width, fill_height),
                                          static_cast<std::size_t>(config.particles_per_cell),
                                          "water particle count is too large");
    if (count > std::numeric_limits<std::uint32_t>::max()) {
        throw std::runtime_error("water particle count exceeds shader index range");
    }
    return static_cast<std::uint32_t>(count);
}

[[nodiscard]] inline std::uint32_t active_particle_count_for_fill(const Water2DConfig& config) {
    return particle_count_for_fill(config, config.initial_fill_width, config.initial_fill_height);
}

[[nodiscard]] inline std::uint32_t particle_capacity_for_config(const Water2DConfig& config) {
    return particle_count_for_fill(config, kWater2DMaxFillFraction, kWater2DMaxFillFraction);
}

inline void refresh_particle_counts(Water2DConfig& config) {
    config.active_particle_count = active_particle_count_for_fill(config);
    config.particle_capacity = particle_capacity_for_config(config);
    if (config.active_particle_count > config.particle_capacity) {
        throw std::runtime_error("water active particle count exceeds particle capacity");
    }
}

[[nodiscard]] inline std::size_t scalar_field_byte_size(const Water2DConfig& config) {
    return checked_mul(cell_count(config), sizeof(float), "water scalar field is too large");
}

[[nodiscard]] inline std::size_t cell_uint_field_byte_size(const Water2DConfig& config) {
    return checked_mul(cell_count(config), sizeof(std::uint32_t),
                       "water uint cell field is too large");
}

[[nodiscard]] inline std::size_t u_face_byte_size(const Water2DConfig& config) {
    return checked_mul(u_face_count(config), sizeof(float), "water U-face field is too large");
}

[[nodiscard]] inline std::size_t v_face_byte_size(const Water2DConfig& config) {
    return checked_mul(v_face_count(config), sizeof(float), "water V-face field is too large");
}

[[nodiscard]] inline std::size_t particle_value_count(const Water2DConfig& config) {
    return checked_mul(static_cast<std::size_t>(config.particle_capacity), std::size_t{4},
                       "water particle vector field is too large");
}

[[nodiscard]] inline std::size_t particle_buffer_byte_size(const Water2DConfig& config) {
    return checked_mul(particle_value_count(config), sizeof(float),
                       "water particle buffer is too large");
}

[[nodiscard]] inline std::size_t particle_bin_index_count(const Water2DConfig& config) {
    if (config.max_particles_per_cell == 0) {
        throw std::runtime_error("water max particles per cell must be positive");
    }
    return checked_mul(cell_count(config), static_cast<std::size_t>(config.max_particles_per_cell),
                       "water particle bins are too large");
}

[[nodiscard]] inline std::size_t particle_bin_index_byte_size(const Water2DConfig& config) {
    return checked_mul(particle_bin_index_count(config), sizeof(std::uint32_t),
                       "water particle bin index buffer is too large");
}

[[nodiscard]] inline Water2DConfig water_2d_config_from_run_config(const RunConfig& config) {
    Water2DConfig water_config;
    if (config.grid_width != 0) {
        water_config.grid_width = config.grid_width;
    }
    if (config.grid_height != 0) {
        water_config.grid_height = config.grid_height;
    }
    refresh_particle_counts(water_config);
    static_cast<void>(cell_count(water_config));
    static_cast<void>(u_face_count(water_config));
    static_cast<void>(v_face_count(water_config));
    static_cast<void>(particle_bin_index_count(water_config));
    return water_config;
}

[[nodiscard]] inline std::uint32_t water_2d_headless_frame_count(const RunConfig& config) {
    if (config.frames == 0) {
        return 120;
    }
    return config.frames;
}

[[nodiscard]] inline FrameTiming fixed_water_2d_headless_timing(const Water2DConfig& config,
                                                                std::uint64_t frame_index) {
    if (frame_index == 0) {
        throw std::runtime_error("fixed water headless frame index must be positive");
    }
    return {
        .delta_seconds = config.fixed_delta_seconds,
        .elapsed_seconds =
            static_cast<double>(config.fixed_delta_seconds) * static_cast<double>(frame_index),
        .frame_index = frame_index,
    };
}

} // namespace cubey::projects::fluid::water_2d
