#pragma once

#include <cubey/core/frame_clock.h>
#include <cubey/core/run_config.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace cubey::projects::fluid::smoke_2d {

struct SmokeCellGpu {
    std::array<float, 4> dye{};
    std::array<float, 4> velocity{};
};

static_assert(sizeof(SmokeCellGpu) == sizeof(float) * 8U);

enum class Smoke2DDebugView : std::uint32_t {
    Dye = 0,
    Velocity = 1,
    Divergence = 2,
    Pressure = 3,
    Speed = 4,
    Vorticity = 5,
    Obstacle = 6,
};

inline constexpr std::uint32_t kMaxProceduralInjectorCount = 16;
inline constexpr std::uint32_t kSmoke2DComputeGroupSize = 8;
inline constexpr std::uint32_t kSmoke2DSimulationPushConstantFloatCount = 12;

struct Smoke2DConfig {
    std::uint32_t grid_width = 1024;
    std::uint32_t grid_height = 1024;
    std::uint32_t procedural_injector_count = 3;
    std::uint32_t pressure_iterations = 32;
    float fixed_delta_seconds = 1.0F / 60.0F;
    float dye_decay_per_second = 0.990F;
    float velocity_decay_per_second = 0.993F;
    float injector_injection_radius = 0.032F;
    float injector_injection_strength = 6.0F;
    float injector_propulsion_strength = 1.0F;
    float injector_orbit_radius = 0.25F;
    float injector_orbit_radius_spread = 0.22F;
    float injector_orbit_angular_speed = 0.0F;
    float injector_orbit_angular_speed_spread = 0.8F;
    float injector_orbit_phase_spread = 1.0F;
    float vorticity_strength = 18.0F;
    bool obstacles_enabled = false;
};

[[nodiscard]] inline Smoke2DDebugView next_debug_view(Smoke2DDebugView view) {
    switch (view) {
    case Smoke2DDebugView::Dye:
        return Smoke2DDebugView::Velocity;
    case Smoke2DDebugView::Velocity:
        return Smoke2DDebugView::Divergence;
    case Smoke2DDebugView::Divergence:
        return Smoke2DDebugView::Pressure;
    case Smoke2DDebugView::Pressure:
        return Smoke2DDebugView::Speed;
    case Smoke2DDebugView::Speed:
        return Smoke2DDebugView::Vorticity;
    case Smoke2DDebugView::Vorticity:
        return Smoke2DDebugView::Obstacle;
    case Smoke2DDebugView::Obstacle:
        return Smoke2DDebugView::Dye;
    }
    return Smoke2DDebugView::Dye;
}

[[nodiscard]] inline std::size_t field_cell_count(const Smoke2DConfig& config) {
    if (config.grid_width == 0 || config.grid_height == 0) {
        throw std::runtime_error("smoke grid dimensions must be positive");
    }

    const std::size_t width = static_cast<std::size_t>(config.grid_width);
    const std::size_t height = static_cast<std::size_t>(config.grid_height);
    if (width > std::numeric_limits<std::size_t>::max() / height) {
        throw std::runtime_error("smoke grid dimensions are too large");
    }
    return width * height;
}

[[nodiscard]] inline std::size_t field_byte_size(const Smoke2DConfig& config) {
    const std::size_t cell_count = field_cell_count(config);
    if (cell_count > std::numeric_limits<std::size_t>::max() / sizeof(SmokeCellGpu)) {
        throw std::runtime_error("smoke field is too large");
    }
    return cell_count * sizeof(SmokeCellGpu);
}

[[nodiscard]] inline std::size_t scalar_field_byte_size(const Smoke2DConfig& config) {
    const std::size_t cell_count = field_cell_count(config);
    if (cell_count > std::numeric_limits<std::size_t>::max() / sizeof(float)) {
        throw std::runtime_error("smoke scalar field is too large");
    }
    return cell_count * sizeof(float);
}

[[nodiscard]] inline Smoke2DConfig smoke_2d_config_from_run_config(const RunConfig& config) {
    Smoke2DConfig smoke_config;
    if (config.grid.width != 0) {
        smoke_config.grid_width = config.grid.width;
    }
    if (config.grid.height != 0) {
        smoke_config.grid_height = config.grid.height;
    }
    if (config.smoke.injectors != 0) {
        if (config.smoke.injectors > kMaxProceduralInjectorCount) {
            throw std::runtime_error("smoke injector count must be 1..16");
        }
        smoke_config.procedural_injector_count = config.smoke.injectors;
    }
    if (run_config_float_is_set(config.smoke.injector_force)) {
        smoke_config.injector_injection_strength = config.smoke.injector_force;
    }
    if (run_config_float_is_set(config.smoke.injector_propulsion)) {
        smoke_config.injector_propulsion_strength = config.smoke.injector_propulsion;
    }
    if (run_config_float_is_set(config.smoke.injector_orbit_radius)) {
        smoke_config.injector_orbit_radius = config.smoke.injector_orbit_radius;
    }
    if (run_config_float_is_set(config.smoke.injector_orbit_radius_spread)) {
        smoke_config.injector_orbit_radius_spread = config.smoke.injector_orbit_radius_spread;
    }
    if (run_config_float_is_set(config.smoke.injector_orbit_angular_speed)) {
        smoke_config.injector_orbit_angular_speed = config.smoke.injector_orbit_angular_speed;
    }
    if (run_config_float_is_set(config.smoke.injector_orbit_angular_speed_spread)) {
        smoke_config.injector_orbit_angular_speed_spread =
            config.smoke.injector_orbit_angular_speed_spread;
    }
    if (run_config_float_is_set(config.smoke.injector_orbit_phase_spread)) {
        smoke_config.injector_orbit_phase_spread = config.smoke.injector_orbit_phase_spread;
    }
    smoke_config.obstacles_enabled = config.smoke.obstacles;
    static_cast<void>(field_cell_count(smoke_config));
    return smoke_config;
}

[[nodiscard]] inline std::uint32_t headless_frame_count(const RunConfig& config) {
    if (config.frames == 0) {
        return 120;
    }
    return config.frames;
}

[[nodiscard]] inline FrameTiming fixed_headless_timing(const Smoke2DConfig& config,
                                                       std::uint64_t frame_index) {
    if (frame_index == 0) {
        throw std::runtime_error("fixed headless frame index must be positive");
    }
    return {
        .delta_seconds = config.fixed_delta_seconds,
        .elapsed_seconds =
            static_cast<double>(config.fixed_delta_seconds) * static_cast<double>(frame_index),
        .frame_index = frame_index,
    };
}

} // namespace cubey::projects::fluid::smoke_2d
