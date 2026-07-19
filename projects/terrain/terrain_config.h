#pragma once

#include "terrain_backdrop_density.h"
#include "terrain_backdrop_stage.h"
#include "terrain_source.h"

#include <cubey/core/run_config.h>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string_view>

namespace cubey::projects::terrain {

enum class TerrainCameraPreset : std::uint8_t {
    Oblique,
    Profile,
    Top,
    Surface,
    SurfaceLow,
    Ground,
    Backdrop,
    BackdropStage,
    Midground,
};

enum class TerrainDebugView : std::uint8_t {
    Surface,
    Height,
    BaseHeight,
    Slope,
    Weathering,
    Lod,
    Clay,
    Shadow,
    AerialTransmittance,
    VegetationCoverage,
    Normal,
    MaterialWeights,
    AmbientVisibility,
    TessellationFactor,
    ProjectedEdge,
    MaterialAlbedo,
    MaterialNormal,
    SourceBands,
    MaterialRoughness,
    MaterialHeight,
    MaterialCavity,
    ClassificationNormal,
    SourceRange,
    SourceMassif,
    SourceValley,
    SourceRidge,
    SourceMeso,
    StageOwnership,
};

enum class TerrainPresentationMode : std::uint8_t {
    Standard,
    Backdrop,
};

enum class TerrainRenderPath : std::uint8_t {
    Control,
    Quality,
    Backdrop,
};

enum class TerrainBackdropProfile : std::uint8_t {
    HardCutV1,
    RadialV1,
    RasterV1,
};

enum class TerrainBackdropCenterOwnership : std::uint8_t {
    ConsumerOwned,
    Continuous,
};

enum class TerrainSurfaceDetail : std::uint8_t {
    Tile,
    Layered,
};

struct TerrainRuntimeConfig {
    TerrainSourceConfig source{};
    std::filesystem::path heightfield_path{};
    TerrainCameraPreset camera = TerrainCameraPreset::Oblique;
    TerrainBackdropProfile backdrop_profile = TerrainBackdropProfile::HardCutV1;
    TerrainBackdropCenterOwnership backdrop_center = TerrainBackdropCenterOwnership::ConsumerOwned;
    TerrainBackdropStageMode backdrop_mode = TerrainBackdropStageMode::Detached;
    std::optional<float> backdrop_azimuth_radians{};
    std::optional<float> backdrop_orbit_radius_m{};
    std::optional<float> backdrop_elevation_radians{};
    float backdrop_minimum_visible_distance_m = 3'200.0F;
    TerrainDebugView debug_view = TerrainDebugView::Surface;
    TerrainPresentationMode presentation = TerrainPresentationMode::Standard;
    TerrainRenderPath render_path = TerrainRenderPath::Control;
    TerrainBackdropMeshDensity backdrop_mesh_density = TerrainBackdropMeshDensity::High;
    TerrainSurfaceDetail surface_detail = TerrainSurfaceDetail::Tile;
    float target_edge_px = 4.0F;
    float near_cell_size_m = 2.0F;
    float vertical_scale = 1.0F;
    std::uint32_t lod_levels = 8U;
    std::uint32_t cells_per_axis = 128U;
};

[[nodiscard]] std::string_view terrain_camera_preset_name(TerrainCameraPreset preset);
[[nodiscard]] TerrainCameraPreset terrain_camera_preset_from_name(std::string_view name);
[[nodiscard]] bool terrain_camera_is_surface(TerrainCameraPreset preset) noexcept;
[[nodiscard]] bool terrain_camera_is_backdrop(TerrainCameraPreset preset) noexcept;
[[nodiscard]] bool terrain_camera_advances_headless(TerrainCameraPreset preset) noexcept;
[[nodiscard]] std::string_view
terrain_backdrop_profile_name(TerrainBackdropProfile profile) noexcept;
[[nodiscard]] TerrainBackdropProfile terrain_backdrop_profile_from_name(std::string_view name);
[[nodiscard]] std::string_view
terrain_backdrop_center_ownership_name(TerrainBackdropCenterOwnership ownership) noexcept;
[[nodiscard]] TerrainBackdropCenterOwnership
terrain_backdrop_center_ownership_from_name(std::string_view name);
[[nodiscard]] float terrain_camera_clearance_m(TerrainCameraPreset preset);
[[nodiscard]] float terrain_camera_traversal_speed_mps(TerrainCameraPreset preset) noexcept;
[[nodiscard]] float terrain_camera_fovy_radians(TerrainCameraPreset preset) noexcept;
[[nodiscard]] std::string_view terrain_debug_view_name(TerrainDebugView view);
[[nodiscard]] TerrainDebugView terrain_debug_view_from_name(std::string_view name);
[[nodiscard]] std::string_view terrain_presentation_mode_name(TerrainPresentationMode mode);
[[nodiscard]] TerrainPresentationMode terrain_presentation_mode_from_name(std::string_view name);
[[nodiscard]] std::string_view terrain_render_path_name(TerrainRenderPath path);
[[nodiscard]] TerrainRenderPath terrain_render_path_from_name(std::string_view name);
[[nodiscard]] std::string_view terrain_surface_detail_name(TerrainSurfaceDetail detail);
[[nodiscard]] TerrainSurfaceDetail terrain_surface_detail_from_name(std::string_view name);
void validate_terrain_runtime_config(const TerrainRuntimeConfig& config);
[[nodiscard]] TerrainRuntimeConfig terrain_runtime_config_from_run_config(const RunConfig& config);

} // namespace cubey::projects::terrain
