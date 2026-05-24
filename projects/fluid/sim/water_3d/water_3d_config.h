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
    SurfaceFoam = 10,
    Whitewater = 11,
};

enum class Water3DTransferMode : std::uint32_t {
    PicFlip = 0,
    Apic = 1,
};

enum class Water3DP2GMode : std::uint32_t {
    ActiveFaces = 0,
    TiledFaces = 1,
};

inline constexpr std::uint32_t kWater3DComputeGroupSize = 4;
inline constexpr std::uint32_t kWater3DParticleGroupSize = 64;
inline constexpr std::uint32_t kWater3DP2GTileExtent = 4;
inline constexpr std::uint32_t kWater3DP2GTileFaceSlots = 192;
inline constexpr std::uint32_t kWater3DSimulationPushConstantFloatCount = 4;
inline constexpr std::uint32_t kWater3DSimulationUniformFloatCount = 32;
inline constexpr std::uint32_t kWater3DRenderPushConstantFloatCount = 48;
inline constexpr std::uint32_t kWater3DWallCells = 2;
inline constexpr std::uint32_t kWater3DMinimumGridExtent = 16;
inline constexpr std::uint32_t kWater3DDefaultGridWidth = 64;
inline constexpr std::uint32_t kWater3DDefaultGridHeight = 64;
inline constexpr std::uint32_t kWater3DDefaultGridDepth = 64;
inline constexpr std::uint32_t kWater3DDefaultWhitewaterCapacity = 65536;
inline constexpr std::uint32_t kWater3DScanGroupSize = 256;
inline constexpr std::uint32_t kWater3DMaxExactShaderInteger = 1U << 24U;
inline constexpr std::uint32_t kWater3DDiagnosticSlotCount = 68;
inline constexpr std::uint32_t kWater3DDiagnosticDivergenceScale = 1000000;
inline constexpr float kWater3DMinFillFraction = 0.08F;
inline constexpr float kWater3DMaxFillFraction = 0.75F;

enum class Water3DDiagnosticSlot : std::uint32_t {
    ActiveParticles = 0,
    InactiveScanParticles = 1,
    OutOfBoundsParticles = 2,
    NonemptyCells = 3,
    OverpackedCells = 4,
    OverpackedParticles = 5,
    MaxCellCount = 6,
    ActiveFaces = 7,
    ActiveFaceDispatchGroups = 8,
    DivergenceAbsSumFixed = 9,
    DivergenceAbsMaxFixed = 10,
    DivergentCells = 11,
    WhitewaterEmitted = 12,
    WhitewaterActive = 13,
    WhitewaterCapacity = 14,
    ParticleScanCount = 15,
    DivergenceAbsSumFixedHigh = 16,
    P2GActiveFaces = 17,
    P2GFacesProcessed = 18,
    P2GBlockedFaces = 19,
    P2GUFacesProcessed = 20,
    P2GVFacesProcessed = 21,
    P2GWFacesProcessed = 22,
    P2GNeighborCellsTested = 23,
    P2GNeighborCellsInBounds = 24,
    P2GEmptyCellVisits = 25,
    P2GCellParticleSlotsScanned = 26,
    P2GCellParticleSlotsScannedHigh = 27,
    P2GInactiveParticlesSeen = 28,
    P2GWeightPositiveParticles = 29,
    P2GWeightPositiveParticlesHigh = 30,
    P2GWeightZeroParticles = 31,
    P2GWeightZeroParticlesHigh = 32,
    P2GZeroWeightFaces = 33,
    P2GMaxCellCountSeen = 34,
    P2GOverpackedCellVisits = 35,
    P2GApicParticleSamples = 36,
    P2GApicParticleSamplesHigh = 37,
    P2GInvalidFaceIds = 38,
    P2GCandidateSlotsBin0 = 39,
    P2GCandidateSlotsBin1To32 = 40,
    P2GCandidateSlotsBin33To64 = 41,
    P2GCandidateSlotsBin65To96 = 42,
    P2GCandidateSlotsBin97To128 = 43,
    P2GCandidateSlotsBin129To192 = 44,
    P2GCandidateSlotsBin193To384 = 45,
    P2GCandidateSlotsBin385Plus = 46,
    P2GPositiveCandidatesBin0 = 47,
    P2GPositiveCandidatesBin1To3 = 48,
    P2GPositiveCandidatesBin4To7 = 49,
    P2GPositiveCandidatesBin8To15 = 50,
    P2GPositiveCandidatesBin16To31 = 51,
    P2GPositiveCandidatesBin32To63 = 52,
    P2GPositiveCandidatesBin64To127 = 53,
    P2GPositiveCandidatesBin128Plus = 54,
    P2GOverpackedNeighborCellsBin0 = 55,
    P2GOverpackedNeighborCellsBin1 = 56,
    P2GOverpackedNeighborCellsBin2To3 = 57,
    P2GOverpackedNeighborCellsBin4To7 = 58,
    P2GOverpackedNeighborCellsBin8Plus = 59,
    P2GMaxCandidateSlotsPerFace = 60,
    P2GZeroWeightUFaces = 61,
    P2GZeroWeightVFaces = 62,
    P2GZeroWeightWFaces = 63,
    P2GActiveTiles = 64,
    P2GTileFaceSlots = 65,
    P2GTileInactiveFaceSlots = 66,
    P2GTileDispatchGroups = 67,
};

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
    Water3DP2GMode p2g_mode = Water3DP2GMode::ActiveFaces;
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
    float surface_thickness_scale = 0.65F;
    float surface_gap_fill_radius_px = 1.0F;
    float surface_smoothing_radius_world = 0.010F;
    std::uint32_t surface_smoothing_iterations = 2;
    float surface_depth_sigma = 0.025F;
    float surface_thickness_smoothing = 0.5F;
    float surface_absorption = 0.8F;
    float surface_refraction_strength = 0.025F;
    float foam_amount = 0.70F;
    float foam_sharpness = 2.2F;
    bool whitewater_enabled = true;
    std::uint32_t whitewater_capacity = kWater3DDefaultWhitewaterCapacity;
    std::uint32_t whitewater_max_emit_per_frame = 2048;
    float whitewater_intensity = 1.0F;
    float whitewater_speed_threshold = 1.1F;
    float whitewater_lifetime = 1.6F;
    float whitewater_radius = 0.010F;
    float whitewater_drag = 0.94F;
    float whitewater_gravity_scale = 0.55F;
    float environment_intensity = 1.0F;
    float environment_rotation_degrees = 0.0F;
    float exposure = 0.0F;
    std::uint32_t profile_diagnostic_interval = 1;
    bool profile_diagnostics = false;
};

