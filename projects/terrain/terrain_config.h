#pragma once

#include "terrain_placement_mode.h"
#include "terrain_surface_model.h"

#include <cubey/core/run_config.h>
#include <cubey/engine/terrain_backdrop_runtime.h>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string_view>

namespace cubey::projects::terrain {

using TerrainDebugView = cubey::TerrainBackdropDebugView;
using TerrainMaterialMode = cubey::TerrainBackdropMaterialMode;

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
[[nodiscard]] TerrainRuntimeConfig terrain_runtime_config_from_run_config(
    const RunConfig& config, const std::filesystem::path& default_heightfield_path,
    const std::filesystem::path& default_surface_fields_path = {});

} // namespace cubey::projects::terrain
