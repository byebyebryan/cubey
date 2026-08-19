#pragma once

#include "../common/fluid_config_schema.h"

#include <cubey/core/frame_clock.h>
#include <cubey/host/common_config.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
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
inline constexpr std::uint32_t kPyro3DRenderPushConstantFloatCount = 32;
inline constexpr float kDefaultPyro3DSourceRadius = 0.05F;
inline constexpr float kDefaultFire3DSourceHeight = 0.10F;
inline constexpr float kDefaultFireSourceRadius = 0.125F;
inline constexpr std::uint32_t kDefaultExplosion3DSourceCount = 9;
inline constexpr float kDefaultExplosion3DSourceHeight = 0.10F;
inline constexpr float kDefaultExplosion3DSourceRadius = 0.025F;
inline constexpr float kDefaultFire3DObstacleHeight = 0.58F;
inline constexpr float kDefaultFire3DObstacleRadius = 0.105F;
inline constexpr float kDefaultExplosion3DObstacleHeight = 0.48F;
inline constexpr float kDefaultExplosion3DObstacleRadius = 0.15F;
inline constexpr float kDefaultPyro3DObstacleHeight = kDefaultFire3DObstacleHeight;
inline constexpr float kDefaultPyro3DObstacleRadius = kDefaultFire3DObstacleRadius;
inline constexpr float kMaxPyro3DObstacleRadius = 0.5F;

// Startup-only values preserve omission explicitly.  The runtime product
// below keeps concrete defaults for simulation, shaders, and UI code.
struct Pyro3DStartupOptions {
    std::optional<std::uint32_t> shadow_grid_width{};
    std::optional<std::uint32_t> shadow_grid_height{};
    std::optional<std::uint32_t> shadow_grid_depth{};
    std::optional<std::uint32_t> shadow_steps{};
    std::optional<std::uint32_t> shadow_update_interval{};
    std::optional<std::uint32_t> sources{};
    std::optional<float> source_height{};
    std::optional<float> source_radius{};
    std::optional<float> source_force{};
    std::optional<float> soot{};
    std::optional<float> temperature{};
    std::optional<float> fuel{};
    std::optional<float> buoyancy{};
    std::optional<float> ignition_temperature{};
    std::optional<float> burn_rate{};
    std::optional<float> heat_output{};
    std::optional<float> soot_yield{};
    std::optional<float> expansion{};
    std::optional<float> flame_cooling{};
    std::optional<float> shredding{};
    std::optional<float> turbulence{};
    std::optional<float> obstacle_height{};
    std::optional<float> obstacle_radius{};
    std::optional<float> explosion_interval_seconds{};
    std::optional<float> explosion_duration_seconds{};
    std::optional<float> explosion_boost{};
};

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
    float source_center_height = kDefaultFire3DSourceHeight;
    float source_radius = kDefaultFireSourceRadius;
    float source_velocity_strength = 8.5F;
    float source_smoke_amount = 16.0F;
    float source_heat_amount = 2.8F;
    float source_flame_amount = 4.0F;
    float explosion_interval_seconds = 3.0F;
    float explosion_duration_seconds = 0.50F;
    float explosion_boost = 20.0F;
    float fire_ignition_temperature = 0.22F;
    float fire_burn_rate = 4.0F;
    float fire_heat_output = 2.8F;
    float fire_soot_yield = 0.45F;
    float fire_expansion = 1.35F;
    float fire_flame_cooling = 3.8F;
    float fire_shredding = 3.6F;
    float fire_turbulence = 0.95F;
    float obstacle_center_height = kDefaultPyro3DObstacleHeight;
    float obstacle_radius = kDefaultPyro3DObstacleRadius;
    float vorticity_strength = 1.0F;
    float buoyancy_strength = 2.5F;
    float absorption = 28.0F;
    float emission = 2.0F;
    float shadow_absorption = 50.0F;
    float ambient_light = 0.5F;
    float render_exposure = 0.42F;
    float render_background_lift = 0.42F;
    float render_rim_strength = 1.25F;
    float render_scatter_strength = 1.15F;
    float render_smoke_warmth = 0.18F;
    float render_flame_intensity = 1.65F;
    float render_flame_core_strength = 1.35F;
    std::uint32_t profile_diagnostic_interval = 1U;
    bool profile_diagnostics = false;
    bool headless = false;
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

