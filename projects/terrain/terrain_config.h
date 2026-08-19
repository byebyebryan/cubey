#pragma once

#include "terrain_surface_model.h"

#include <cubey/core/config_schema.h>
#include <cubey/engine/atmosphere_environment_config.h>
#include <cubey/engine/cloud_environment_config.h>
#include <cubey/host/common_config.h>
#include <cubey/render/terrain_backdrop_presentation.h>
#include <cubey/terrain/terrain_placement_mode.h>

#include <cstdint>
#include <filesystem>
#include <optional>
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
    TerrainMaterialMode material = TerrainMaterialMode::FilteredDetail;
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

[[nodiscard]] TerrainRuntimeConfig terrain_runtime_config_from_options(
    const TerrainStartupOptions& options, TerrainDebugView debug_view,
    const std::filesystem::path& default_heightfield_path,
    const std::filesystem::path& default_surface_fields_path = {});

[[nodiscard]] inline TerrainRuntimeConfig terrain_runtime_config_from_options(
    const TerrainStartupOptions& options, const std::filesystem::path& default_heightfield_path,
    const std::filesystem::path& default_surface_fields_path = {}) {
    return terrain_runtime_config_from_options(options, TerrainDebugView::Surface,
                                               default_heightfield_path,
                                               default_surface_fields_path);
}

[[nodiscard]] cubey::AtmosphereEnvironmentRunState
terrain_atmosphere_state_from_options(const cubey::AtmosphereEnvironmentOptions& atmosphere);

[[nodiscard]] cubey::CloudEnvironmentConfig terrain_cloud_config_from_options(
    const cubey::CloudEnvironmentOptions& clouds,
    const cubey::render::AtmosphereEnvironmentConfig& atmosphere);

struct TerrainProjectConfig {
    cubey::host::CommonRunConfig common{};
    std::optional<TerrainDebugView> debug_view{};
    TerrainStartupOptions terrain{};
    cubey::AtmosphereEnvironmentOptions atmosphere{};
    cubey::CloudEnvironmentOptions clouds{};
    TerrainRuntimeConfig runtime{};
};

[[nodiscard]] cubey::config::Schema terrain_project_config_schema(TerrainProjectConfig& config);
[[nodiscard]] TerrainProjectConfig parse_terrain_project_config(
    int argc, char** argv, cubey::config::ParseResult* result = nullptr);

void resolve_terrain_project_config(TerrainProjectConfig& config,
                                    const std::filesystem::path& default_heightfield_path,
                                    const std::filesystem::path& default_surface_fields_path = {});

} // namespace cubey::projects::terrain
