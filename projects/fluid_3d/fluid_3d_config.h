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

enum class Fluid3DScenario : std::uint32_t {
    SmokePlume = 0,
    Explosion = 1,
    Fire = 2,
};

inline constexpr std::uint32_t kMaxFluid3DSourceCount = 16;

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
    std::uint32_t source_count = 3;
    float fixed_delta_seconds = 1.0F / 60.0F;
    float density_decay_per_second = 0.99F;
    float velocity_decay_per_second = 0.99F;
    Fluid3DScenario scenario = Fluid3DScenario::SmokePlume;
    float source_radius = 0.05F;
    float source_velocity_strength = 6.0F;
    float source_smoke_amount = 6.0F;
    float source_heat_amount = 1.4F;
    float source_flame_amount = 2.0F;
    float explosion_interval_seconds = 3.0F;
    float explosion_duration_seconds = 0.12F;
    float explosion_boost = 18.0F;
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

[[nodiscard]] inline const char* fluid_3d_scenario_name(Fluid3DScenario scenario) {
    switch (scenario) {
    case Fluid3DScenario::SmokePlume:
        return "Smoke plume";
    case Fluid3DScenario::Explosion:
        return "Explosion";
    case Fluid3DScenario::Fire:
        return "Fire";
    }
    return "Smoke plume";
}

[[nodiscard]] inline Fluid3DScenario fluid_3d_scenario_from_string(std::string_view name) {
    if (name == "smoke-plume" || name == "plume") {
        return Fluid3DScenario::SmokePlume;
    }
    if (name == "explosion") {
        return Fluid3DScenario::Explosion;
    }
    if (name == "fire") {
        return Fluid3DScenario::Fire;
    }
    throw std::runtime_error("fluid 3D scenario must be smoke-plume, explosion, or fire");
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
    if (config.fluid_sources != 0) {
        if (config.fluid_sources > kMaxFluid3DSourceCount) {
            throw std::runtime_error("fluid 3D source count must be 1..16");
        }
        result.source_count = config.fluid_sources;
    }
    result.scenario = fluid_3d_scenario_from_string(config.fluid_scenario);
    result.source_radius = config.fluid_source_radius;
    result.source_velocity_strength = config.fluid_source_force;
    result.source_smoke_amount = config.fluid_smoke;
    result.source_heat_amount = config.fluid_heat;
    result.source_flame_amount = config.fluid_flame;
    result.explosion_interval_seconds = config.fluid_explosion_interval_seconds;
    result.explosion_duration_seconds = config.fluid_explosion_duration_seconds;
    result.explosion_boost = config.fluid_explosion_boost;
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