struct Water3DSimulationUniforms {
    std::array<float, 4> grid_options{};
    std::array<float, 4> particle_options{};
    std::array<float, 4> fill_options{};
    std::array<float, 4> solve_options{};
    std::array<float, 4> lifecycle_options{};
    std::array<float, 4> render_options{};
    std::array<float, 4> whitewater_options{};
    std::array<float, 4> whitewater_lifecycle{};
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
    case Water3DRenderView::SurfaceFoam:
        return "Surface foam";
    case Water3DRenderView::Whitewater:
        return "Whitewater";
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

[[nodiscard]] inline const char* water_3d_p2g_mode_name(Water3DP2GMode mode) {
    switch (mode) {
    case Water3DP2GMode::ActiveFaces:
        return "active";
    case Water3DP2GMode::TiledFaces:
        return "tiled";
    }
    return "active";
}

[[nodiscard]] inline Water3DP2GMode water_3d_p2g_mode_from_name(std::string_view name) {
    if (name.empty() || name == "active" || name == "active-faces") {
        return Water3DP2GMode::ActiveFaces;
    }
    if (name == "tiled" || name == "tiled-faces") {
        return Water3DP2GMode::TiledFaces;
    }
    throw std::runtime_error("water 3D P2G mode must be active or tiled");
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
    if (name == "surface-foam") {
        return Water3DRenderView::SurfaceFoam;
    }
    if (name == "whitewater") {
        return Water3DRenderView::Whitewater;
    }
    throw std::runtime_error("water 3D render view must be surface, particles, cells, velocity, "
                             "pressure, solid, overpack, surface-depth, surface-thickness, or "
                             "surface-normals, surface-foam, or whitewater");
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
        return Water3DRenderView::SurfaceFoam;
    case Water3DRenderView::SurfaceFoam:
        return Water3DRenderView::Whitewater;
    case Water3DRenderView::Whitewater:
        return Water3DRenderView::Surface;
    }
    return Water3DRenderView::Surface;
}

