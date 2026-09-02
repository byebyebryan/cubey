#pragma once

#include "../common/fluid_config_schema.h"
#include "../common/water_common.h"

#include <cubey/engine/pbr_environment_schema.h>
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

using Water3DTransferMode = common::WaterTransferMode;

enum class Water3DP2GMode : std::uint32_t {
    ActiveFaces = 0,
    TiledFaces = 1,
};

inline constexpr std::uint32_t kWater3DComputeGroupSize = 4;
inline constexpr std::uint32_t kWater3DParticleGroupSize = 64;
inline constexpr std::uint32_t kWater3DP2GTileExtent = 4;
inline constexpr std::uint32_t kWater3DP2GTileFaceSlots = 192;
inline constexpr std::uint32_t kWater3DSimulationPushConstantFloatCount = 8;
inline constexpr std::uint32_t kWater3DSimulationUniformFloatCount = 88;
inline constexpr std::uint32_t kWater3DRenderPushConstantFloatCount = 56;
inline constexpr float kWater3DSurfaceDefaultParticleMaxRadiusPx = 12.0F;
inline constexpr float kWater3DSurfaceMaxParticleRadiusPx = 48.0F;
inline constexpr float kWater3DSurfaceMaxSmoothRadiusPx = 24.0F;
inline constexpr float kWater3DWhitewaterMaxBlurPx = 12.0F;
inline constexpr std::uint32_t kWater3DWallCells = 2;
inline constexpr std::uint32_t kWater3DMinimumGridExtent = 16;
inline constexpr std::uint32_t kWater3DDefaultGridWidth = 128;
inline constexpr std::uint32_t kWater3DDefaultGridHeight = 64;
inline constexpr std::uint32_t kWater3DDefaultGridDepth = 48;
inline constexpr std::uint32_t kWater3DDefaultEmitterParticleCapacity = 65536;
inline constexpr std::uint32_t kWater3DDefaultWhitewaterCapacity = 65536;
inline constexpr std::uint32_t kWater3DScanGroupSize = 256;
inline constexpr std::uint32_t kWater3DMaxExactShaderInteger =
    common::kWaterMaxExactShaderInteger;
inline constexpr std::uint32_t kWater3DDiagnosticSlotCount = 71;
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
    LiquidParticles = 68,
    RainParticles = 69,
    TransferTruncatedParticles = 70,
};

struct Water3DDomainConfig {
    std::array<float, 3> scale{2.4F, 1.0F, 0.7F};
};

struct Water3DHoseConfig {
    bool enabled = false;
    std::array<float, 3> position{0.08F, 0.76F, 0.50F};
    float yaw_degrees = 0.0F;
    float pitch_degrees = -18.0F;
    float speed = 2.4F;
    float radius = 0.035F;
    float particles_per_second = 18000.0F;
    float spread_degrees = 14.0F;
    std::uint32_t particle_capacity = kWater3DDefaultEmitterParticleCapacity;
};

struct Water3DDrainConfig {
    bool enabled = false;
    std::array<float, 3> center{0.93F, 0.07F, 0.50F};
    std::array<float, 3> half_size{0.045F, 0.035F, 0.28F};
    float pull_speed = 2.2F;
    float pull_radius = 1.10F;
};

struct Water3DWaveConfig {
    bool enabled = true;
    std::array<float, 3> center{0.10F, 0.18F, 0.50F};
    std::array<float, 3> half_size{0.09F, 0.24F, 0.36F};
    float amplitude = 1.25F;
    float frequency_hz = 0.55F;
};

struct Water3DRainConfig {
    bool enabled = false;
    std::array<float, 3> center{0.55F, 0.96F, 0.50F};
    std::array<float, 3> half_size{0.45F, 0.01F, 0.30F};
    float speed = 2.0F;
    float radius = 0.020F;
    float particles_per_second = 8000.0F;
    float spread_degrees = 5.0F;
};

