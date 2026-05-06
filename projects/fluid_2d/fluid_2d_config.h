#pragma once

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

struct Fluid2DConfig {
    std::uint32_t grid_width = 256;
    std::uint32_t grid_height = 144;
    std::uint32_t compute_group_size = 8;
    float fixed_delta_seconds = 1.0F / 60.0F;
    float dye_decay_per_second = 0.985F;
    float velocity_decay_per_second = 0.992F;
};

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

[[nodiscard]] inline std::uint32_t headless_frame_count(const RunConfig& config) {
    if (config.frames == 0) {
        return 120;
    }
    return config.frames;
}

} // namespace cubey::projects::fluid_2d
