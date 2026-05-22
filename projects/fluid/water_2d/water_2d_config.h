#pragma once

#include <cubey/core/frame_clock.h>
#include <cubey/core/run_config.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string_view>

namespace cubey::projects::fluid::water_2d {

enum class Water2DDebugView : std::uint32_t {
    Surface = 0,
    Phi = 1,
    Velocity = 2,
    Divergence = 3,
    Pressure = 4,
    Solid = 5,
};

inline constexpr std::uint32_t kWater2DComputeGroupSize = 8;
inline constexpr std::uint32_t kWater2DSimulationPushConstantFloatCount = 16;

struct Water2DConfig {
    std::uint32_t grid_width = 512;
    std::uint32_t grid_height = 288;
    std::uint32_t pressure_iterations = 32;
    std::uint32_t reinitialization_iterations = 4;
    float fixed_delta_seconds = 1.0F / 60.0F;
    float gravity = -1.65F;
    float initial_fill_height = 0.74F;
    float initial_fill_width = 0.43F;
    std::array<float, 2> obstacle_center{0.58F, 0.38F};
    float obstacle_radius = 0.095F;
    bool obstacles_enabled = false;
};

[[nodiscard]] inline const char* water_2d_debug_view_name(Water2DDebugView view) {
    switch (view) {
    case Water2DDebugView::Surface:
        return "Surface";
    case Water2DDebugView::Phi:
        return "Phi";
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
    if (name == "phi") {
        return Water2DDebugView::Phi;
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
    throw std::runtime_error(
        "water 2D debug view must be surface, phi, velocity, divergence, pressure, or solid");
}

[[nodiscard]] inline Water2DDebugView next_debug_view(Water2DDebugView view) {
    switch (view) {
    case Water2DDebugView::Surface:
        return Water2DDebugView::Phi;
    case Water2DDebugView::Phi:
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

[[nodiscard]] inline std::size_t scalar_field_byte_size(const Water2DConfig& config) {
    return checked_mul(cell_count(config), sizeof(float), "water scalar field is too large");
}

[[nodiscard]] inline std::size_t u_face_byte_size(const Water2DConfig& config) {
    return checked_mul(u_face_count(config), sizeof(float), "water U-face field is too large");
}

[[nodiscard]] inline std::size_t v_face_byte_size(const Water2DConfig& config) {
    return checked_mul(v_face_count(config), sizeof(float), "water V-face field is too large");
}

[[nodiscard]] inline Water2DConfig water_2d_config_from_run_config(const RunConfig& config) {
    Water2DConfig water_config;
    if (config.grid_width != 0) {
        water_config.grid_width = config.grid_width;
    }
    if (config.grid_height != 0) {
        water_config.grid_height = config.grid_height;
    }
    static_cast<void>(cell_count(water_config));
    static_cast<void>(u_face_count(water_config));
    static_cast<void>(v_face_count(water_config));
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
