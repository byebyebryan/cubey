#pragma once

#include <cubey/core/frame_clock.h>
#include <cubey/core/run_config.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace cubey::projects::fluid::pyro_3d {

enum class Pyro3DDebugView : std::uint32_t {
    Smoke = 0,
    DensitySlice = 1,
    Velocity = 2,
};

enum class Pyro3DMode : std::uint32_t {
    Fire = 0,
    Explosion = 1,
};

inline constexpr std::uint32_t kMaxPyro3DSourceCount = 16;
inline constexpr std::uint32_t kPyro3DComputeGroupSize = 4;
inline constexpr std::uint32_t kPyro3DSimulationPushConstantFloatCount = 28;
inline constexpr std::uint32_t kPyro3DRenderPushConstantFloatCount = 24;
inline constexpr float kDefaultPyro3DSourceRadius = 0.05F;
inline constexpr float kDefaultFireSourceRadius = 0.085F;
inline constexpr std::uint32_t kDefaultExplosion3DSourceCount = 9;
inline constexpr float kDefaultExplosion3DSourceRadius = 0.060F;
inline constexpr float kDefaultPyro3DObstacleHeight = 0.48F;
inline constexpr float kDefaultPyro3DObstacleRadius = 0.15F;
inline constexpr float kMaxPyro3DObstacleRadius = 0.5F;

struct Pyro3DConfig {
    std::uint32_t grid_width = 128;
    std::uint32_t grid_height = 128;
    std::uint32_t grid_depth = 128;
    std::uint32_t pressure_iterations = 12;
    std::uint32_t raymarch_steps = 128;
    std::uint32_t shadow_grid_width = 64;
    std::uint32_t shadow_grid_height = 64;
    std::uint32_t shadow_grid_depth = 64;
    std::uint32_t shadow_steps = 64;
    std::uint32_t shadow_update_interval = 1;
    std::uint32_t source_count = 1;
    float fixed_delta_seconds = 1.0F / 60.0F;
    float density_decay_per_second = 0.99F;
    float velocity_decay_per_second = 0.99F;
    Pyro3DMode mode = Pyro3DMode::Fire;
    float source_radius = kDefaultFireSourceRadius;
    float source_velocity_strength = 6.0F;
    float source_smoke_amount = 6.0F;
    float source_heat_amount = 1.4F;
    float source_flame_amount = 2.0F;
    float explosion_interval_seconds = 3.0F;
    float explosion_duration_seconds = 0.12F;
    float explosion_boost = 18.0F;
    float fire_ignition_temperature = 0.22F;
    float fire_burn_rate = 2.2F;
    float fire_heat_output = 1.65F;
    float fire_soot_yield = 0.070F;
    float fire_expansion = 0.65F;
    float fire_flame_cooling = 5.5F;
    float fire_shredding = 1.6F;
    float fire_turbulence = 0.35F;
    float obstacle_center_height = kDefaultPyro3DObstacleHeight;
    float obstacle_radius = kDefaultPyro3DObstacleRadius;
    float vorticity_strength = 1.0F;
    float buoyancy_strength = 1.0F;
    float absorption = 8.0F;
    float emission = 2.0F;
    float shadow_absorption = 50.0F;
    float ambient_light = 0.5F;
};

[[nodiscard]] inline Pyro3DDebugView next_debug_view(Pyro3DDebugView view) {
    switch (view) {
    case Pyro3DDebugView::Smoke:
        return Pyro3DDebugView::DensitySlice;
    case Pyro3DDebugView::DensitySlice:
        return Pyro3DDebugView::Velocity;
    case Pyro3DDebugView::Velocity:
        return Pyro3DDebugView::Smoke;
    }
    return Pyro3DDebugView::Smoke;
}

[[nodiscard]] inline const char* pyro_3d_mode_name(Pyro3DMode mode) {
    switch (mode) {
    case Pyro3DMode::Fire:
        return "Fire";
    case Pyro3DMode::Explosion:
        return "Explosion";
    }
    return "Fire";
}

