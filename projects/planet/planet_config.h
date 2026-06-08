#pragma once

#include <cubey/core/run_config.h>

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace cubey::projects::planet {

inline constexpr float kPlanetEarthlikeRadiusM = 6371000.0F;
inline constexpr float kPlanetEarthlikeAtmosphereHeightM = 100000.0F;
inline constexpr float kPlanetEarthlikeCameraAltitudeM = 2400000.0F;
inline constexpr float kPlanetMiniRadiusM = 600000.0F;
inline constexpr float kPlanetMiniAtmosphereHeightM = 70000.0F;
inline constexpr float kPlanetMiniCameraAltitudeM = 240000.0F;
inline constexpr float kPlanetDefaultRadiusM = kPlanetEarthlikeRadiusM;
inline constexpr float kPlanetDefaultAtmosphereHeightM = kPlanetEarthlikeAtmosphereHeightM;
inline constexpr float kPlanetDefaultCameraAltitudeM = kPlanetEarthlikeCameraAltitudeM;
inline constexpr std::uint32_t kPlanetDefaultPatchesPerFace = 2;
inline constexpr std::uint32_t kPlanetDefaultPatchResolution = 32;
inline constexpr std::uint32_t kPlanetDefaultMaxLodLevel = 8;
inline constexpr float kPlanetDefaultLodTargetEdgePx = 6.0F;
inline constexpr float kPlanetDefaultLodHysteresis = 0.15F;
inline constexpr float kPlanetDefaultTerrainHeightScaleM = 12000.0F;
inline constexpr float kPlanetDefaultTerrainNoiseScale = 2.5F;
inline constexpr std::uint32_t kPlanetDefaultTerrainSeed = 1337U;
inline constexpr float kPlanetDefaultTerrainMidDetailStrength = 0.58F;
inline constexpr float kPlanetDefaultTerrainFineDetailStrength = 0.12F;
inline constexpr float kPlanetDefaultTerrainFineDetailScale = 12.0F;
inline constexpr float kPlanetDefaultSeaLevelM = 0.0F;
inline constexpr float kPlanetDefaultBathymetryDepthScaleM = 8000.0F;
inline constexpr float kPlanetDefaultShorelineWidthM = 1500.0F;
inline constexpr float kPlanetDefaultAtmosphereHazeStrength = 0.26F;
inline constexpr float kPlanetDefaultAtmosphereHazeStart = 0.55F;
inline constexpr float kPlanetDefaultAtmosphereHazeEnd = 1.10F;
inline constexpr float kPlanetDefaultAtmosphereAerialStrength = 0.45F;
inline constexpr std::uint32_t kPlanetMaxPatchResolution = 128;
inline constexpr std::uint32_t kPlanetMaxLiveLodLevel = 12;
inline constexpr std::uint64_t kPlanetMaxLivePatchInstances = 65536ULL;
inline constexpr std::size_t kPlanetDiagnosticLodCapacity = 16;
inline constexpr std::uint64_t kPlanetCpuMeshVertexCap = 2000000ULL;
inline constexpr std::uint32_t kPlanetDefaultLocalDetailLodLevels = 6;
inline constexpr std::uint32_t kPlanetDefaultLocalDetailCellsPerAxis = 128;
inline constexpr float kPlanetDefaultLocalDetailOuterHalfExtentM = 8192.0F;
inline constexpr float kPlanetDefaultLocalDetailHeightStrengthM = 180.0F;
inline constexpr float kPlanetDefaultLocalDetailScaleM = 160.0F;
inline constexpr std::uint32_t kPlanetMaxLocalDetailLodLevels = 8;
inline constexpr std::uint32_t kPlanetMaxLocalDetailCellsPerAxis = 512;

enum class PlanetDebugView : std::uint8_t {
    Final,
    FaceId,
    PatchId,
    LodLevel,
    ScreenError,
    LodTransition,
    Seams,
    CellEdge,
    TerrainHeight,
    TerrainSlope,
    TerrainMaterial,
    Bathymetry,
    Shoreline,
    LandMask,
    Moisture,
    Temperature,
    Roughness,
    Wireframe,
    CelestialPlanes,
    LocalDetailWireframe,
    LocalDetailBlend,
    LocalDetailLod,
    LocalDetailHeight,
    LocalDetailFeatures,
    LocalDetailFinal,
    TerrainBandBase,
    TerrainBandRelief,
    TerrainBandDetail,
    LocalDetailHorizon,
};

enum class PlanetScalePreset : std::uint8_t {
    Earthlike,
    Mini,
};

enum class PlanetConfigChangeKind : std::uint8_t {
    None,
    Dynamic,
    SurfaceTopology,
    LocalDetailTopology,
};

enum class PlanetAtmosphereMode : std::uint8_t {
    Analytic,
    Physical,
};

struct PlanetConfig {
    PlanetScalePreset scale_preset = PlanetScalePreset::Earthlike;
    float radius_m = kPlanetDefaultRadiusM;
    float atmosphere_height_m = kPlanetDefaultAtmosphereHeightM;
    float camera_altitude_m = kPlanetDefaultCameraAltitudeM;
    std::uint32_t patches_per_face = kPlanetDefaultPatchesPerFace;
    std::uint32_t patch_resolution = kPlanetDefaultPatchResolution;
    std::uint32_t max_lod_level = kPlanetDefaultMaxLodLevel;
    float lod_target_edge_px = kPlanetDefaultLodTargetEdgePx;
    float lod_hysteresis = kPlanetDefaultLodHysteresis;
    std::uint32_t local_detail_lod_levels = kPlanetDefaultLocalDetailLodLevels;
    std::uint32_t local_detail_cells_per_axis = kPlanetDefaultLocalDetailCellsPerAxis;
    float local_detail_outer_half_extent_m = kPlanetDefaultLocalDetailOuterHalfExtentM;
    bool local_detail_enabled = true;
    float local_detail_height_strength_m = kPlanetDefaultLocalDetailHeightStrengthM;
    float local_detail_scale_m = kPlanetDefaultLocalDetailScaleM;
    PlanetDebugView debug_view = PlanetDebugView::Final;
    bool wire_overlay = false;
    bool skirts_enabled = true;
    float skirt_depth_scale = 0.75F;
    bool terrain_enabled = true;
    float terrain_height_scale_m = kPlanetDefaultTerrainHeightScaleM;
    float terrain_noise_scale = kPlanetDefaultTerrainNoiseScale;
    std::uint32_t terrain_seed = kPlanetDefaultTerrainSeed;
    float terrain_mid_detail_strength = kPlanetDefaultTerrainMidDetailStrength;
    float terrain_fine_detail_strength = kPlanetDefaultTerrainFineDetailStrength;
    float terrain_fine_detail_scale = kPlanetDefaultTerrainFineDetailScale;
    float sea_level_m = kPlanetDefaultSeaLevelM;
    float bathymetry_depth_scale_m = kPlanetDefaultBathymetryDepthScaleM;
    float shoreline_width_m = kPlanetDefaultShorelineWidthM;
    float atmosphere_haze_strength = kPlanetDefaultAtmosphereHazeStrength;
    float atmosphere_haze_start = kPlanetDefaultAtmosphereHazeStart;
    float atmosphere_haze_end = kPlanetDefaultAtmosphereHazeEnd;
    float atmosphere_aerial_strength = kPlanetDefaultAtmosphereAerialStrength;
    PlanetAtmosphereMode atmosphere_mode = PlanetAtmosphereMode::Physical;

    friend bool operator==(const PlanetConfig&, const PlanetConfig&) = default;
};

[[nodiscard]] PlanetConfig planet_config_for_scale_preset(PlanetScalePreset preset);
void apply_planet_scale_preset(PlanetConfig& config, PlanetScalePreset preset);
[[nodiscard]] PlanetConfig planet_config_from_run_config(const RunConfig& config);
[[nodiscard]] PlanetConfigChangeKind planet_config_change_kind(const PlanetConfig& current,
                                                               const PlanetConfig& next);
[[nodiscard]] PlanetScalePreset planet_scale_preset_from_string(std::string_view value);
[[nodiscard]] const char* planet_scale_preset_name(PlanetScalePreset preset);
[[nodiscard]] PlanetDebugView planet_debug_view_from_string(std::string_view value);
[[nodiscard]] const char* planet_debug_view_name(PlanetDebugView view);
[[nodiscard]] bool planet_debug_view_is_local_detail(PlanetDebugView view);
[[nodiscard]] bool planet_debug_view_uses_local_detail_surface(PlanetDebugView view);
[[nodiscard]] bool planet_debug_view_uses_horizon_local_detail(PlanetDebugView view);
[[nodiscard]] PlanetAtmosphereMode planet_atmosphere_mode_from_string(std::string_view value);
[[nodiscard]] const char* planet_atmosphere_mode_name(PlanetAtmosphereMode mode);
void validate_planet_config(const PlanetConfig& config);

} // namespace cubey::projects::planet
