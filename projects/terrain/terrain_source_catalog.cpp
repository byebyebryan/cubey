#include "terrain_source_catalog.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace cubey::projects::terrain {
namespace {

constexpr std::string_view kSchema = "cubey.terrain.landscape-variations.v1";

[[nodiscard]] bool safe_relative_path(const std::filesystem::path& path) {
    if (path.empty() || path.is_absolute()) {
        return false;
    }
    return std::none_of(path.begin(), path.end(),
                        [](const std::filesystem::path& component) { return component == ".."; });
}

[[nodiscard]] nlohmann::json read_index(const std::filesystem::path& path) {
    std::ifstream stream(path);
    if (!stream) {
        throw std::runtime_error("failed to open terrain source catalog: " + path.string());
    }
    try {
        return nlohmann::json::parse(stream);
    } catch (const nlohmann::json::exception& error) {
        throw std::runtime_error("invalid terrain source catalog " + path.string() + ": " +
                                 error.what());
    }
}

[[nodiscard]] std::filesystem::path resolve_catalog_path(const std::filesystem::path& root,
                                                         const nlohmann::json& record,
                                                         std::string_view field,
                                                         std::string_view expected_filename) {
    const std::filesystem::path relative = record.at(field).get<std::string>();
    if (!safe_relative_path(relative) || relative.filename() != expected_filename) {
        throw std::runtime_error("terrain source catalog contains an unsafe asset path");
    }
    const std::filesystem::path resolved = (root / relative).lexically_normal();
    std::error_code error;
    if (!std::filesystem::is_regular_file(resolved, error) || error) {
        throw std::runtime_error("terrain source catalog asset is missing: " + resolved.string());
    }
    return resolved;
}

} // namespace

std::vector<TerrainSourceCatalogEntry>
load_terrain_landscape_variation_catalog(const std::filesystem::path& root) {
    const std::filesystem::path normalized_root =
        std::filesystem::absolute(root).lexically_normal();
    const nlohmann::json document = read_index(normalized_root / "variation-index.json");
    try {
        if (document.at("schema").get<std::string_view>() != kSchema) {
            throw std::runtime_error("unsupported terrain source catalog schema");
        }
        const nlohmann::json& variants = document.at("variants");
        if (!variants.is_array() || variants.empty()) {
            throw std::runtime_error("terrain source catalog contains no variants");
        }

        std::set<std::string, std::less<>> ids;
        std::vector<TerrainSourceCatalogEntry> result;
        result.reserve(variants.size());
        for (const nlohmann::json& record : variants) {
            TerrainSourceCatalogEntry entry;
            entry.id = record.at("id").get<std::string>();
            entry.label = record.at("label").get<std::string>();
            entry.seed = record.at("seed").get<std::uint64_t>();
            if (entry.id.empty() || entry.label.empty() || !ids.insert(entry.id).second) {
                throw std::runtime_error(
                    "terrain source catalog contains an invalid or duplicate id");
            }
            entry.heightfield_path =
                resolve_catalog_path(normalized_root, record, "heightfield", "heightfield.json");
            entry.surface_fields_path = resolve_catalog_path(
                normalized_root, record, "surface_fields", "surface-fields.json");
            if (entry.heightfield_path.parent_path().filename() != entry.id ||
                entry.surface_fields_path.parent_path().filename() != entry.id) {
                throw std::runtime_error("terrain source catalog entry path does not match its id");
            }
            result.push_back(std::move(entry));
        }
        return result;
    } catch (const nlohmann::json::exception& error) {
        throw std::runtime_error("terrain source catalog contract is invalid: " +
                                 std::string(error.what()));
    }
}

} // namespace cubey::projects::terrain
