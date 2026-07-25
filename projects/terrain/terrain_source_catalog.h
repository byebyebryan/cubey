#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace cubey::projects::terrain {

struct TerrainSourceCatalogEntry {
    std::string id{};
    std::string label{};
    std::uint64_t seed = 0U;
    std::filesystem::path heightfield_path{};
    std::filesystem::path surface_fields_path{};
};

[[nodiscard]] std::vector<TerrainSourceCatalogEntry>
load_terrain_landscape_variation_catalog(const std::filesystem::path& root);

} // namespace cubey::projects::terrain
