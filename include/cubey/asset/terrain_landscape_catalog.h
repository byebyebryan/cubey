#pragma once
#include <cubey/asset/terrain_site.h>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace cubey::asset {
struct TerrainLandscapeReference {
    std::filesystem::path path;
    std::string sha256;
};
struct TerrainLandscapePreset {
    std::string id, label, site_id, water_label;
    TerrainLandscapeReference site;
    std::optional<TerrainLandscapeReference> water;
};
struct TerrainLandscapeCatalog {
    std::string proposed_default;
    std::vector<TerrainLandscapePreset> presets;
};
enum class TerrainLandscapeAvailability {
    Available,
    Missing,
    Incomplete,
    Invalid
};
struct TerrainLandscapeProbe {
    TerrainLandscapeAvailability availability = TerrainLandscapeAvailability::Missing;
    std::string reason;
};
struct TerrainLandscapeSelection {
    TerrainSite site;
    std::filesystem::path site_manifest;
    std::optional<std::filesystem::path> water_binding, capture;
};
// Pure catalog parsing: no producer, solver or cache mutation. References are
// pinned and relative to a caller-selected terrain cache root.
[[nodiscard]] TerrainLandscapeCatalog
load_terrain_landscape_catalog(const std::filesystem::path& catalog);
[[nodiscard]] const TerrainLandscapePreset&
terrain_landscape_preset(const TerrainLandscapeCatalog& catalog, std::string_view id);
[[nodiscard]] TerrainLandscapeSelection
resolve_terrain_landscape(const TerrainLandscapePreset& preset,
                          const std::filesystem::path& cache_root);
// Small metadata/hash and file-size checks only; payload hashes and numerical
// admission remain mandatory when constructing the shared capture runtime.
[[nodiscard]] TerrainLandscapeProbe
probe_terrain_landscape(const TerrainLandscapePreset& preset,
                        const std::filesystem::path& cache_root);
[[nodiscard]] std::string_view
terrain_landscape_availability_name(TerrainLandscapeAvailability availability);
} // namespace cubey::asset