[[nodiscard]] inline std::size_t volume_cell_count(const Pyro3DConfig& config) {
    if (config.grid_width == 0 || config.grid_height == 0 || config.grid_depth == 0) {
        throw std::runtime_error("pyro 3D grid dimensions must be positive");
    }
    const std::size_t width = static_cast<std::size_t>(config.grid_width);
    const std::size_t height = static_cast<std::size_t>(config.grid_height);
    const std::size_t depth = static_cast<std::size_t>(config.grid_depth);
    if (width > std::numeric_limits<std::size_t>::max() / height) {
        throw std::runtime_error("pyro 3D grid dimensions are too large");
    }
    const std::size_t slice = width * height;
    if (slice > std::numeric_limits<std::size_t>::max() / depth) {
        throw std::runtime_error("pyro 3D grid dimensions are too large");
    }
    return slice * depth;
}

[[nodiscard]] inline std::size_t volume_byte_size(const Pyro3DConfig& config,
                                                  std::size_t bytes_per_cell) {
    if (bytes_per_cell == 0) {
        throw std::runtime_error("pyro 3D volume byte size requires a positive cell size");
    }
    const std::size_t cells = volume_cell_count(config);
    if (cells > std::numeric_limits<std::size_t>::max() / bytes_per_cell) {
        throw std::runtime_error("pyro 3D volume is too large");
    }
    return cells * bytes_per_cell;
}

[[nodiscard]] inline std::size_t shadow_volume_cell_count(const Pyro3DConfig& config) {
    if (config.shadow_grid_width == 0 || config.shadow_grid_height == 0 ||
        config.shadow_grid_depth == 0) {
        throw std::runtime_error("pyro 3D shadow grid dimensions must be positive");
    }
    const std::size_t width = static_cast<std::size_t>(config.shadow_grid_width);
    const std::size_t height = static_cast<std::size_t>(config.shadow_grid_height);
    const std::size_t depth = static_cast<std::size_t>(config.shadow_grid_depth);
    if (width > std::numeric_limits<std::size_t>::max() / height) {
        throw std::runtime_error("pyro 3D shadow grid dimensions are too large");
    }
    const std::size_t slice = width * height;
    if (slice > std::numeric_limits<std::size_t>::max() / depth) {
        throw std::runtime_error("pyro 3D shadow grid dimensions are too large");
    }
    return slice * depth;
}

