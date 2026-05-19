#pragma once

#include <cubey/core/frame_clock.h>
#include <cubey/core/run_config.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace cubey::projects::fluid_3d {

enum class Fluid3DDebugView : std::uint32_t {
    Smoke = 0,
    DensitySlice = 1,
    Velocity = 2,
};

inline constexpr std::uint32_t kMaxFluid3DInjectorCount = 16;

struct Fluid3DConfig {
    std::uint32_t grid_width = 96;
    std::uint32_t grid_height = 96;
    std::uint32_t grid_depth = 96;
    std::uint32_t compute_group_size = 4;
    std::uint32_t pressure_iterations = 12;
    std::uint32_t raymarch_steps = 96;
    std::uint32_t injector_count = 4;
    float fixed_delta_seconds = 1.0F / 60.0F;
    float density_decay_per_second = 0.990F;
    float velocity_decay_per_second = 0.992F;
    float injector_radius = 0.085F;
    float injector_strength = 6.0F;
    float injector_velocity_scale = 1.3F;
    float vorticity_strength = 0.22F;
    float absorption = 5.0F;
    float emission = 1.6F;
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

[[nodiscard]] inline std::size_t volume_rgba32f_byte_size(const Fluid3DConfig& config) {
    constexpr std::size_t kRgba32fBytes = sizeof(float) * 4U;
    const std::size_t cells = volume_cell_count(config);
    if (cells > std::numeric_limits<std::size_t>::max() / kRgba32fBytes) {
        throw std::runtime_error("fluid 3D volume is too large");
    }
    return cells * kRgba32fBytes;
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
    if (config.injectors != 0) {
        if (config.injectors > kMaxFluid3DInjectorCount) {
            throw std::runtime_error("fluid 3D injector count must be 1..16");
        }
        result.injector_count = config.injectors;
    }
    result.injector_strength = config.injector_force;
    static_cast<void>(volume_cell_count(result));
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