struct Water3DConfig {
    std::uint32_t grid_width = kWater3DDefaultGridWidth;
    std::uint32_t grid_height = kWater3DDefaultGridHeight;
    std::uint32_t grid_depth = kWater3DDefaultGridDepth;
    std::uint32_t pressure_iterations = 32;
    std::uint32_t particles_per_cell = 4;
    std::uint32_t max_particles_per_cell = 128;
    std::uint32_t active_particle_count = 213440;
    std::uint32_t initial_particle_capacity = 663552;
    std::uint32_t particle_capacity = 729088;
    std::uint32_t substeps = 1;
    Water3DTransferMode transfer_mode = Water3DTransferMode::Apic;
    Water3DP2GMode p2g_mode = Water3DP2GMode::ActiveFaces;
    float fixed_delta_seconds = 1.0F / 60.0F;
    float gravity = -1.35F;
    float flip_ratio = 0.78F;
    float particle_radius = 0.018F;
    float initial_fill_width = 0.32F;
    float initial_fill_height = 0.72F;
    float initial_fill_depth = 0.62F;
    std::array<float, 2> initial_fill_center{0.24F, 0.50F};
    float velocity_limit = 3.0F;
    float particle_damping = 0.998F;
    float particle_volume_strength = 12.0F;
    float boundary_restitution = 0.08F;
    float slice_depth = 0.50F;
    float surface_thickness_scale = 0.65F;
    float surface_particle_max_radius_px = kWater3DSurfaceDefaultParticleMaxRadiusPx;
    float surface_gap_fill_radius_px = 1.0F;
    float surface_smoothing_radius_world = 0.010F;
    float surface_smoothing_max_radius_px = 8.0F;
    std::uint32_t surface_smoothing_iterations = 2;
    float surface_depth_sigma = 0.025F;
    float surface_thickness_smoothing = 0.5F;
    float surface_absorption = 0.8F;
    float surface_refraction_strength = 0.025F;
    float foam_amount = 0.58F;
    float foam_sharpness = 1.7F;
    bool whitewater_enabled = true;
    std::uint32_t whitewater_capacity = kWater3DDefaultWhitewaterCapacity;
    std::uint32_t whitewater_max_emit_per_frame = 4096;
    float whitewater_intensity = 1.15F;
    float whitewater_speed_threshold = 0.95F;
    float whitewater_lifetime = 2.0F;
    float whitewater_radius = 0.007F;
    float whitewater_blur_radius_px = 0.0F;
    float whitewater_drag = 0.91F;
    float whitewater_gravity_scale = 0.65F;
    float environment_intensity = 1.0F;
    float environment_rotation_degrees = 0.0F;
    float exposure = 0.0F;
    Water3DDomainConfig domain{};
    Water3DHoseConfig hose{};
    Water3DDrainConfig drain{};
    Water3DWaveConfig wave{};
    Water3DRainConfig rain{};
    std::uint32_t profile_diagnostic_interval = 1;
    bool profile_diagnostics = false;
};

// Startup-only overrides retain the distinction between an omitted option and
// an explicit value.  The concrete Water3DConfig below remains the runtime
// product with its established defaults.
struct Water3DStartupOptions {
    std::optional<std::string> transfer_mode{};
    std::optional<std::uint32_t> transfer_limit{};
    std::optional<std::string> p2g_mode{};
    std::optional<float> initial_fill_width{};
    std::optional<float> initial_fill_height{};
    std::optional<float> initial_fill_depth{};
    std::optional<float> wave_amplitude{};
    std::optional<float> wave_frequency_hz{};
    std::optional<float> whitewater_intensity{};
    std::optional<float> whitewater_speed_threshold{};
    std::optional<bool> hose{};
    std::optional<bool> drain{};
    std::optional<bool> rain{};
    std::optional<bool> wave{};
    std::optional<bool> whitewater{};
};

