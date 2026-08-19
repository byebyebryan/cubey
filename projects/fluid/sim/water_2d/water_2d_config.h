#pragma once

#include "../common/fluid_config_schema.h"
#include "../common/water_common.h"

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

namespace cubey::projects::fluid::water_2d {

enum class Water2DDebugView : std::uint32_t {
    Surface = 0,
    Particles = 1,
    Cells = 2,
    Velocity = 3,
    Divergence = 4,
    Pressure = 5,
    Solid = 6,
    Foam = 7,
};

enum class Water2DScenario : std::uint32_t {
    DamBreak = 0,
    ObstacleSplash = 1,
    WaveSlab = 2,
    HoseFill = 3,
};

enum class Water2DObstacleShape : std::uint32_t {
    None = 0,
    Circle = 1,
    Box = 2,
};

using Water2DTransferMode = common::WaterTransferMode;

enum class Water2DDiagnosticSlot : std::uint32_t {
    ActiveParticles = 0,
    InactiveScanParticles = 1,
    NonemptyCells = 2,
    OverpackedCells = 3,
    OverpackedParticles = 4,
    TransferTruncatedParticles = 5,
    MaxCellCount = 6,
    ParticleScanCount = 7,
};

inline constexpr std::uint32_t kWater2DComputeGroupSize = 8;
inline constexpr std::uint32_t kWater2DSimulationPushConstantFloatCount = 8;
inline constexpr std::uint32_t kWater2DSimulationUniformFloatCount = 56;
inline constexpr std::uint32_t kWater2DRenderPushConstantFloatCount = 20;
inline constexpr std::uint32_t kWater2DDefaultGridWidth = 256;
inline constexpr std::uint32_t kWater2DDefaultGridHeight = 144;
inline constexpr std::uint32_t kWater2DWallCells = 2;
inline constexpr std::uint32_t kWater2DMinimumGridWidth = 16;
inline constexpr std::uint32_t kWater2DMinimumGridHeight = 16;
inline constexpr std::uint32_t kWater2DMaxExactShaderInteger =
    common::kWaterMaxExactShaderInteger;
inline constexpr float kWater2DMinFillFraction = 0.08F;
inline constexpr float kWater2DMaxFillFraction = 0.92F;
inline constexpr std::uint32_t kWater2DDefaultHoseParticleCapacity = 262144;
inline constexpr std::uint32_t kWater2DDiagnosticSlotCount = 8;
inline constexpr float kWater2DDiagnosticsModeClear = 0.0F;
inline constexpr float kWater2DDiagnosticsModeWorkload = 1.0F;

struct Water2DHoseConfig {
    bool enabled = false;
    std::array<float, 2> position{0.12F, 0.76F};
    float angle_degrees = -28.0F;
    float speed = 2.0F;
    float radius = 0.035F;
    float particles_per_second = 12000.0F;
    float spread_degrees = 10.0F;
    std::uint32_t particle_capacity = kWater2DDefaultHoseParticleCapacity;
};

struct Water2DDrainConfig {
    bool enabled = false;
    std::array<float, 2> center{0.86F, 0.08F};
    std::array<float, 2> half_size{0.12F, 0.055F};
    float pull_speed = 2.2F;
    float pull_radius = 0.42F;
};

struct Water2DWaveConfig {
    bool enabled = false;
    std::array<float, 2> center{0.11F, 0.30F};
    std::array<float, 2> half_size{0.08F, 0.24F};
    float amplitude = 1.15F;
    float frequency_hz = 0.55F;
};

struct Water2DConfig {
    std::uint32_t grid_width = kWater2DDefaultGridWidth;
    std::uint32_t grid_height = kWater2DDefaultGridHeight;
    std::uint32_t pressure_iterations = 256;
    std::uint32_t particles_per_cell = 4;
    std::uint32_t max_particles_per_cell = 64;
    std::uint32_t active_particle_count = 51200;
    std::uint32_t initial_particle_capacity = 124080;
    std::uint32_t particle_capacity = 386224;
    std::uint32_t substeps = 1;
    Water2DScenario scenario = Water2DScenario::DamBreak;
    Water2DTransferMode transfer_mode = Water2DTransferMode::Apic;
    float fixed_delta_seconds = 1.0F / 60.0F;
    float gravity = -1.60F;
    float flip_ratio = 0.78F;
    float particle_radius = 0.0125F;
    float initial_fill_height = 0.70F;
    float initial_fill_width = 0.50F;
    float velocity_limit = 3.0F;
    float particle_damping = 0.999F;
    float particle_separation_radius = 0.58F;
    float particle_separation_strength = 0.32F;
    float particle_volume_strength = 18.0F;
    float boundary_restitution = 0.18F;
    float obstacle_friction = 0.86F;
    float surface_threshold = 0.82F;
    float edge_strength = 0.52F;
    float surface_splat_radius_scale = 1.65F;
    float surface_density_scale = 0.42F;
    float surface_smoothing_radius_px = 7.0F;
    std::uint32_t surface_smoothing_iterations = 3;
    float surface_refraction_strength = 0.022F;
    float surface_caustic_strength = 0.28F;
    float surface_specular_strength = 0.55F;
    float foam_strength = 0.50F;
    float foam_sharpness = 0.95F;
    float foam_breakup = 0.66F;
    bool profile_diagnostics = false;
    std::uint32_t profile_diagnostic_interval = 1;
    Water2DObstacleShape obstacle_shape = Water2DObstacleShape::None;
    std::array<float, 2> obstacle_center{0.58F, 0.38F};
    float obstacle_radius = 0.095F;
    std::array<float, 2> obstacle_half_size{0.07F, 0.14F};
    Water2DHoseConfig hose{};
    Water2DDrainConfig drain{};
    Water2DWaveConfig wave{};
};

struct Water2DStartupOptions {
    std::optional<std::string> transfer_mode{};
    std::optional<std::uint32_t> transfer_limit{};
    std::optional<bool> hose{};
    std::optional<bool> drain{};
    std::optional<bool> wave{};
};

struct Water2DSimulationUniforms {
    std::array<float, 4> grid_options{};
    std::array<float, 4> init_options{};
    std::array<float, 4> obstacle_options{};
    std::array<float, 4> obstacle_extents{};
    std::array<float, 4> particle_options{};
    std::array<float, 4> solve_options{};
    std::array<float, 4> lifecycle_options{};
    std::array<float, 4> hose_options0{};
    std::array<float, 4> hose_options1{};
    std::array<float, 4> hose_options2{};
    std::array<float, 4> drain_options{};
    std::array<float, 4> drain_extents{};
    std::array<float, 4> wave_options0{};
    std::array<float, 4> wave_options1{};
};

struct Water2DDispatchPushConstants {
    std::array<float, 4> dispatch_options{};
    std::array<float, 4> emit_options{};
};

struct Water2DRuntimeState {
    std::uint32_t hose_cursor = 0;
    std::uint32_t particle_scan_count = 0;
    float hose_emit_accumulator = 0.0F;
    bool pressure_read_b = false;
};

[[nodiscard]] inline std::uint32_t
water_2d_runtime_particle_scan_count(const Water2DConfig& config,
                                     const Water2DRuntimeState& state) {
    return common::water_runtime_particle_scan_count(
        config.active_particle_count, config.particle_capacity, state.particle_scan_count);
}

static_assert(sizeof(Water2DSimulationUniforms) ==
              sizeof(float) * kWater2DSimulationUniformFloatCount);
static_assert(sizeof(Water2DDispatchPushConstants) ==
              sizeof(float) * kWater2DSimulationPushConstantFloatCount);

[[nodiscard]] inline const char* water_2d_debug_view_name(Water2DDebugView view) {
    switch (view) {
    case Water2DDebugView::Surface:
        return "Surface";
    case Water2DDebugView::Particles:
        return "Particles";
    case Water2DDebugView::Cells:
        return "Cells";
    case Water2DDebugView::Velocity:
        return "Velocity";
    case Water2DDebugView::Divergence:
        return "Divergence";
    case Water2DDebugView::Pressure:
        return "Pressure";
    case Water2DDebugView::Solid:
        return "Solid";
    case Water2DDebugView::Foam:
        return "Foam";
    }
    return "Surface";
}

[[nodiscard]] inline const char* water_2d_scenario_name(Water2DScenario scenario) {
    switch (scenario) {
    case Water2DScenario::DamBreak:
        return "Dam break";
    case Water2DScenario::ObstacleSplash:
        return "Obstacle splash";
    case Water2DScenario::WaveSlab:
        return "Wave slab";
    case Water2DScenario::HoseFill:
        return "Hose fill";
    }
    return "Dam break";
}

[[nodiscard]] inline const char* water_2d_obstacle_shape_name(Water2DObstacleShape shape) {
    switch (shape) {
    case Water2DObstacleShape::None:
        return "None";
    case Water2DObstacleShape::Circle:
        return "Circle";
    case Water2DObstacleShape::Box:
        return "Box";
    }
    return "None";
}

[[nodiscard]] inline const char* water_2d_transfer_mode_name(Water2DTransferMode mode) {
    return common::water_transfer_mode_name(mode);
}

[[nodiscard]] inline Water2DTransferMode water_2d_transfer_mode_from_name(std::string_view name) {
    return common::water_transfer_mode_from_name(
        name, "water 2D transfer mode must be apic or pic-flip");
}

[[nodiscard]] inline Water2DDebugView water_2d_debug_view_from_name(std::string_view name) {
    if (name.empty() || name == "surface") {
        return Water2DDebugView::Surface;
    }
    if (name == "particles") {
        return Water2DDebugView::Particles;
    }
    if (name == "cells") {
        return Water2DDebugView::Cells;
    }
    if (name == "velocity") {
        return Water2DDebugView::Velocity;
    }
    if (name == "divergence") {
        return Water2DDebugView::Divergence;
    }
    if (name == "pressure") {
        return Water2DDebugView::Pressure;
    }
    if (name == "solid") {
        return Water2DDebugView::Solid;
    }
    if (name == "foam") {
        return Water2DDebugView::Foam;
    }
    throw std::runtime_error("water 2D debug view must be surface, particles, cells, velocity, "
                             "divergence, pressure, solid, or foam");
}

[[nodiscard]] inline Water2DDebugView next_debug_view(Water2DDebugView view) {
    switch (view) {
    case Water2DDebugView::Surface:
        return Water2DDebugView::Particles;
    case Water2DDebugView::Particles:
        return Water2DDebugView::Cells;
    case Water2DDebugView::Cells:
        return Water2DDebugView::Velocity;
    case Water2DDebugView::Velocity:
        return Water2DDebugView::Divergence;
    case Water2DDebugView::Divergence:
        return Water2DDebugView::Pressure;
    case Water2DDebugView::Pressure:
        return Water2DDebugView::Solid;
    case Water2DDebugView::Solid:
        return Water2DDebugView::Foam;
    case Water2DDebugView::Foam:
        return Water2DDebugView::Surface;
    }
    return Water2DDebugView::Surface;
}

using common::checked_mul;

inline void validate_exact_shader_integer(std::size_t value, const char* message) {
    common::validate_exact_shader_integer(value, message);
}

[[nodiscard]] inline float water_2d_shader_count_float(std::size_t value, const char* message) {
    return common::water_shader_count_float(value, message);
}

inline void validate_water_2d_grid_dimensions(const Water2DConfig& config) {
    if (config.grid_width < kWater2DMinimumGridWidth ||
        config.grid_height < kWater2DMinimumGridHeight) {
        throw std::runtime_error("water grid dimensions must be at least 16x16");
    }
    validate_exact_shader_integer(config.grid_width,
                                  "water grid width exceeds exact shader integer range");
    validate_exact_shader_integer(config.grid_height,
                                  "water grid height exceeds exact shader integer range");
}

[[nodiscard]] inline std::uint32_t water_2d_fill_axis_cell_count(std::uint32_t axis_cells,
                                                                 float fill_fraction) {
    return common::water_fill_axis_cell_count(axis_cells, kWater2DWallCells,
                                              kWater2DMinFillFraction, kWater2DMaxFillFraction,
                                              fill_fraction);
}

[[nodiscard]] inline std::size_t cell_count(const Water2DConfig& config) {
    validate_water_2d_grid_dimensions(config);
    const std::size_t count = checked_mul(static_cast<std::size_t>(config.grid_width),
                                          static_cast<std::size_t>(config.grid_height),
                                          "water grid dimensions are too large");
    validate_exact_shader_integer(count, "water cell count exceeds exact shader integer range");
    return count;
}

[[nodiscard]] inline std::size_t u_face_count(const Water2DConfig& config) {
    validate_water_2d_grid_dimensions(config);
    const std::size_t count =
        checked_mul(static_cast<std::size_t>(config.grid_width) + 1U,
                    static_cast<std::size_t>(config.grid_height), "water U-face grid is too large");
    validate_exact_shader_integer(count, "water U-face count exceeds exact shader integer range");
    return count;
}

[[nodiscard]] inline std::size_t v_face_count(const Water2DConfig& config) {
    validate_water_2d_grid_dimensions(config);
    const std::size_t count = checked_mul(static_cast<std::size_t>(config.grid_width),
                                          static_cast<std::size_t>(config.grid_height) + 1U,
                                          "water V-face grid is too large");
    validate_exact_shader_integer(count, "water V-face count exceeds exact shader integer range");
    return count;
}

[[nodiscard]] inline std::size_t fill_cell_count(const Water2DConfig& config, float fill_width,
                                                 float fill_height) {
    validate_water_2d_grid_dimensions(config);
    const auto fill_cols =
        static_cast<std::size_t>(water_2d_fill_axis_cell_count(config.grid_width, fill_width));
    const auto fill_rows =
        static_cast<std::size_t>(water_2d_fill_axis_cell_count(config.grid_height, fill_height));
    return checked_mul(fill_cols, fill_rows, "water initial fill area is too large");
}

[[nodiscard]] inline std::uint32_t particle_count_for_fill(const Water2DConfig& config,
                                                           float fill_width, float fill_height) {
    if (config.particles_per_cell == 0) {
        throw std::runtime_error("water particles-per-cell must be positive");
    }
    const std::size_t count = checked_mul(fill_cell_count(config, fill_width, fill_height),
                                          static_cast<std::size_t>(config.particles_per_cell),
                                          "water particle count is too large");
    if (count > std::numeric_limits<std::uint32_t>::max()) {
        throw std::runtime_error("water particle count exceeds shader index range");
    }
    validate_exact_shader_integer(count, "water particle count exceeds exact shader integer range");
    return static_cast<std::uint32_t>(count);
}

[[nodiscard]] inline std::uint32_t active_particle_count_for_fill(const Water2DConfig& config) {
    return particle_count_for_fill(config, config.initial_fill_width, config.initial_fill_height);
}

[[nodiscard]] inline std::uint32_t particle_capacity_for_config(const Water2DConfig& config) {
    const std::uint32_t initial_capacity =
        particle_count_for_fill(config, kWater2DMaxFillFraction, kWater2DMaxFillFraction);
    if (config.hose.particle_capacity >
        std::numeric_limits<std::uint32_t>::max() - initial_capacity) {
        throw std::runtime_error("water particle capacity exceeds shader index range");
    }
    const std::uint32_t capacity = initial_capacity + config.hose.particle_capacity;
    validate_exact_shader_integer(capacity,
                                  "water particle capacity exceeds exact shader integer range");
    return capacity;
}

[[nodiscard]] inline std::uint32_t
initial_particle_capacity_for_config(const Water2DConfig& config) {
    return particle_count_for_fill(config, kWater2DMaxFillFraction, kWater2DMaxFillFraction);
}

[[nodiscard]] inline std::uint32_t hose_particle_start_for_config(const Water2DConfig& config) {
    return config.active_particle_count;
}

[[nodiscard]] inline std::uint32_t
hose_particle_pool_capacity_for_config(const Water2DConfig& config) {
    const std::uint32_t pool_start = hose_particle_start_for_config(config);
    if (pool_start > config.particle_capacity) {
        throw std::runtime_error("water hose particle pool starts beyond particle capacity");
    }
    return config.particle_capacity - pool_start;
}

inline void refresh_particle_counts(Water2DConfig& config) {
    config.active_particle_count = active_particle_count_for_fill(config);
    config.initial_particle_capacity = initial_particle_capacity_for_config(config);
    config.particle_capacity = particle_capacity_for_config(config);
    if (config.active_particle_count > config.initial_particle_capacity) {
        throw std::runtime_error("water active particle count exceeds initial particle capacity");
    }
}

inline void apply_water_2d_scenario_defaults(Water2DConfig& config) {
    switch (config.scenario) {
    case Water2DScenario::DamBreak:
        config.initial_fill_width = 0.50F;
        config.initial_fill_height = 0.70F;
        config.obstacle_shape = Water2DObstacleShape::None;
        config.obstacle_center = {0.58F, 0.38F};
        config.obstacle_radius = 0.095F;
        config.obstacle_half_size = {0.07F, 0.14F};
        config.hose.enabled = false;
        config.drain.enabled = false;
        config.wave.enabled = false;
        break;
    case Water2DScenario::ObstacleSplash:
        config.initial_fill_width = 0.62F;
        config.initial_fill_height = 0.62F;
        config.obstacle_shape = Water2DObstacleShape::Circle;
        config.obstacle_center = {0.60F, 0.30F};
        config.obstacle_radius = 0.11F;
        config.obstacle_half_size = {0.07F, 0.14F};
        config.hose.enabled = false;
        config.drain.enabled = false;
        config.wave.enabled = false;
        break;
    case Water2DScenario::WaveSlab:
        config.initial_fill_width = 0.84F;
        config.initial_fill_height = 0.42F;
        config.obstacle_shape = Water2DObstacleShape::None;
        config.obstacle_center = {0.50F, 0.34F};
        config.obstacle_radius = 0.10F;
        config.obstacle_half_size = {0.08F, 0.12F};
        config.hose.enabled = false;
        config.drain.enabled = false;
        config.wave.enabled = true;
        config.wave.center = {0.11F, 0.30F};
        config.wave.half_size = {0.08F, 0.24F};
        config.wave.amplitude = 1.15F;
        config.wave.frequency_hz = 0.55F;
        break;
    case Water2DScenario::HoseFill:
        config.initial_fill_width = 0.28F;
        config.initial_fill_height = 0.18F;
        config.obstacle_shape = Water2DObstacleShape::None;
        config.obstacle_center = {0.58F, 0.38F};
        config.obstacle_radius = 0.095F;
        config.obstacle_half_size = {0.07F, 0.14F};
        config.hose.enabled = true;
        config.hose.position = {0.12F, 0.76F};
        config.hose.angle_degrees = -28.0F;
        config.hose.speed = 2.0F;
        config.hose.radius = 0.035F;
        config.hose.particles_per_second = 12000.0F;
        config.hose.spread_degrees = 10.0F;
        config.drain.enabled = true;
        config.drain.center = {0.86F, 0.08F};
        config.drain.half_size = {0.12F, 0.055F};
        config.drain.pull_speed = 2.2F;
        config.drain.pull_radius = 0.42F;
        config.wave.enabled = false;
        break;
    }
    refresh_particle_counts(config);
}

[[nodiscard]] inline std::size_t scalar_field_byte_size(const Water2DConfig& config) {
    return checked_mul(cell_count(config), sizeof(float), "water scalar field is too large");
}

[[nodiscard]] inline std::size_t cell_uint_field_byte_size(const Water2DConfig& config) {
    return checked_mul(cell_count(config), sizeof(std::uint32_t),
                       "water uint cell field is too large");
}

[[nodiscard]] inline std::size_t u_face_byte_size(const Water2DConfig& config) {
    return checked_mul(u_face_count(config), sizeof(float), "water U-face field is too large");
}

[[nodiscard]] inline std::size_t v_face_byte_size(const Water2DConfig& config) {
    return checked_mul(v_face_count(config), sizeof(float), "water V-face field is too large");
}

[[nodiscard]] inline std::size_t particle_value_count(const Water2DConfig& config) {
    return checked_mul(static_cast<std::size_t>(config.particle_capacity), std::size_t{4},
                       "water particle vector field is too large");
}

[[nodiscard]] inline std::size_t particle_buffer_byte_size(const Water2DConfig& config) {
    return checked_mul(particle_value_count(config), sizeof(float),
                       "water particle buffer is too large");
}

[[nodiscard]] inline std::size_t particle_affine_buffer_byte_size(const Water2DConfig& config) {
    return particle_buffer_byte_size(config);
}

[[nodiscard]] inline std::size_t particle_bin_index_count(const Water2DConfig& config) {
    if (config.max_particles_per_cell == 0) {
        throw std::runtime_error("water max particles per cell must be positive");
    }
    validate_exact_shader_integer(
        config.max_particles_per_cell,
        "water max particles per cell exceeds exact shader integer range");
    return checked_mul(cell_count(config), static_cast<std::size_t>(config.max_particles_per_cell),
                       "water particle bins are too large");
}

[[nodiscard]] inline std::size_t particle_bin_index_byte_size(const Water2DConfig& config) {
    return checked_mul(particle_bin_index_count(config), sizeof(std::uint32_t),
                       "water particle bin index buffer is too large");
}

[[nodiscard]] inline std::size_t diagnostics_buffer_byte_size(const Water2DConfig&) {
    return checked_mul(kWater2DDiagnosticSlotCount, sizeof(std::uint32_t),
                       "water diagnostics buffer is too large");
}

[[nodiscard]] inline Water2DConfig
water_2d_config_from_options(const common::FluidGridOptions& grid,
                             const Water2DStartupOptions& options,
                             const host::CommonRunConfig& common_config) {
    Water2DConfig water_config;
    if (common_config.profile_diagnostics && !common_config.headless) {
        throw std::runtime_error("water 2D profile diagnostics require --headless");
    }
    water_config.profile_diagnostics = common_config.profile_diagnostics;
    water_config.profile_diagnostic_interval = common_config.profile_diagnostic_interval;
    if (grid.width) {
        water_config.grid_width = *grid.width;
    }
    if (grid.height) {
        water_config.grid_height = *grid.height;
    }
    if (options.transfer_mode) {
        water_config.transfer_mode = water_2d_transfer_mode_from_name(*options.transfer_mode);
    }
    if (options.transfer_limit) {
        water_config.max_particles_per_cell = *options.transfer_limit;
    }
    if (options.hose) {
        water_config.hose.enabled = *options.hose;
    }
    if (options.drain) {
        water_config.drain.enabled = *options.drain;
    }
    if (options.wave) {
        water_config.wave.enabled = *options.wave;
    }
    refresh_particle_counts(water_config);
    static_cast<void>(cell_count(water_config));
    static_cast<void>(u_face_count(water_config));
    static_cast<void>(v_face_count(water_config));
    static_cast<void>(particle_bin_index_count(water_config));
    static_cast<void>(diagnostics_buffer_byte_size(water_config));
    return water_config;
}

[[nodiscard]] inline std::uint32_t
water_2d_headless_frame_count(const host::CommonRunConfig& config) {
    if (config.frames == 0) {
        return 120;
    }
    return config.frames;
}

[[nodiscard]] inline FrameTiming fixed_water_2d_headless_timing(const Water2DConfig& config,
                                                                std::uint64_t frame_index) {
    if (frame_index == 0) {
        throw std::runtime_error("fixed water headless frame index must be positive");
    }
    return {
        .delta_seconds = config.fixed_delta_seconds,
        .elapsed_seconds =
            static_cast<double>(config.fixed_delta_seconds) * static_cast<double>(frame_index),
        .frame_index = frame_index,
    };
}

} // namespace cubey::projects::fluid::water_2d
