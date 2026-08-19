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
#include <string>
#include <string_view>

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
};

enum class Smoke2DPressureSolver : std::uint32_t {
    Jacobi = 0,
    RedBlackGaussSeidel = 1,
};

inline constexpr std::uint32_t kMaxProceduralInjectorCount = 16;
inline constexpr std::uint32_t kSmoke2DComputeGroupSize = 8;
inline constexpr std::uint32_t kSmoke2DSimulationPushConstantFloatCount = 16;

struct Smoke2DConfig {
    std::uint32_t grid_width = 1024;
    std::uint32_t grid_height = 1024;
    std::uint32_t procedural_injector_count = 5;
    std::uint32_t pressure_iterations = 40;
    Smoke2DPressureSolver pressure_solver = Smoke2DPressureSolver::Jacobi;
    float fixed_delta_seconds = 1.0F / 60.0F;
    float dye_decay_per_second = 0.992F;
    float velocity_decay_per_second = 0.996F;
    float injector_injection_radius = 0.026F;
    float injector_injection_strength = 7.2F;
    float injector_propulsion_strength = 1.35F;
    float injector_orbit_radius = 0.26F;
    float injector_orbit_radius_spread = 0.28F;
    float injector_orbit_angular_speed = 0.0F;
    float injector_orbit_angular_speed_spread = 1.1F;
    float injector_orbit_phase_spread = 1.0F;
    float vorticity_strength = 22.0F;
    float advection_strength = 0.18F;
    float low_energy_cleanup_strength = 0.12F;
    float low_energy_cleanup_start = 0.030F;
    float low_energy_cleanup_end = 0.20F;
    std::uint32_t profile_diagnostic_interval = 1;
    bool profile_diagnostics = false;
    bool headless = false;
};

// Startup overrides are intentionally separate from the concrete simulation
// product.  The former model "unset" with optional values; the latter keeps
// the complete, validated runtime defaults used by shaders and UI.
struct Smoke2DStartupOptions {
    std::optional<std::uint32_t> injectors{};
    std::optional<std::uint32_t> pressure_iterations{};
    std::optional<std::string> pressure_solver{};
    std::optional<float> dye_decay{};
    std::optional<float> velocity_decay{};
    std::optional<float> injector_radius{};
    std::optional<float> injector_force{};
    std::optional<float> injector_propulsion{};
    std::optional<float> injector_orbit_radius{};
    std::optional<float> injector_orbit_radius_spread{};
    std::optional<float> injector_orbit_angular_speed{};
    std::optional<float> injector_orbit_angular_speed_spread{};
    std::optional<float> injector_orbit_phase_spread{};
    std::optional<float> vorticity{};
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
        return Smoke2DDebugView::Dye;
    }
    return Smoke2DDebugView::Dye;
}

[[nodiscard]] inline const char* smoke_2d_pressure_solver_name(Smoke2DPressureSolver solver) {
    switch (solver) {
    case Smoke2DPressureSolver::Jacobi:
        return "Jacobi";
    case Smoke2DPressureSolver::RedBlackGaussSeidel:
        return "RBGS";
    }
    return "Jacobi";
}

[[nodiscard]] inline Smoke2DPressureSolver
smoke_2d_pressure_solver_from_name(std::string_view name) {
    if (name.empty() || name == "jacobi") {
        return Smoke2DPressureSolver::Jacobi;
    }
    if (name == "rbgs" || name == "red-black-gauss-seidel") {
        return Smoke2DPressureSolver::RedBlackGaussSeidel;
    }
    throw std::runtime_error("smoke pressure solver must be jacobi or rbgs");
}

[[nodiscard]] inline const char* smoke_2d_debug_view_name(Smoke2DDebugView view) {
    switch (view) {
    case Smoke2DDebugView::Dye:
        return "Dye";
    case Smoke2DDebugView::Velocity:
        return "Velocity";
    case Smoke2DDebugView::Divergence:
        return "Divergence";
    case Smoke2DDebugView::Pressure:
        return "Pressure";
    case Smoke2DDebugView::Speed:
        return "Speed";
    case Smoke2DDebugView::Vorticity:
        return "Vorticity";
    }
    return "Dye";
}

