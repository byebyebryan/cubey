#pragma once

#include <cubey/frame_clock.h>
#include <cubey/run_config.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace cubey::projects::fluid_2d {

struct FluidCellGpu {
    std::array<float, 4> dye{};
    std::array<float, 4> velocity{};
};

static_assert(sizeof(FluidCellGpu) == sizeof(float) * 8U);

enum class FluidDebugView : std::uint32_t {
    Dye = 0,
    Velocity = 1,
    Divergence = 2,
    Pressure = 3,
};

struct Fluid2DConfig {
    std::uint32_t grid_width = 256;
    std::uint32_t grid_height = 144;
    std::uint32_t compute_group_size = 8;
    std::uint32_t pressure_iterations = 24;
    float fixed_delta_seconds = 1.0F / 60.0F;
    float dye_decay_per_second = 0.985F;
    float velocity_decay_per_second = 0.992F;
};

[[nodiscard]] inline FluidDebugView next_debug_view(FluidDebugView view) {
    switch (view) {
    case FluidDebugView::Dye:
        return FluidDebugView::Velocity;
    case FluidDebugView::Velocity:
        return FluidDebugView::Divergence;
    case FluidDebugView::Divergence:
        return FluidDebugView::Pressure;
    case FluidDebugView::Pressure:
        return FluidDebugView::Dye;
    }
    return FluidDebugView::Dye;
}

[[nodiscard]] inline std::size_t field_cell_count(const Fluid2DConfig& config) {
    if (config.grid_width == 0 || config.grid_height == 0) {
        throw std::runtime_error("fluid grid dimensions must be positive");
    }

    const std::size_t width = static_cast<std::size_t>(config.grid_width);
    const std::size_t height = static_cast<std::size_t>(config.grid_height);
    if (width > std::numeric_limits<std::size_t>::max() / height) {
        throw std::runtime_error("fluid grid dimensions are too large");
    }
    return width * height;
}

[[nodiscard]] inline std::size_t field_byte_size(const Fluid2DConfig& config) {
    const std::size_t cell_count = field_cell_count(config);
    if (cell_count > std::numeric_limits<std::size_t>::max() / sizeof(FluidCellGpu)) {
        throw std::runtime_error("fluid field is too large");
    }
    return cell_count * sizeof(FluidCellGpu);
}

[[nodiscard]] inline std::size_t scalar_field_byte_size(const Fluid2DConfig& config) {
    const std::size_t cell_count = field_cell_count(config);
    if (cell_count > std::numeric_limits<std::size_t>::max() / sizeof(float)) {
        throw std::runtime_error("fluid scalar field is too large");
    }
    return cell_count * sizeof(float);
}

[[nodiscard]] inline std::uint32_t headless_frame_count(const RunConfig& config) {
    if (config.frames == 0) {
        return 120;
    }
    return config.frames;
}

[[nodiscard]] inline FrameTiming fixed_headless_timing(const Fluid2DConfig& config,
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

} // namespace cubey::projects::fluid_2d
