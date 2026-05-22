#pragma once

#include <cubey/core/frame_clock.h>
#include <cubey/core/run_config.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string_view>

namespace cubey::projects::fluid::water_3d {

enum class Water3DRenderView : std::uint32_t {
    Surface = 0,
    Particles = 1,
    Cells = 2,
    Velocity = 3,
    Pressure = 4,
    Solid = 5,
    Overpack = 6,
    SurfaceDepth = 7,
    SurfaceThickness = 8,
    SurfaceNormals = 9,
};

enum class Water3DTransferMode : std::uint32_t {
    PicFlip = 0,
    Apic = 1,
};

inline constexpr std::uint32_t kWater3DComputeGroupSize = 4;
inline constexpr std::uint32_t kWater3DParticleGroupSize = 64;
inline constexpr std::uint32_t kWater3DSimulationPushConstantFloatCount = 4;
inline constexpr std::uint32_t kWater3DSimulationUniformFloatCount = 32;
inline constexpr std::uint32_t kWater3DRenderPushConstantFloatCount = 48;
inline constexpr std::uint32_t kWater3DWallCells = 2;
inline constexpr std::uint32_t kWater3DMinimumGridExtent = 16;
inline constexpr std::uint32_t kWater3DDefaultGridWidth = 64;
inline constexpr std::uint32_t kWater3DDefaultGridHeight = 64;
inline constexpr std::uint32_t kWater3DDefaultGridDepth = 64;
inline constexpr std::uint32_t kWater3DMaxExactShaderInteger = 1U << 24U;
inline constexpr float kWater3DMinFillFraction = 0.08F;
inline constexpr float kWater3DMaxFillFraction = 0.75F;

struct Water3DConfig {
    std::uint32_t grid_width = kWater3DDefaultGridWidth;
    std::uint32_t grid_height = kWater3DDefaultGridHeight;
    std::uint32_t grid_depth = kWater3DDefaultGridDepth;
    std::uint32_t pressure_iterations = 32;
    std::uint32_t particles_per_cell = 4;
    std::uint32_t max_particles_per_cell = 32;
    std::uint32_t active_particle_count = 180224;
    std::uint32_t particle_capacity = 442368;
    std::uint32_t substeps = 1;
    Water3DTransferMode transfer_mode = Water3DTransferMode::Apic;
    float fixed_delta_seconds = 1.0F / 60.0F;
    float gravity = -1.35F;
    float flip_ratio = 0.78F;
    float particle_radius = 0.018F;
    float initial_fill_width = 0.50F;
    float initial_fill_height = 0.70F;
    float initial_fill_depth = 0.50F;
    float velocity_limit = 3.0F;
    float particle_damping = 0.998F;
    float particle_volume_strength = 12.0F;
    float boundary_restitution = 0.08F;
    float slice_depth = 0.50F;
    float surface_thickness_scale = 1.0F;
    float surface_smoothing_radius_px = 5.0F;
    float surface_depth_sigma = 0.035F;
    float surface_absorption = 1.5F;
    float surface_refraction_strength = 0.035F;
};

struct Water3DSimulationUniforms {
    std::array<float, 4> grid_options{};
    std::array<float, 4> particle_options{};
    std::array<float, 4> fill_options{};
    std::array<float, 4> solve_options{};
    std::array<float, 4> lifecycle_options{};
    std::array<float, 4> render_options{};
    std::array<float, 4> reserved0{};
    std::array<float, 4> reserved1{};
};

struct Water3DDispatchPushConstants {
    std::array<float, 4> dispatch_options{};
};

struct Water3DRuntimeState {
    std::uint32_t particle_scan_count = 0;
    bool pressure_read_b = false;
};

struct Water3DAppInfo {
    const char* app_name = "water_3d";
    const char* ready_status = "rendering 3D water project";
    const char* ui_title = "Water 3D";
};

static_assert(sizeof(Water3DSimulationUniforms) ==
              sizeof(float) * kWater3DSimulationUniformFloatCount);
static_assert(sizeof(Water3DDispatchPushConstants) ==
              sizeof(float) * kWater3DSimulationPushConstantFloatCount);

[[nodiscard]] inline const char* water_3d_render_view_name(Water3DRenderView view) {
    switch (view) {
    case Water3DRenderView::Surface:
        return "Surface";
    case Water3DRenderView::Particles:
        return "Particles";
    case Water3DRenderView::Cells:
        return "Cells";
    case Water3DRenderView::Velocity:
        return "Velocity";
    case Water3DRenderView::Pressure:
        return "Pressure";
    case Water3DRenderView::Solid:
        return "Solid";
    case Water3DRenderView::Overpack:
        return "Overpack";
    case Water3DRenderView::SurfaceDepth:
        return "Surface depth";
    case Water3DRenderView::SurfaceThickness:
        return "Surface thickness";
    case Water3DRenderView::SurfaceNormals:
        return "Surface normals";
    }
    return "Surface";
}

[[nodiscard]] inline const char* water_3d_transfer_mode_name(Water3DTransferMode mode) {
    switch (mode) {
    case Water3DTransferMode::PicFlip:
        return "PIC/FLIP";
    case Water3DTransferMode::Apic:
        return "APIC";
    }
    return "APIC";
}

[[nodiscard]] inline Water3DRenderView water_3d_render_view_from_name(std::string_view name) {
    if (name.empty() || name == "surface") {
        return Water3DRenderView::Surface;
    }
    if (name == "particles") {
        return Water3DRenderView::Particles;
    }
    if (name == "cells") {
        return Water3DRenderView::Cells;
    }
    if (name == "velocity") {
        return Water3DRenderView::Velocity;
    }
    if (name == "pressure") {
        return Water3DRenderView::Pressure;
    }
    if (name == "solid") {
        return Water3DRenderView::Solid;
    }
    if (name == "overpack") {
        return Water3DRenderView::Overpack;
    }
    if (name == "surface-depth") {
        return Water3DRenderView::SurfaceDepth;
    }
    if (name == "surface-thickness") {
        return Water3DRenderView::SurfaceThickness;
    }
    if (name == "surface-normals") {
        return Water3DRenderView::SurfaceNormals;
    }
    throw std::runtime_error("water 3D render view must be surface, particles, cells, velocity, "
                             "pressure, solid, overpack, surface-depth, surface-thickness, or "
                             "surface-normals");
}

[[nodiscard]] inline Water3DRenderView next_render_view(Water3DRenderView view) {
    switch (view) {
    case Water3DRenderView::Surface:
        return Water3DRenderView::Particles;
    case Water3DRenderView::Particles:
        return Water3DRenderView::Cells;
    case Water3DRenderView::Cells:
        return Water3DRenderView::Velocity;
    case Water3DRenderView::Velocity:
        return Water3DRenderView::Pressure;
    case Water3DRenderView::Pressure:
        return Water3DRenderView::Solid;
    case Water3DRenderView::Solid:
        return Water3DRenderView::Overpack;
    case Water3DRenderView::Overpack:
        return Water3DRenderView::SurfaceDepth;
    case Water3DRenderView::SurfaceDepth:
        return Water3DRenderView::SurfaceThickness;
    case Water3DRenderView::SurfaceThickness:
        return Water3DRenderView::SurfaceNormals;
    case Water3DRenderView::SurfaceNormals:
        return Water3DRenderView::Surface;
    }
    return Water3DRenderView::Surface;
}

[[nodiscard]] inline bool is_water_3d_surface_view(Water3DRenderView view) {
    return view == Water3DRenderView::Surface || view == Water3DRenderView::SurfaceDepth ||
           view == Water3DRenderView::SurfaceThickness || view == Water3DRenderView::SurfaceNormals;
}

[[nodiscard]] inline std::size_t checked_mul(std::size_t lhs, std::size_t rhs,
                                             const char* message) {
    if (lhs != 0 && rhs > std::numeric_limits<std::size_t>::max() / lhs) {
        throw std::runtime_error(message);
    }
    return lhs * rhs;
}

inline void validate_exact_shader_integer(std::size_t value, const char* message) {
    if (value > kWater3DMaxExactShaderInteger) {
        throw std::runtime_error(message);
    }
}

[[nodiscard]] inline float water_3d_shader_count_float(std::size_t value, const char* message) {
    validate_exact_shader_integer(value, message);
    return static_cast<float>(value);
}

inline void validate_water_3d_grid_dimensions(const Water3DConfig& config) {
    if (config.grid_width < kWater3DMinimumGridExtent ||
        config.grid_height < kWater3DMinimumGridExtent ||
        config.grid_depth < kWater3DMinimumGridExtent) {
        throw std::runtime_error("water 3D grid dimensions must be at least 16 cells");
    }
    validate_exact_shader_integer(config.grid_width,
                                  "water 3D grid width exceeds exact shader integer range");
    validate_exact_shader_integer(config.grid_height,
                                  "water 3D grid height exceeds exact shader integer range");
    validate_exact_shader_integer(config.grid_depth,
                                  "water 3D grid depth exceeds exact shader integer range");
}

[[nodiscard]] inline std::uint32_t water_3d_fill_axis_cell_count(std::uint32_t axis_cells,
                                                                 float fill_fraction) {
    const std::uint32_t usable_cells = axis_cells - (kWater3DWallCells * 2U);
    const float clamped_fraction =
        std::clamp(fill_fraction, kWater3DMinFillFraction, kWater3DMaxFillFraction);
    const auto raw_fill_cells =
        static_cast<std::uint32_t>(std::floor(static_cast<float>(axis_cells) * clamped_fraction));
    return std::clamp(raw_fill_cells, 1U, usable_cells);
}

[[nodiscard]] inline std::size_t cell_count(const Water3DConfig& config) {
    validate_water_3d_grid_dimensions(config);
    const std::size_t slice =
        checked_mul(config.grid_width, config.grid_height, "water 3D grid slice is too large");
    const std::size_t count = checked_mul(slice, config.grid_depth, "water 3D grid is too large");
    validate_exact_shader_integer(count, "water 3D cell count exceeds exact shader integer range");
    return count;
}

[[nodiscard]] inline std::size_t u_face_count(const Water3DConfig& config) {
    validate_water_3d_grid_dimensions(config);
    const std::size_t slice = checked_mul(static_cast<std::size_t>(config.grid_width) + 1U,
                                          config.grid_height, "water 3D U-face slice is too large");
    const std::size_t count =
        checked_mul(slice, config.grid_depth, "water 3D U-face grid is too large");
    validate_exact_shader_integer(count,
                                  "water 3D U-face count exceeds exact shader integer range");
    return count;
}

[[nodiscard]] inline std::size_t v_face_count(const Water3DConfig& config) {
    validate_water_3d_grid_dimensions(config);
    const std::size_t slice =
        checked_mul(config.grid_width, static_cast<std::size_t>(config.grid_height) + 1U,
                    "water 3D V-face slice is too large");
    const std::size_t count =
        checked_mul(slice, config.grid_depth, "water 3D V-face grid is too large");
    validate_exact_shader_integer(count,
                                  "water 3D V-face count exceeds exact shader integer range");
    return count;
}

[[nodiscard]] inline std::size_t w_face_count(const Water3DConfig& config) {
    validate_water_3d_grid_dimensions(config);
    const std::size_t slice =
        checked_mul(config.grid_width, config.grid_height, "water 3D W-face slice is too large");
    const std::size_t count = checked_mul(slice, static_cast<std::size_t>(config.grid_depth) + 1U,
                                          "water 3D W-face grid is too large");
    validate_exact_shader_integer(count,
                                  "water 3D W-face count exceeds exact shader integer range");
    return count;
}

[[nodiscard]] inline std::size_t fill_cell_count(const Water3DConfig& config, float fill_width,
                                                 float fill_height, float fill_depth) {
    validate_water_3d_grid_dimensions(config);
    const std::size_t fill_x = water_3d_fill_axis_cell_count(config.grid_width, fill_width);
    const std::size_t fill_y = water_3d_fill_axis_cell_count(config.grid_height, fill_height);
    const std::size_t fill_z = water_3d_fill_axis_cell_count(config.grid_depth, fill_depth);
    return checked_mul(checked_mul(fill_x, fill_y, "water 3D fill slice is too large"), fill_z,
                       "water 3D fill volume is too large");
}

[[nodiscard]] inline std::uint32_t particle_count_for_fill(const Water3DConfig& config,
                                                           float fill_width, float fill_height,
                                                           float fill_depth) {
    if (config.particles_per_cell == 0) {
        throw std::runtime_error("water 3D particles-per-cell must be positive");
    }
    const std::size_t count =
        checked_mul(fill_cell_count(config, fill_width, fill_height, fill_depth),
                    config.particles_per_cell, "water 3D particle count is too large");
    validate_exact_shader_integer(count,
                                  "water 3D particle count exceeds exact shader integer range");
    return static_cast<std::uint32_t>(count);
}

[[nodiscard]] inline std::uint32_t active_particle_count_for_fill(const Water3DConfig& config) {
    return particle_count_for_fill(config, config.initial_fill_width, config.initial_fill_height,
                                   config.initial_fill_depth);
}

[[nodiscard]] inline std::uint32_t particle_capacity_for_config(const Water3DConfig& config) {
    return particle_count_for_fill(config, kWater3DMaxFillFraction, kWater3DMaxFillFraction,
                                   kWater3DMaxFillFraction);
}

inline void refresh_particle_counts(Water3DConfig& config) {
    config.active_particle_count = active_particle_count_for_fill(config);
    config.particle_capacity = particle_capacity_for_config(config);
    if (config.active_particle_count > config.particle_capacity) {
        throw std::runtime_error("water 3D active particles exceed particle capacity");
    }
}

[[nodiscard]] inline std::uint32_t
water_3d_runtime_particle_scan_count(const Water3DConfig& config,
                                     const Water3DRuntimeState& state) {
    const std::uint32_t scan_count =
        state.particle_scan_count == 0 ? config.active_particle_count : state.particle_scan_count;
    return std::clamp(scan_count, config.active_particle_count, config.particle_capacity);
}

[[nodiscard]] inline std::size_t scalar_field_byte_size(const Water3DConfig& config) {
    return checked_mul(cell_count(config), sizeof(float), "water 3D scalar field is too large");
}

[[nodiscard]] inline std::size_t cell_uint_field_byte_size(const Water3DConfig& config) {
    return checked_mul(cell_count(config), sizeof(std::uint32_t),
                       "water 3D uint cell field is too large");
}

[[nodiscard]] inline std::size_t u_face_byte_size(const Water3DConfig& config) {
    return checked_mul(u_face_count(config), sizeof(float), "water 3D U-face field is too large");
}

[[nodiscard]] inline std::size_t v_face_byte_size(const Water3DConfig& config) {
    return checked_mul(v_face_count(config), sizeof(float), "water 3D V-face field is too large");
}

[[nodiscard]] inline std::size_t w_face_byte_size(const Water3DConfig& config) {
    return checked_mul(w_face_count(config), sizeof(float), "water 3D W-face field is too large");
}

[[nodiscard]] inline std::size_t particle_value_count(const Water3DConfig& config) {
    return checked_mul(static_cast<std::size_t>(config.particle_capacity), std::size_t{4},
                       "water 3D particle vector field is too large");
}

[[nodiscard]] inline std::size_t particle_buffer_byte_size(const Water3DConfig& config) {
    return checked_mul(particle_value_count(config), sizeof(float),
                       "water 3D particle buffer is too large");
}

[[nodiscard]] inline std::size_t particle_affine_value_count(const Water3DConfig& config) {
    return checked_mul(static_cast<std::size_t>(config.particle_capacity), std::size_t{12},
                       "water 3D APIC affine field is too large");
}

[[nodiscard]] inline std::size_t particle_affine_buffer_byte_size(const Water3DConfig& config) {
    return checked_mul(particle_affine_value_count(config), sizeof(float),
                       "water 3D APIC affine buffer is too large");
}

[[nodiscard]] inline std::size_t particle_bin_index_count(const Water3DConfig& config) {
    if (config.max_particles_per_cell == 0) {
        throw std::runtime_error("water 3D max particles per cell must be positive");
    }
    validate_exact_shader_integer(
        config.max_particles_per_cell,
        "water 3D max particles per cell exceeds exact shader integer range");
    return checked_mul(cell_count(config), config.max_particles_per_cell,
                       "water 3D particle bins are too large");
}

[[nodiscard]] inline std::size_t particle_bin_index_byte_size(const Water3DConfig& config) {
    return checked_mul(particle_bin_index_count(config), sizeof(std::uint32_t),
                       "water 3D particle bin index buffer is too large");
}

[[nodiscard]] inline Water3DConfig water_3d_config_from_run_config(const RunConfig& config) {
    Water3DConfig water_config;
    if (config.grid_width != 0) {
        water_config.grid_width = config.grid_width;
    }
    if (config.grid_height != 0) {
        water_config.grid_height = config.grid_height;
    }
    if (config.grid_depth != 0) {
        water_config.grid_depth = config.grid_depth;
    }
    refresh_particle_counts(water_config);
    static_cast<void>(cell_count(water_config));
    static_cast<void>(u_face_count(water_config));
    static_cast<void>(v_face_count(water_config));
    static_cast<void>(w_face_count(water_config));
    static_cast<void>(particle_bin_index_count(water_config));
    return water_config;
}

[[nodiscard]] inline std::uint32_t water_3d_headless_frame_count(const RunConfig& config) {
    return config.frames == 0 ? 120U : config.frames;
}

[[nodiscard]] inline FrameTiming fixed_water_3d_headless_timing(const Water3DConfig& config,
                                                                std::uint64_t frame_index) {
    if (frame_index == 0) {
        throw std::runtime_error("fixed water 3D frame index must be positive");
    }
    return {
        .delta_seconds = config.fixed_delta_seconds,
        .elapsed_seconds =
            static_cast<double>(config.fixed_delta_seconds) * static_cast<double>(frame_index),
        .frame_index = frame_index,
    };
}

} // namespace cubey::projects::fluid::water_3d