[[nodiscard]] inline Smoke2DDebugView smoke_2d_debug_view_from_name(std::string_view name) {
    if (name.empty() || name == "dye") {
        return Smoke2DDebugView::Dye;
    }
    if (name == "velocity") {
        return Smoke2DDebugView::Velocity;
    }
    if (name == "divergence") {
        return Smoke2DDebugView::Divergence;
    }
    if (name == "pressure") {
        return Smoke2DDebugView::Pressure;
    }
    if (name == "speed") {
        return Smoke2DDebugView::Speed;
    }
    if (name == "vorticity") {
        return Smoke2DDebugView::Vorticity;
    }
    throw std::runtime_error("smoke 2D debug view must be dye, velocity, divergence, pressure, "
                             "speed, or vorticity");
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

[[nodiscard]] inline Smoke2DConfig
smoke_2d_config_from_options(const common::FluidGridOptions& grid,
                             const Smoke2DStartupOptions& options,
                             const host::CommonRunConfig& common_config) {
    Smoke2DConfig smoke_config;
    if (grid.width) {
        smoke_config.grid_width = *grid.width;
    }
    if (grid.height) {
        smoke_config.grid_height = *grid.height;
    }
    if (options.injectors) {
        if (*options.injectors > kMaxProceduralInjectorCount) {
            throw std::runtime_error("smoke injector count must be 1..16");
        }
        smoke_config.procedural_injector_count = *options.injectors;
    }
    if (options.pressure_iterations) {
        smoke_config.pressure_iterations = *options.pressure_iterations;
    }
    smoke_config.pressure_solver =
        smoke_2d_pressure_solver_from_name(options.pressure_solver.value_or(""));
    if (options.dye_decay) {
        smoke_config.dye_decay_per_second = *options.dye_decay;
    }
    if (options.velocity_decay) {
        smoke_config.velocity_decay_per_second = *options.velocity_decay;
    }
    if (options.injector_radius) {
        smoke_config.injector_injection_radius = *options.injector_radius;
    }
    if (options.injector_force) {
        smoke_config.injector_injection_strength = *options.injector_force;
    }
    if (options.injector_propulsion) {
        smoke_config.injector_propulsion_strength = *options.injector_propulsion;
    }
    if (options.injector_orbit_radius) {
        smoke_config.injector_orbit_radius = *options.injector_orbit_radius;
    }
    if (options.injector_orbit_radius_spread) {
        smoke_config.injector_orbit_radius_spread = *options.injector_orbit_radius_spread;
    }
    if (options.injector_orbit_angular_speed) {
        smoke_config.injector_orbit_angular_speed = *options.injector_orbit_angular_speed;
    }
    if (options.injector_orbit_angular_speed_spread) {
        smoke_config.injector_orbit_angular_speed_spread =
            *options.injector_orbit_angular_speed_spread;
    }
    if (options.injector_orbit_phase_spread) {
        smoke_config.injector_orbit_phase_spread = *options.injector_orbit_phase_spread;
    }
    if (options.vorticity) {
        smoke_config.vorticity_strength = *options.vorticity;
    }
    smoke_config.profile_diagnostics = common_config.profile_diagnostics;
    smoke_config.profile_diagnostic_interval = common_config.profile_diagnostic_interval;
    smoke_config.headless = common_config.headless;
    static_cast<void>(field_cell_count(smoke_config));
    if (smoke_config.profile_diagnostics && !smoke_config.headless) {
        throw std::runtime_error("smoke 2D profile diagnostics require --headless");
    }
    if (smoke_config.pressure_iterations == 0) {
        throw std::runtime_error("smoke pressure iterations must be positive");
    }
    if (smoke_config.dye_decay_per_second < 0.0F || smoke_config.dye_decay_per_second > 1.0F) {
        throw std::runtime_error("smoke dye decay must be in [0, 1]");
    }
    if (smoke_config.velocity_decay_per_second < 0.0F ||
        smoke_config.velocity_decay_per_second > 1.0F) {
        throw std::runtime_error("smoke velocity decay must be in [0, 1]");
    }
    if (smoke_config.injector_injection_radius <= 0.0F) {
        throw std::runtime_error("smoke injector radius must be positive");
    }
    if (smoke_config.injector_injection_strength < 0.0F) {
        throw std::runtime_error("smoke injector strength must be nonnegative");
    }
    if (smoke_config.injector_propulsion_strength < 0.0F) {
        throw std::runtime_error("smoke injector propulsion must be nonnegative");
    }
    if (smoke_config.injector_orbit_radius <= 0.0F) {
        throw std::runtime_error("smoke injector orbit radius must be positive");
    }
    if (smoke_config.injector_orbit_radius_spread < 0.0F) {
        throw std::runtime_error("smoke injector orbit radius spread must be nonnegative");
    }
    if (smoke_config.injector_orbit_angular_speed_spread < 0.0F) {
        throw std::runtime_error("smoke injector orbit speed spread must be nonnegative");
    }
    if (smoke_config.injector_orbit_phase_spread < 0.0F) {
        throw std::runtime_error("smoke injector orbit phase spread must be nonnegative");
    }
    if (smoke_config.vorticity_strength < 0.0F) {
        throw std::runtime_error("smoke vorticity must be nonnegative");
    }
    if (smoke_config.advection_strength <= 0.0F) {
        throw std::runtime_error("smoke advection strength must be positive");
    }
    if (smoke_config.low_energy_cleanup_strength < 0.0F ||
        smoke_config.low_energy_cleanup_strength > 1.0F) {
        throw std::runtime_error("smoke low-energy cleanup strength must be in [0, 1]");
    }
    if (smoke_config.low_energy_cleanup_start < 0.0F ||
        smoke_config.low_energy_cleanup_end <= smoke_config.low_energy_cleanup_start) {
        throw std::runtime_error("smoke low-energy cleanup range is invalid");
    }
    return smoke_config;
}

[[nodiscard]] inline std::uint32_t headless_frame_count(const host::CommonRunConfig& config) {
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
