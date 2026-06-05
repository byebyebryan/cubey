#pragma once

#include <cubey/core/run_config.h>

#include <cstdint>
#include <string_view>

namespace cubey::projects::planet {

inline constexpr float kPlanetDefaultRadiusM = 600000.0F;
inline constexpr float kPlanetDefaultAtmosphereHeightM = 70000.0F;
inline constexpr float kPlanetDefaultCameraAltitudeM = 240000.0F;
inline constexpr std::uint32_t kPlanetDefaultPatchesPerFace = 2;
inline constexpr std::uint32_t kPlanetDefaultPatchResolution = 32;
inline constexpr std::uint32_t kPlanetDefaultMaxLodLevel = 2;
inline constexpr float kPlanetDefaultLodTargetEdgePx = 24.0F;
inline constexpr float kPlanetDefaultTerrainHeightScaleM = 12000.0F;
inline constexpr float kPlanetDefaultTerrainNoiseScale = 2.5F;
inline constexpr std::uint32_t kPlanetDefaultTerrainSeed = 1337U;

enum class PlanetDebugView : std::uint8_t {
    Final,
    FaceId,
    PatchId,
    LodLevel,
    ScreenError,
    Seams,
};

struct PlanetConfig {
    float radius_m = kPlanetDefaultRadiusM;
    float atmosphere_height_m = kPlanetDefaultAtmosphereHeightM;
    float camera_altitude_m = kPlanetDefaultCameraAltitudeM;
    std::uint32_t patches_per_face = kPlanetDefaultPatchesPerFace;
    std::uint32_t patch_resolution = kPlanetDefaultPatchResolution;
    std::uint32_t max_lod_level = kPlanetDefaultMaxLodLevel;
    float lod_target_edge_px = kPlanetDefaultLodTargetEdgePx;
    PlanetDebugView debug_view = PlanetDebugView::Final;
    bool wire_overlay = false;
    bool skirts_enabled = true;
    float skirt_depth_scale = 0.75F;
    bool terrain_enabled = true;
    float terrain_height_scale_m = kPlanetDefaultTerrainHeightScaleM;
    float terrain_noise_scale = kPlanetDefaultTerrainNoiseScale;
    std::uint32_t terrain_seed = kPlanetDefaultTerrainSeed;

    friend bool operator==(const PlanetConfig&, const PlanetConfig&) = default;
};

[[nodiscard]] PlanetConfig planet_config_from_run_config(const RunConfig& config);
[[nodiscard]] PlanetDebugView planet_debug_view_from_string(std::string_view value);
[[nodiscard]] const char* planet_debug_view_name(PlanetDebugView view);
void validate_planet_config(const PlanetConfig& config);

} // namespace cubey::projects::planet