[[nodiscard]] inline bool is_water_3d_surface_view(Water3DRenderView view) {
    return view == Water3DRenderView::Surface || view == Water3DRenderView::SurfaceDepth ||
           view == Water3DRenderView::SurfaceThickness ||
           view == Water3DRenderView::SurfaceNormals || view == Water3DRenderView::SurfaceFoam ||
           view == Water3DRenderView::Whitewater;
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

inline void validate_water_3d_whitewater_capacity(const Water3DConfig& config) {
    if (config.whitewater_capacity == 0) {
        throw std::runtime_error("water 3D whitewater capacity must be positive");
    }
    validate_exact_shader_integer(
        config.whitewater_capacity,
        "water 3D whitewater capacity exceeds exact shader integer range");
    validate_exact_shader_integer(
        config.whitewater_max_emit_per_frame,
        "water 3D whitewater max emit count exceeds exact shader integer range");
}

inline void validate_water_3d_particle_limits(const Water3DConfig& config) {
    if (config.max_particles_per_cell == 0) {
        throw std::runtime_error("water 3D max particles per cell must be positive");
    }
    validate_exact_shader_integer(
        config.max_particles_per_cell,
        "water 3D max particles per cell exceeds exact shader integer range");
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

[[nodiscard]] inline std::size_t total_face_count(const Water3DConfig& config) {
    const std::size_t uv_faces = checked_add(u_face_count(config), v_face_count(config),
                                             "water 3D total face count is too large");
    const std::size_t count =
        checked_add(uv_faces, w_face_count(config), "water 3D total face count is too large");
    validate_exact_shader_integer(count,
                                  "water 3D total face count exceeds exact shader integer range");
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

[[nodiscard]] inline std::size_t water_3d_scan_block_count(std::size_t element_count) {
    const std::size_t adjusted =
        checked_add(element_count, static_cast<std::size_t>(kWater3DScanGroupSize - 1U),
                    "water 3D scan element count is too large");
    const std::size_t count = adjusted / kWater3DScanGroupSize;
    validate_exact_shader_integer(count, "water 3D scan block count exceeds exact shader range");
    return count;
}

[[nodiscard]] inline std::size_t particle_sort_scan_level0_count(const Water3DConfig& config) {
    return water_3d_scan_block_count(cell_count(config));
}

[[nodiscard]] inline std::size_t particle_sort_scan_level1_count(const Water3DConfig& config) {
    return water_3d_scan_block_count(particle_sort_scan_level0_count(config));
}

[[nodiscard]] inline std::size_t particle_sort_scan_level2_count(const Water3DConfig& config) {
    return water_3d_scan_block_count(particle_sort_scan_level1_count(config));
}

[[nodiscard]] inline std::size_t particle_sort_scan_level0_byte_size(const Water3DConfig& config) {
    return checked_mul(particle_sort_scan_level0_count(config), sizeof(std::uint32_t),
                       "water 3D particle sort scan level 0 buffer is too large");
}

[[nodiscard]] inline std::size_t particle_sort_scan_level1_byte_size(const Water3DConfig& config) {
    return checked_mul(particle_sort_scan_level1_count(config), sizeof(std::uint32_t),
                       "water 3D particle sort scan level 1 buffer is too large");
}

[[nodiscard]] inline std::size_t particle_sort_scan_level2_byte_size(const Water3DConfig& config) {
    return checked_mul(particle_sort_scan_level2_count(config), sizeof(std::uint32_t),
                       "water 3D particle sort scan level 2 buffer is too large");
}

[[nodiscard]] inline std::size_t active_work_count_byte_size(const Water3DConfig& config) {
    static_cast<void>(config);
    return sizeof(std::uint32_t) * 4U;
}

[[nodiscard]] inline std::size_t active_face_flag_byte_size(const Water3DConfig& config) {
    return checked_mul(total_face_count(config), sizeof(std::uint32_t),
                       "water 3D active face flag buffer is too large");
}

[[nodiscard]] inline std::size_t active_face_index_byte_size(const Water3DConfig& config) {
    return checked_mul(total_face_count(config), sizeof(std::uint32_t),
                       "water 3D active face index buffer is too large");
}

[[nodiscard]] inline std::size_t active_face_dispatch_arg_byte_size(const Water3DConfig& config) {
    static_cast<void>(config);
    return sizeof(std::uint32_t) * 3U;
}

[[nodiscard]] inline std::uint32_t water_3d_p2g_tile_axis_count(std::uint32_t cell_count) {
    return (cell_count + kWater3DP2GTileExtent - 1U) / kWater3DP2GTileExtent;
}

[[nodiscard]] inline std::uint32_t water_3d_p2g_tile_count_x(const Water3DConfig& config) {
    validate_water_3d_grid_dimensions(config);
    return water_3d_p2g_tile_axis_count(config.grid_width);
}

[[nodiscard]] inline std::uint32_t water_3d_p2g_tile_count_y(const Water3DConfig& config) {
    validate_water_3d_grid_dimensions(config);
    return water_3d_p2g_tile_axis_count(config.grid_height);
}

[[nodiscard]] inline std::uint32_t water_3d_p2g_tile_count_z(const Water3DConfig& config) {
    validate_water_3d_grid_dimensions(config);
    return water_3d_p2g_tile_axis_count(config.grid_depth);
}

[[nodiscard]] inline std::size_t water_3d_p2g_tile_count(const Water3DConfig& config) {
    const std::size_t xy = checked_mul(water_3d_p2g_tile_count_x(config),
                                       water_3d_p2g_tile_count_y(config),
                                       "water 3D P2G tile slice is too large");
    const std::size_t count = checked_mul(xy, water_3d_p2g_tile_count_z(config),
                                          "water 3D P2G tile grid is too large");
    validate_exact_shader_integer(count, "water 3D P2G tile count exceeds exact shader range");
    return count;
}

[[nodiscard]] inline std::size_t active_tile_flag_byte_size(const Water3DConfig& config) {
    return checked_mul(water_3d_p2g_tile_count(config), sizeof(std::uint32_t),
                       "water 3D active tile flag buffer is too large");
}

[[nodiscard]] inline std::size_t active_tile_index_byte_size(const Water3DConfig& config) {
    return checked_mul(water_3d_p2g_tile_count(config), sizeof(std::uint32_t),
                       "water 3D active tile index buffer is too large");
}

[[nodiscard]] inline std::size_t active_tile_dispatch_arg_byte_size(const Water3DConfig& config) {
    static_cast<void>(config);
    return sizeof(std::uint32_t) * 3U;
}

[[nodiscard]] inline std::size_t whitewater_value_count(const Water3DConfig& config) {
    validate_water_3d_whitewater_capacity(config);
    return checked_mul(static_cast<std::size_t>(config.whitewater_capacity), std::size_t{4},
                       "water 3D whitewater vector field is too large");
}

[[nodiscard]] inline std::size_t whitewater_buffer_byte_size(const Water3DConfig& config) {
    return checked_mul(whitewater_value_count(config), sizeof(float),
                       "water 3D whitewater buffer is too large");
}

[[nodiscard]] inline std::size_t whitewater_counter_byte_size(const Water3DConfig& config) {
    validate_water_3d_whitewater_capacity(config);
    return sizeof(std::uint32_t) * 4U;
}

[[nodiscard]] inline std::size_t whitewater_active_index_byte_size(const Water3DConfig& config) {
    validate_water_3d_whitewater_capacity(config);
    return checked_mul(static_cast<std::size_t>(config.whitewater_capacity), sizeof(std::uint32_t),
                       "water 3D whitewater active index buffer is too large");
}

[[nodiscard]] inline std::size_t whitewater_draw_arg_byte_size(const Water3DConfig& config) {
    validate_water_3d_whitewater_capacity(config);
    return sizeof(std::uint32_t) * 4U;
}

[[nodiscard]] inline std::size_t diagnostics_buffer_byte_size(const Water3DConfig& config) {
    static_cast<void>(config);
    return sizeof(std::uint32_t) * kWater3DDiagnosticSlotCount;
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
    water_config.environment_intensity = config.ibl_intensity;
    water_config.environment_rotation_degrees = config.environment_rotation_degrees;
    water_config.exposure = config.exposure;
    water_config.profile_diagnostics = config.profile_diagnostics;
    water_config.profile_diagnostic_interval = config.profile_diagnostic_interval;
    if (!config.water3d_p2g_mode.empty()) {
        water_config.p2g_mode = water_3d_p2g_mode_from_name(config.water3d_p2g_mode);
    }
    refresh_particle_counts(water_config);
    static_cast<void>(cell_count(water_config));
    static_cast<void>(u_face_count(water_config));
    static_cast<void>(v_face_count(water_config));
    static_cast<void>(w_face_count(water_config));
    static_cast<void>(total_face_count(water_config));
    validate_water_3d_particle_limits(water_config);
    static_cast<void>(particle_sort_scan_level0_count(water_config));
    static_cast<void>(particle_sort_scan_level1_count(water_config));
    static_cast<void>(particle_sort_scan_level2_count(water_config));
    static_cast<void>(active_work_count_byte_size(water_config));
    static_cast<void>(active_face_flag_byte_size(water_config));
    static_cast<void>(active_face_index_byte_size(water_config));
    static_cast<void>(active_face_dispatch_arg_byte_size(water_config));
    static_cast<void>(active_tile_flag_byte_size(water_config));
    static_cast<void>(active_tile_index_byte_size(water_config));
    static_cast<void>(active_tile_dispatch_arg_byte_size(water_config));
    static_cast<void>(whitewater_buffer_byte_size(water_config));
    static_cast<void>(whitewater_counter_byte_size(water_config));
    static_cast<void>(whitewater_active_index_byte_size(water_config));
    static_cast<void>(whitewater_draw_arg_byte_size(water_config));
    static_cast<void>(diagnostics_buffer_byte_size(water_config));
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