struct Water3DSimulationUniforms {
    std::array<float, 4> grid_options{};
    std::array<float, 4> particle_options{};
    std::array<float, 4> fill_options{};
    std::array<float, 4> fill_placement_options{};
    std::array<float, 4> solve_options{};
    std::array<float, 4> lifecycle_options{};
    std::array<float, 4> render_options{};
    std::array<float, 4> whitewater_options{};
    std::array<float, 4> whitewater_lifecycle{};
    std::array<float, 4> emitter_lifecycle{};
    std::array<float, 4> hose_options0{};
    std::array<float, 4> hose_options1{};
    std::array<float, 4> hose_options2{};
    std::array<float, 4> drain_options{};
    std::array<float, 4> drain_extents{};
    std::array<float, 4> drain_flow{};
    std::array<float, 4> wave_options0{};
    std::array<float, 4> wave_options1{};
    std::array<float, 4> wave_options2{};
    std::array<float, 4> rain_options0{};
    std::array<float, 4> rain_options1{};
    std::array<float, 4> rain_options2{};
};

struct Water3DDispatchPushConstants {
    std::array<float, 4> dispatch_options{};
    std::array<float, 4> emit_options{};
};

struct Water3DRuntimeState {
    std::uint32_t emitter_cursor = 0;
    std::uint32_t particle_scan_count = 0;
    float hose_emit_accumulator = 0.0F;
    float rain_emit_accumulator = 0.0F;
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
    return common::water_transfer_mode_name(mode);
}