[[nodiscard]] inline Pyro3DConfig pyro_3d_config_from_run_config(const RunConfig& config,
                                                                 Pyro3DMode mode) {
    Pyro3DConfig result;
    result.mode = mode;
    result.source_count = mode == Pyro3DMode::Fire ? 1U : kDefaultExplosion3DSourceCount;
    result.source_radius =
        mode == Pyro3DMode::Fire ? kDefaultFireSourceRadius : kDefaultExplosion3DSourceRadius;
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
    if (config.pyro_sources != 0) {
        if (config.pyro_sources > kMaxPyro3DSourceCount) {
            throw std::runtime_error("pyro 3D source count must be 1..16");
        }
        result.source_count = config.pyro_sources;
    }
    if (run_config_float_is_set(config.pyro_source_radius)) {
        result.source_radius = config.pyro_source_radius;
    }
    if (result.source_radius <= 0.0F) {
        throw std::runtime_error("pyro 3D source radius must be positive");
    }
    if (run_config_float_is_set(config.pyro_source_force)) {
        result.source_velocity_strength = config.pyro_source_force;
    }
    if (run_config_float_is_set(config.pyro_soot)) {
        result.source_smoke_amount = config.pyro_soot;
    }
    if (run_config_float_is_set(config.pyro_temperature)) {
        result.source_heat_amount = config.pyro_temperature;
    }
    if (run_config_float_is_set(config.pyro_fuel)) {
        result.source_flame_amount = config.pyro_fuel;
    }
    if (run_config_float_is_set(config.explosion_interval_seconds)) {
        result.explosion_interval_seconds = config.explosion_interval_seconds;
    }
    if (run_config_float_is_set(config.explosion_duration_seconds)) {
        result.explosion_duration_seconds = config.explosion_duration_seconds;
    }
    if (run_config_float_is_set(config.explosion_boost)) {
        result.explosion_boost = config.explosion_boost;
    }
    if (result.explosion_interval_seconds <= 0.0F) {
        throw std::runtime_error("pyro 3D explosion interval must be positive");
    }
    if (result.explosion_duration_seconds <= 0.0F) {
        throw std::runtime_error("pyro 3D explosion duration must be positive");
    }
    if (result.explosion_duration_seconds > result.explosion_interval_seconds) {
        throw std::runtime_error("pyro 3D explosion duration must not exceed the interval");
    }
    if (result.explosion_boost < 0.0F) {
        throw std::runtime_error("pyro 3D explosion boost must be nonnegative");
    }
    if (run_config_float_is_set(config.pyro_ignition_temperature)) {
        result.fire_ignition_temperature = config.pyro_ignition_temperature;
    }
    if (run_config_float_is_set(config.pyro_burn_rate)) {
        result.fire_burn_rate = config.pyro_burn_rate;
    }
    if (run_config_float_is_set(config.pyro_heat_output)) {
        result.fire_heat_output = config.pyro_heat_output;
    }
    if (run_config_float_is_set(config.pyro_soot_yield)) {
        result.fire_soot_yield = config.pyro_soot_yield;
    }
    if (run_config_float_is_set(config.pyro_expansion)) {
        result.fire_expansion = config.pyro_expansion;
    }
    if (run_config_float_is_set(config.pyro_flame_cooling)) {
        result.fire_flame_cooling = config.pyro_flame_cooling;
    }
    if (run_config_float_is_set(config.pyro_shredding)) {
        result.fire_shredding = config.pyro_shredding;
    }
    if (run_config_float_is_set(config.pyro_turbulence)) {
        result.fire_turbulence = config.pyro_turbulence;
    }
    if (run_config_float_is_set(config.pyro_obstacle_height)) {
        result.obstacle_center_height = config.pyro_obstacle_height;
    }
    if (run_config_float_is_set(config.pyro_obstacle_radius)) {
        result.obstacle_radius = config.pyro_obstacle_radius;
    }
    if (result.obstacle_center_height < 0.0F || result.obstacle_center_height > 1.0F) {
        throw std::runtime_error("pyro 3D obstacle height must be in [0, 1]");
    }
    if (result.obstacle_radius < 0.0F || result.obstacle_radius > kMaxPyro3DObstacleRadius) {
        throw std::runtime_error("pyro 3D obstacle radius must be in [0, 0.5]");
    }
    if (run_config_float_is_set(config.pyro_buoyancy)) {
        result.buoyancy_strength = config.pyro_buoyancy;
    }
    static_cast<void>(volume_cell_count(result));
    static_cast<void>(shadow_volume_cell_count(result));
    return result;
}

[[nodiscard]] inline std::uint32_t pyro_3d_headless_frame_count(const RunConfig& config) {
    return config.frames == 0 ? 120U : config.frames;
}

[[nodiscard]] inline FrameTiming fixed_pyro_3d_headless_timing(const Pyro3DConfig& config,
                                                               std::uint64_t frame_index) {
    if (frame_index == 0) {
        throw std::runtime_error("fixed pyro 3D frame index must be positive");
    }
    return {
        .delta_seconds = config.fixed_delta_seconds,
        .elapsed_seconds =
            static_cast<double>(config.fixed_delta_seconds) * static_cast<double>(frame_index),
        .frame_index = frame_index,
    };
}

} // namespace cubey::projects::fluid::pyro_3d
