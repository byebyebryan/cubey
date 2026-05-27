#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string_view>

namespace cubey::projects::fluid::common {

enum class WaterTransferMode : std::uint32_t {
    PicFlip = 0,
    Apic = 1,
};

inline constexpr std::uint32_t kWaterMaxExactShaderInteger = 1U << 24U;

[[nodiscard]] inline const char* water_transfer_mode_name(WaterTransferMode mode) {
    switch (mode) {
    case WaterTransferMode::PicFlip:
        return "PIC/FLIP";
    case WaterTransferMode::Apic:
        return "APIC";
    }
    return "APIC";
}

[[nodiscard]] inline WaterTransferMode
water_transfer_mode_from_name(std::string_view name,
                              const char* error_message =
                                  "water transfer mode must be apic or pic-flip") {
    if (name.empty() || name == "apic") {
        return WaterTransferMode::Apic;
    }
    if (name == "pic-flip" || name == "picflip" || name == "pic/flip") {
        return WaterTransferMode::PicFlip;
    }
    throw std::runtime_error(error_message);
}

[[nodiscard]] inline std::size_t checked_mul(std::size_t lhs, std::size_t rhs,
                                             const char* message) {
    if (lhs != 0 && rhs > std::numeric_limits<std::size_t>::max() / lhs) {
        throw std::runtime_error(message);
    }
    return lhs * rhs;
}

[[nodiscard]] inline std::size_t checked_add(std::size_t lhs, std::size_t rhs,
                                             const char* message) {
    if (rhs > std::numeric_limits<std::size_t>::max() - lhs) {
        throw std::runtime_error(message);
    }
    return lhs + rhs;
}

inline void validate_exact_shader_integer(std::size_t value, const char* message) {
    if (value > kWaterMaxExactShaderInteger) {
        throw std::runtime_error(message);
    }
}

[[nodiscard]] inline float water_shader_count_float(std::size_t value, const char* message) {
    validate_exact_shader_integer(value, message);
    return static_cast<float>(value);
}

[[nodiscard]] inline std::uint32_t water_fill_axis_cell_count(std::uint32_t axis_cells,
                                                              std::uint32_t wall_cells,
                                                              float min_fill_fraction,
                                                              float max_fill_fraction,
                                                              float fill_fraction) {
    const std::uint32_t usable_cells = axis_cells - (wall_cells * 2U);
    const float clamped_fraction =
        std::clamp(fill_fraction, min_fill_fraction, max_fill_fraction);
    const auto raw_fill_cells =
        static_cast<std::uint32_t>(static_cast<float>(axis_cells) * clamped_fraction);
    return std::clamp(raw_fill_cells, 1U, usable_cells);
}

[[nodiscard]] inline std::uint32_t water_runtime_particle_scan_count(
    std::uint32_t active_particle_count, std::uint32_t particle_capacity,
    std::uint32_t touched_particle_count) {
    const std::uint32_t scan_count =
        touched_particle_count == 0 ? active_particle_count : touched_particle_count;
    return std::clamp(scan_count, active_particle_count, particle_capacity);
}

} // namespace cubey::projects::fluid::common
