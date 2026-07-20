#pragma once

#include "terrain_placement_mode.h"

#include <cubey/core/run_config.h>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string_view>

namespace cubey::projects::terrain {

enum class TerrainDebugView : std::uint8_t {
    Surface = 0,
    Height = 1,
    Slope = 3,
    Clay = 6,
    Normal = 10,
    MaterialWeights = 11,
    AmbientVisibility = 12,
    ProjectedEdge = 14,
    MaterialAlbedo = 15,
    MaterialNormal = 16,
    MaterialRoughness = 18,
    ClassificationNormal = 21,
    StageOwnership = 27,
};

enum class TerrainMaterialMode : std::uint8_t {
    Flat,
    FilteredDetail,
};

struct TerrainRuntimeConfig {
    std::filesystem::path heightfield_path{};
    std::optional<std::uint64_t> expected_seed{};
    TerrainPlacementMode placement = TerrainPlacementMode::Selected;
    std::uint32_t placement_index = 0U;
    float initial_foreground_height_m = 100.0F;
    std::optional<float> initial_azimuth_radians{};
    std::optional<float> initial_orbit_radius_m{};
    std::optional<float> initial_elevation_radians{};
    TerrainDebugView debug_view = TerrainDebugView::Surface;
    TerrainMaterialMode material = TerrainMaterialMode::FilteredDetail;
    bool foreground_sphere = true;
};

[[nodiscard]] std::string_view terrain_debug_view_name(TerrainDebugView view) noexcept;
[[nodiscard]] TerrainDebugView terrain_debug_view_from_name(std::string_view name);
[[nodiscard]] std::string_view terrain_placement_mode_name(TerrainPlacementMode mode) noexcept;
[[nodiscard]] TerrainPlacementMode terrain_placement_mode_from_name(std::string_view name);
[[nodiscard]] std::string_view terrain_material_mode_name(TerrainMaterialMode mode) noexcept;
[[nodiscard]] TerrainMaterialMode terrain_material_mode_from_name(std::string_view name);

void validate_terrain_runtime_config(const TerrainRuntimeConfig& config);
[[nodiscard]] TerrainRuntimeConfig
terrain_runtime_config_from_run_config(const RunConfig& config,
                                       const std::filesystem::path& default_heightfield_path);

} // namespace cubey::projects::terrain
