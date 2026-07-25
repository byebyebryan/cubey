#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace cubey::projects::terrain {

enum class TerrainSourceAvailability : std::uint8_t {
    NotGenerated,
    Generating,
    Available,
    Incomplete,
};

struct TerrainSourceCatalogEntry {
    std::string id{};
    std::string label{};
    std::uint64_t seed = 0U;
    std::string generation_target{};
    std::filesystem::path heightfield_path{};
    std::filesystem::path surface_fields_path{};
    std::filesystem::path generation_marker_path{};
};

struct TerrainSourcePresetCatalog {
    std::string default_id{};
    std::string default_label{};
    std::uint64_t default_seed = 0U;
    std::vector<TerrainSourceCatalogEntry> optional_presets{};
};

[[nodiscard]] TerrainSourcePresetCatalog
load_terrain_source_preset_catalog(const std::filesystem::path& catalog_path,
                                   const std::filesystem::path& optional_asset_root);
[[nodiscard]] TerrainSourceAvailability
terrain_source_availability(const TerrainSourceCatalogEntry& entry);
[[nodiscard]] std::string_view
terrain_source_availability_name(TerrainSourceAvailability availability) noexcept;

} // namespace cubey::projects::terrain
