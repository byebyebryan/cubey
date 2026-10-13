#pragma once

#include "terrain_surface_model.h"

#include <cubey/core/config_schema.h>
#include <cubey/engine/atmosphere_environment_config.h>
#include <cubey/engine/cloud_environment_config.h>
#include <cubey/host/common_config.h>
#include <cubey/render/terrain_backdrop_presentation.h>
#include <cubey/terrain/terrain_placement_mode.h>

#include <cstdint>
#include <cubey/asset/terrain_site.h>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace cubey::projects::terrain {

inline constexpr float kTerrainDefaultAerialPerspectiveStrength =
    cubey::render::kTerrainBackdropDefaultAerialPerspectiveStrength;

using TerrainDebugView = cubey::render::TerrainBackdropDebugView;
using TerrainMaterialMode = cubey::render::TerrainBackdropMaterialMode;
using cubey::terrain::TerrainPlacementMode;

// The active terrain executable intentionally accepts only the two product
// camera framings; reference-study camera names are not part of this facade.
enum class TerrainCameraPreset : std::uint8_t {
    Backdrop,
    BackdropStage,
};

// Optional values preserve source layering and explicit-assignment semantics
// until the application resolves them against its CMake-provided defaults.
struct TerrainStartupOptions {
    std::optional<std::filesystem::path> heightfield_path{};
    std::optional<std::filesystem::path> surface_fields_path{};
    std::optional<std::uint64_t> seed{};
    std::optional<TerrainPlacementMode> placement{};
    std::optional<std::uint32_t> placement_index{};
    std::optional<float> foreground_height_m{};
    std::optional<TerrainCameraPreset> camera_preset{};
    std::optional<TerrainMaterialMode> surface_detail{};
    std::optional<float> aerial_perspective_strength{};
    std::optional<bool> shadows{};
    std::optional<std::uint32_t> render_stride{};
    std::optional<float> backdrop_azimuth_degrees{};
    std::optional<float> backdrop_orbit_radius_m{};
    std::optional<float> backdrop_elevation_degrees{};
    std::optional<TerrainSurfaceModel> surface_model{};
};

struct TerrainRuntimeConfig {
    std::filesystem::path heightfield_path{};
    std::filesystem::path surface_fields_path{};
    std::optional<std::uint64_t> expected_seed{};
    TerrainSurfaceModel surface_model = TerrainSurfaceModel::MineralControl;
    TerrainPlacementMode placement = TerrainPlacementMode::Selected;
    std::uint32_t placement_index = 0U;
    float initial_foreground_height_m = 200.0F;
    std::optional<float> initial_azimuth_radians{};
    std::optional<float> initial_orbit_radius_m{};
    std::optional<float> initial_elevation_radians{};
    std::uint32_t render_stride = 3U;
    TerrainDebugView debug_view = TerrainDebugView::Surface;
    TerrainMaterialMode material = TerrainMaterialMode::ReadableDetail;
    float aerial_perspective_strength = kTerrainDefaultAerialPerspectiveStrength;
    bool shadows = true;
    bool foreground_sphere = true;
};

[[nodiscard]] std::string_view terrain_debug_view_name(TerrainDebugView view) noexcept;
[[nodiscard]] TerrainDebugView terrain_debug_view_from_name(std::string_view name);
[[nodiscard]] std::string_view terrain_placement_mode_name(TerrainPlacementMode mode) noexcept;
[[nodiscard]] TerrainPlacementMode terrain_placement_mode_from_name(std::string_view name);
[[nodiscard]] std::string_view terrain_material_mode_name(TerrainMaterialMode mode) noexcept;
[[nodiscard]] TerrainMaterialMode terrain_material_mode_from_name(std::string_view name);
[[nodiscard]] std::string_view terrain_surface_model_name(TerrainSurfaceModel model) noexcept;
[[nodiscard]] TerrainSurfaceModel terrain_surface_model_from_name(std::string_view name);

void validate_terrain_runtime_config(const TerrainRuntimeConfig& config);

[[nodiscard]] TerrainRuntimeConfig
terrain_runtime_config_from_options(const TerrainStartupOptions& options,
                                    TerrainDebugView debug_view,
                                    const std::filesystem::path& default_heightfield_path,
                                    const std::filesystem::path& default_surface_fields_path = {});

[[nodiscard]] inline TerrainRuntimeConfig
terrain_runtime_config_from_options(const TerrainStartupOptions& options,
                                    const std::filesystem::path& default_heightfield_path,
                                    const std::filesystem::path& default_surface_fields_path = {}) {
    return terrain_runtime_config_from_options(
        options, TerrainDebugView::Surface, default_heightfield_path, default_surface_fields_path);
}

[[nodiscard]] cubey::AtmosphereEnvironmentRunState
terrain_atmosphere_state_from_options(const cubey::AtmosphereEnvironmentOptions& atmosphere);

[[nodiscard]] cubey::CloudEnvironmentConfig
terrain_cloud_config_from_options(const cubey::CloudEnvironmentOptions& clouds,
                                  const cubey::render::AtmosphereEnvironmentConfig& atmosphere);

struct TerrainProjectConfig {
    cubey::host::CommonRunConfig common{};
    std::optional<TerrainDebugView> debug_view{};
    TerrainStartupOptions terrain{};
    cubey::AtmosphereEnvironmentOptions atmosphere{};
    cubey::CloudEnvironmentOptions clouds{};
    TerrainRuntimeConfig runtime{};
    std::optional<std::filesystem::path> water_capture_path{};
    std::optional<std::filesystem::path> terrain_site_path{}, site_water_binding_path{},
        captured_water_context_source{};
    std::optional<cubey::asset::TerrainSite> terrain_site{};
    std::string water_capture_view = "captured-patch";
    std::string water_capture_camera = "overview";
    std::optional<float> captured_water_focus_x_m{}, captured_water_focus_z_m{};
    std::optional<float> captured_water_focus_height_m{}, captured_water_span_m{};
    std::optional<float> captured_water_yaw_rad{}, captured_water_pitch_rad{};
    std::string captured_water_detail = "uniform";
    float captured_water_detail_span_m = 16368.0F;
    std::uint32_t captured_water_vertex_budget = 2000000U;
    std::optional<float> captured_water_detail_focus_x_m{}, captured_water_detail_focus_z_m{};
    bool captured_water_visible = true;
    bool captured_water_context = false;
    std::string captured_water_ground_view = "shaded";
    std::string terrain_readability = "refined";
    float terrain_context_max_spacing_m = 0.0F; // retained grid-dependent ring schedule
    float terrain_review_orbit_rad_per_s = 0.0F;
    bool captured_water_animate = true;
    bool captured_water_audit = false;
    bool captured_water_transition_smoke = false;
    double captured_water_animation_s = 0.0;
};

[[nodiscard]] cubey::config::Schema terrain_project_config_schema(TerrainProjectConfig& config);
[[nodiscard]] TerrainProjectConfig
parse_terrain_project_config(int argc, char** argv, cubey::config::ParseResult* result = nullptr);

void resolve_terrain_project_config(TerrainProjectConfig& config,
                                    const std::filesystem::path& default_heightfield_path,
                                    const std::filesystem::path& default_surface_fields_path = {});

} // namespace cubey::projects::terrain
