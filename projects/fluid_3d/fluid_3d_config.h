#pragma once

#include <cubey/core/frame_clock.h>
#include <cubey/core/run_config.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string_view>

namespace cubey::projects::fluid_3d {

enum class Fluid3DDebugView : std::uint32_t {
    Smoke = 0,
    DensitySlice = 1,
    Velocity = 2,
};

enum class Fluid3DInjectorMovement : std::uint32_t {
    Orbit = 0,
    Circle = 1,
};

inline constexpr std::uint32_t kMaxFluid3DInjectorCount = 16;

struct Fluid3DConfig {
    std::uint32_t grid_width = 128;
    std::uint32_t grid_height = 128;
    std::uint32_t grid_depth = 128;
    std::uint32_t compute_group_size = 4;
    std::uint32_t pressure_iterations = 12;
    std::uint32_t raymarch_steps = 128;
    std::uint32_t shadow_grid_width = 64;
    std::uint32_t shadow_grid_height = 64;
    std::uint32_t shadow_grid_depth = 64;
    std::uint32_t shadow_steps = 64;
    std::uint32_t shadow_update_interval = 1;
    std::uint32_t injector_count = 4;
    float fixed_delta_seconds = 1.0F / 60.0F;
    float density_decay_per_second = 0.99F;
    float velocity_decay_per_second = 0.99F;
    float injector_radius = 0.05F;
    float injector_strength = 6.0F;
    float injector_density_strength = 6.0F;
    float injector_propulsion_strength = 1.0F;
    float injector_velocity_scale = 1.3F;
    float injector_orbit_radius = 0.25F;
    float injector_orbit_radius_spread = 0.22F;
    float injector_orbit_angular_speed = 0.0F;
    float injector_orbit_angular_speed_spread = 0.8F;
    float injector_orbit_phase_spread = 1.0F;
    float injector_orbit_inclination_degrees = 0.0F;
    float injector_orbit_inclination_spread_degrees = 60.0F;
    Fluid3DInjectorMovement injector_movement = Fluid3DInjectorMovement::Orbit;
    float injector_circle_height = 0.5F;
    float vorticity_strength = 1.0F;
    float buoyancy_strength = 1.0F;
    float absorption = 8.0F;
    float emission = 2.0F;
    float shadow_absorption = 50.0F;
    float ambient_light = 0.5F;
};

[[nodiscard]] inline Fluid3DDebugView next_debug_view(Fluid3DDebugView view) {
    switch (view) {
    case Fluid3DDebugView::Smoke:
        return Fluid3DDebugView::DensitySlice;
    case Fluid3DDebugView::DensitySlice:
        return Fluid3DDebugView::Velocity;
    case Fluid3DDebugView::Velocity:
        return Fluid3DDebugView::Smoke;
    }
    return Fluid3DDebugView::Smoke;
}

[[nodiscard]] inline const char* fluid_3d_injector_movement_name(
    Fluid3DInjectorMovement movement) {
    switch (movement) {
    case Fluid3DInjectorMovement::Orbit:
        return "Orbit";
    case Fluid3DInjectorMovement::Circle:
        return "Circle";
    }
    return "Orbit";
}

[[nodiscard]] inline Fluid3DInjectorMovement fluid_3d_injector_movement_from_string(
    std::string_view name) {
    if (name == "orbit") {
        return Fluid3DInjectorMovement::Orbit;
    }
    if (name == "circle") {
        return Fluid3DInjectorMovement::Circle;
    }
    throw std::runtime_error("fluid 3D injector movement must be orbit or circle");
}

[[nodiscard]] inline std::size_t volume_cell_count(const Fluid3DConfig& config) {
    if (config.grid_width == 0 || config.grid_height == 0 || config.grid_depth == 0) {
        throw std::runtime_error("fluid 3D grid dimensions must be positive");
    }
    const std::size_t width = static_cast<std::size_t>(config.grid_width);
    const std::size_t height = static_cast<std::size_t>(config.grid_height);
    const std::size_t depth = static_cast<std::size_t>(config.grid_depth);
    if (width > std::numeric_limits<std::size_t>::max() / height) {
        throw std::runtime_error("fluid 3D grid dimensions are too large");
    }
    const std::size_t slice = width * height;
    if (slice > std::numeric_limits<std::size_t>::max() / depth) {
        throw std::runtime_error("fluid 3D grid dimensions are too large");
    }
    return slice * depth;
}

[[nodiscard]] inline std::size_t volume_byte_size(const Fluid3DConfig& config,
                                                  std::size_t bytes_per_cell) {
    if (bytes_per_cell == 0) {
        throw std::runtime_error("fluid 3D volume byte size requires a positive cell size");
    }
    const std::size_t cells = volume_cell_count(config);
    if (cells > std::numeric_limits<std::size_t>::max() / bytes_per_cell) {
        throw std::runtime_error("fluid 3D volume is too large");
    }
    return cells * bytes_per_cell;
}

[[nodiscard]] inline std::size_t shadow_volume_cell_count(const Fluid3DConfig& config) {
    if (config.shadow_grid_width == 0 || config.shadow_grid_height == 0 ||
        config.shadow_grid_depth == 0) {
        throw std::runtime_error("fluid 3D shadow grid dimensions must be positive");
    }
    const std::size_t width = static_cast<std::size_t>(config.shadow_grid_width);
    const std::size_t height = static_cast<std::size_t>(config.shadow_grid_height);
    const std::size_t depth = static_cast<std::size_t>(config.shadow_grid_depth);
    if (width > std::numeric_limits<std::size_t>::max() / height) {
        throw std::runtime_error("fluid 3D shadow grid dimensions are too large");
    }
    const std::size_t slice = width * height;
    if (slice > std::numeric_limits<std::size_t>::max() / depth) {
        throw std::runtime_error("fluid 3D shadow grid dimensions are too large");
    }
    return slice * depth;
}

[[nodiscard]] inline Fluid3DConfig fluid_3d_config_from_run_config(const RunConfig& config) {
    Fluid3DConfig result;
    if (config.grid_width != 0) {
        result.grid_width = config.grid_width;
    }
    if (config.grid_height != 0) {
        result.grid_height = config.grid_height;
    }
    if (config.grid_depth != 0) {
        result.grid_depth = config.grid_depth;
    }
    if (config.shadow_grid_width != 0) {
        result.shadow_grid_width = config.shadow_grid_width;
    }
    if (config.shadow_grid_height != 0) {
        result.shadow_grid_height = config.shadow_grid_height;
    }
    if (config.shadow_grid_depth != 0) {
        result.shadow_grid_depth = config.shadow_grid_depth;
    }
    if (config.shadow_steps != 0) {
        result.shadow_steps = config.shadow_steps;
    }
    if (config.shadow_update_interval != 0) {
        result.shadow_update_interval = config.shadow_update_interval;
    }
    if (config.injectors != 0) {
        if (config.injectors > kMaxFluid3DInjectorCount) {
            throw std::runtime_error("fluid 3D injector count must be 1..16");
        }
        result.injector_count = config.injectors;
    }
    result.injector_strength = config.injector_force;
    result.injector_density_strength = config.fluid_density_injection;
    result.injector_propulsion_strength = config.injector_propulsion;
    result.injector_orbit_radius = config.injector_orbit_radius;
    result.injector_orbit_radius_spread = config.injector_orbit_radius_spread;
    result.injector_orbit_angular_speed = config.injector_orbit_angular_speed;
    result.injector_orbit_angular_speed_spread = config.injector_orbit_angular_speed_spread;
    result.injector_orbit_phase_spread = config.injector_orbit_phase_spread;
    result.injector_orbit_inclination_degrees = config.injector_orbit_inclination_degrees;
    result.injector_orbit_inclination_spread_degrees =
        config.injector_orbit_inclination_spread_degrees;
    result.injector_movement = fluid_3d_injector_movement_from_string(config.injector_movement);
    result.injector_circle_height = config.injector_circle_height;
    result.buoyancy_strength = config.fluid_buoyancy;
    static_cast<void>(volume_cell_count(result));
    static_cast<void>(shadow_volume_cell_count(result));
    return result;
}

[[nodiscard]] inline std::uint32_t fluid_3d_headless_frame_count(const RunConfig& config) {
    return config.frames == 0 ? 120U : config.frames;
}

[[nodiscard]] inline FrameTiming fixed_fluid_3d_headless_timing(const Fluid3DConfig& config,
                                                                std::uint64_t frame_index) {
    if (frame_index == 0) {
        throw std::runtime_error("fixed fluid 3D frame index must be positive");
    }
    return {
        .delta_seconds = config.fixed_delta_seconds,
        .elapsed_seconds =
            static_cast<double>(config.fixed_delta_seconds) * static_cast<double>(frame_index),
        .frame_index = frame_index,
    };
}

} // namespace cubey::projects::fluid_3d