[[nodiscard]] inline Pyro3DConfig pyro_3d_config_from_options(
    const common::FluidGridOptions& grid, const Pyro3DStartupOptions& options, Pyro3DMode mode,
    const host::CommonRunConfig& common_config = {}) {
    Pyro3DConfig result;
    result.mode = mode;
    result.source_count = mode == Pyro3DMode::Fire ? 1U : kDefaultExplosion3DSourceCount;
    result.source_radius =
        mode == Pyro3DMode::Fire ? kDefaultFireSourceRadius : kDefaultExplosion3DSourceRadius;
    if (mode == Pyro3DMode::Explosion) {
        result.density_decay_per_second = 0.97F;
        result.source_center_height = kDefaultExplosion3DSourceHeight;
        result.source_velocity_strength = 8.5F;
        result.source_smoke_amount = 8.0F;
        result.source_heat_amount = 2.6F;
        result.source_flame_amount = 4.5F;
        result.fire_expansion = 1.0F;
        result.fire_flame_cooling = 3.4F;
        result.fire_shredding = 2.8F;
        result.fire_turbulence = 0.85F;
        result.obstacle_center_height = kDefaultExplosion3DObstacleHeight;
        result.obstacle_radius = 0.0F;
        result.buoyancy_strength = 1.35F;
        result.absorption = 15.0F;
        result.render_exposure = 0.64F;
        result.render_background_lift = 0.42F;
        result.render_rim_strength = 1.55F;
        result.render_scatter_strength = 1.38F;
        result.render_smoke_warmth = 0.24F;
        result.render_flame_intensity = 2.35F;
        result.render_flame_core_strength = 2.10F;
    }
    if (common_config.profile_diagnostic_interval == 0U) {
        throw std::runtime_error("pyro 3D profile diagnostic interval must be positive");
    }
    if (common_config.profile_diagnostics && !common_config.headless) {
        throw std::runtime_error("pyro 3D profile diagnostics require --headless");
    }
    result.profile_diagnostics = common_config.profile_diagnostics;
    result.profile_diagnostic_interval = common_config.profile_diagnostic_interval;
    result.headless = common_config.headless;

    if (grid.width) {
        result.grid_width = *grid.width;
    }
    if (grid.height) {
        result.grid_height = *grid.height;
    }
    if (grid.depth) {
        result.grid_depth = *grid.depth;
    }
    if (options.shadow_grid_width) {
        result.shadow_grid_width = *options.shadow_grid_width;
    }
    if (options.shadow_grid_height) {
        result.shadow_grid_height = *options.shadow_grid_height;
    }
    if (options.shadow_grid_depth) {
        result.shadow_grid_depth = *options.shadow_grid_depth;
    }
    if (options.shadow_steps) {
        result.shadow_steps = *options.shadow_steps;
    }
    if (options.shadow_update_interval) {
        result.shadow_update_interval = *options.shadow_update_interval;
    }
    if (options.sources) {
        if (*options.sources == 0U || *options.sources > kMaxPyro3DSourceCount) {
            throw std::runtime_error("pyro 3D source count must be 1..16");
        }
        result.source_count = *options.sources;
    }
    if (options.source_height) {
        result.source_center_height = *options.source_height;
    }
    if (result.source_center_height < 0.0F || result.source_center_height > 1.0F) {
        throw std::runtime_error("pyro 3D source height must be in [0, 1]");
    }
    if (options.source_radius) {
        result.source_radius = *options.source_radius;
    }
    if (result.source_radius <= 0.0F) {
        throw std::runtime_error("pyro 3D source radius must be positive");
    }
    if (options.source_force) {
        result.source_velocity_strength = *options.source_force;
    }
    if (options.soot) {
        result.source_smoke_amount = *options.soot;
    }
    if (options.temperature) {
        result.source_heat_amount = *options.temperature;
    }
    if (options.fuel) {
        result.source_flame_amount = *options.fuel;
    }
    if (options.explosion_interval_seconds) {
        result.explosion_interval_seconds = *options.explosion_interval_seconds;
    }
    if (options.explosion_duration_seconds) {
        result.explosion_duration_seconds = *options.explosion_duration_seconds;
    }
    if (options.explosion_boost) {
        result.explosion_boost = *options.explosion_boost;
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
    if (options.ignition_temperature) {
        result.fire_ignition_temperature = *options.ignition_temperature;
    }
    if (options.burn_rate) {
        result.fire_burn_rate = *options.burn_rate;
    }
    if (options.heat_output) {
        result.fire_heat_output = *options.heat_output;
    }
    if (options.soot_yield) {
        result.fire_soot_yield = *options.soot_yield;
    }
    if (options.expansion) {
        result.fire_expansion = *options.expansion;
    }
    if (options.flame_cooling) {
        result.fire_flame_cooling = *options.flame_cooling;
    }
    if (options.shredding) {
        result.fire_shredding = *options.shredding;
    }
    if (options.turbulence) {
        result.fire_turbulence = *options.turbulence;
    }
    if (options.obstacle_height) {
        result.obstacle_center_height = *options.obstacle_height;
    }
    if (options.obstacle_radius) {
        result.obstacle_radius = *options.obstacle_radius;
    }
    if (result.obstacle_center_height < 0.0F || result.obstacle_center_height > 1.0F) {
        throw std::runtime_error("pyro 3D obstacle height must be in [0, 1]");
    }
    if (result.obstacle_radius < 0.0F || result.obstacle_radius > kMaxPyro3DObstacleRadius) {
        throw std::runtime_error("pyro 3D obstacle radius must be in [0, 0.5]");
    }
    if (options.buoyancy) {
        result.buoyancy_strength = *options.buoyancy;
    }
    if (result.shadow_steps == 0U || result.shadow_update_interval == 0U) {
        throw std::runtime_error("pyro 3D shadow settings must be positive");
    }
    if (result.source_velocity_strength < 0.0F || result.source_smoke_amount < 0.0F ||
        result.source_heat_amount < 0.0F || result.source_flame_amount < 0.0F) {
        throw std::runtime_error("pyro 3D source amounts must be nonnegative");
    }
    if (result.fire_ignition_temperature < 0.0F || result.fire_burn_rate < 0.0F ||
        result.fire_heat_output < 0.0F || result.fire_soot_yield < 0.0F ||
        result.fire_expansion < 0.0F || result.fire_flame_cooling < 0.0F ||
        result.fire_shredding < 0.0F || result.fire_turbulence < 0.0F) {
        throw std::runtime_error("pyro 3D fire settings must be nonnegative");
    }
    static_cast<void>(volume_cell_count(result));
    static_cast<void>(shadow_volume_cell_count(result));
    return result;
}

[[nodiscard]] inline Pyro3DConfig pyro_3d_config_from_options(
    const common::FluidGridOptions& grid, const Pyro3DStartupOptions& options,
    const host::CommonRunConfig& common_config, Pyro3DMode mode) {
    return pyro_3d_config_from_options(grid, options, mode, common_config);
}

[[nodiscard]] inline std::uint32_t pyro_3d_headless_frame_count(
    const host::CommonRunConfig& config) {
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