[[nodiscard]] inline Water3DTransferMode water_3d_transfer_mode_from_name(std::string_view name) {
    return common::water_transfer_mode_from_name(
        name, "water 3D transfer mode must be apic or pic-flip");
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

using common::checked_add;
using common::checked_mul;

inline void validate_exact_shader_integer(std::size_t value, const char* message) {
    common::validate_exact_shader_integer(value, message);
}

[[nodiscard]] inline float water_3d_shader_count_float(std::size_t value, const char* message) {
    return common::water_shader_count_float(value, message);
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
    return common::water_fill_axis_cell_count(axis_cells, kWater3DWallCells,
                                              kWater3DMinFillFraction, kWater3DMaxFillFraction,
                                              fill_fraction);
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

[[nodiscard]] inline std::uint32_t
initial_particle_capacity_for_config(const Water3DConfig& config) {
    return particle_count_for_fill(config, kWater3DMaxFillFraction, kWater3DMaxFillFraction,
                                   kWater3DMaxFillFraction);
}

[[nodiscard]] inline std::uint32_t particle_capacity_for_config(const Water3DConfig& config) {
    const std::uint32_t initial_capacity = initial_particle_capacity_for_config(config);
    if (config.hose.particle_capacity >
        std::numeric_limits<std::uint32_t>::max() - initial_capacity) {
        throw std::runtime_error("water 3D particle capacity exceeds shader index range");
    }
    const std::uint32_t capacity = initial_capacity + config.hose.particle_capacity;
    validate_exact_shader_integer(capacity,
                                  "water 3D particle capacity exceeds exact shader integer range");
    return capacity;
}

[[nodiscard]] inline std::uint32_t
emitter_particle_start_for_config(const Water3DConfig& config) {
    return config.active_particle_count;
}

[[nodiscard]] inline std::uint32_t
emitter_particle_pool_capacity_for_config(const Water3DConfig& config) {
    const std::uint32_t pool_start = emitter_particle_start_for_config(config);
    if (pool_start > config.particle_capacity) {
        throw std::runtime_error("water 3D emitter particle pool starts beyond particle capacity");
    }
    return config.particle_capacity - pool_start;
}

inline void refresh_particle_counts(Water3DConfig& config) {
    config.active_particle_count = active_particle_count_for_fill(config);
    config.initial_particle_capacity = initial_particle_capacity_for_config(config);
    config.particle_capacity = particle_capacity_for_config(config);
    if (config.active_particle_count > config.initial_particle_capacity) {
        throw std::runtime_error("water 3D active particles exceed initial particle capacity");
    }
    if (config.initial_particle_capacity > config.particle_capacity) {
        throw std::runtime_error("water 3D initial particle capacity exceeds total capacity");
    }
}

[[nodiscard]] inline std::uint32_t
water_3d_runtime_particle_scan_count(const Water3DConfig& config,
                                     const Water3DRuntimeState& state) {
    return common::water_runtime_particle_scan_count(
        config.active_particle_count, config.particle_capacity, state.particle_scan_count);
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

[[nodiscard]] inline std::size_t sorted_particle_index_byte_size(const Water3DConfig& config) {
    return checked_mul(static_cast<std::size_t>(config.particle_capacity), sizeof(std::uint32_t),
                       "water 3D sorted particle index buffer is too large");
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

[[nodiscard]] inline Water3DConfig
water_3d_config_from_options(const common::FluidGridOptions& grid,
                             const Water3DStartupOptions& options,
                             const cubey::PbrStaticIblOptions& pbr,
                             const host::CommonRunConfig& common_config) {
    Water3DConfig water_config;
    if (common_config.profile_diagnostics && !common_config.headless) {
        throw std::runtime_error("water 3D profile diagnostics require --headless");
    }
    water_config.profile_diagnostics = common_config.profile_diagnostics;
    water_config.profile_diagnostic_interval = common_config.profile_diagnostic_interval;
    if (grid.width) {
        water_config.grid_width = *grid.width;
    }
    if (grid.height) {
        water_config.grid_height = *grid.height;
    }
    if (grid.depth) {
        water_config.grid_depth = *grid.depth;
    }
    water_config.environment_intensity = pbr.ibl_intensity;
    water_config.environment_rotation_degrees = pbr.environment_rotation_degrees;
    water_config.exposure = pbr.exposure;
    if (options.transfer_mode) {
        water_config.transfer_mode = water_3d_transfer_mode_from_name(*options.transfer_mode);
    }
    if (options.transfer_limit) {
        water_config.max_particles_per_cell = *options.transfer_limit;
    }
    if (options.p2g_mode) {
        water_config.p2g_mode = water_3d_p2g_mode_from_name(*options.p2g_mode);
    }
    if (options.initial_fill_width) {
        water_config.initial_fill_width = *options.initial_fill_width;
    }
    if (options.initial_fill_height) {
        water_config.initial_fill_height = *options.initial_fill_height;
    }
    if (options.initial_fill_depth) {
        water_config.initial_fill_depth = *options.initial_fill_depth;
    }
    if (options.wave_amplitude) {
        water_config.wave.amplitude = *options.wave_amplitude;
    }
    if (options.wave_frequency_hz) {
        water_config.wave.frequency_hz = *options.wave_frequency_hz;
    }
    if (options.whitewater_intensity) {
        water_config.whitewater_intensity = *options.whitewater_intensity;
    }
    if (options.whitewater_speed_threshold) {
        water_config.whitewater_speed_threshold = *options.whitewater_speed_threshold;
    }
    if (options.hose) {
        water_config.hose.enabled = *options.hose;
    }
    if (options.drain) {
        water_config.drain.enabled = *options.drain;
    }
    if (options.rain) {
        water_config.rain.enabled = *options.rain;
    }
    if (options.wave) {
        water_config.wave.enabled = *options.wave;
    }
    if (options.whitewater) {
        water_config.whitewater_enabled = *options.whitewater;
    }
    refresh_particle_counts(water_config);
    static_cast<void>(cell_count(water_config));
    static_cast<void>(u_face_count(water_config));
    static_cast<void>(v_face_count(water_config));
    static_cast<void>(w_face_count(water_config));
    static_cast<void>(total_face_count(water_config));
    validate_water_3d_particle_limits(water_config);
    static_cast<void>(sorted_particle_index_byte_size(water_config));
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

[[nodiscard]] inline std::uint32_t
water_3d_headless_frame_count(const host::CommonRunConfig& config) {
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
