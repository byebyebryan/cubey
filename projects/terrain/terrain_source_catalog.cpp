#include "terrain_source_catalog.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace cubey::projects::terrain {
namespace {

constexpr std::string_view kSchema = "cubey.terrain.source-presets.v1";

[[nodiscard]] bool safe_id(std::string_view id) {
    if (id.empty() || id.front() == '-' || id.back() == '-') {
        return false;
    }
    return std::all_of(id.begin(), id.end(), [](char character) {
        const unsigned char value = static_cast<unsigned char>(character);
        return std::islower(value) != 0 || std::isdigit(value) != 0 || character == '-';
    });
}

[[nodiscard]] bool valid_sha256(std::string_view hash) {
    return hash.size() == 64U && std::all_of(hash.begin(), hash.end(), [](char character) {
               const unsigned char value = static_cast<unsigned char>(character);
               return std::isdigit(value) != 0 || (character >= 'a' && character <= 'f');
           });
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

[[nodiscard]] bool regular_file(const std::filesystem::path& path) {
    std::error_code error;
    return std::filesystem::is_regular_file(path, error) && !error;
}

} // namespace

TerrainSourcePresetCatalog
load_terrain_source_preset_catalog(const std::filesystem::path& catalog_path,
                                   const std::filesystem::path& optional_asset_root) {
    const std::filesystem::path normalized_catalog =
        std::filesystem::absolute(catalog_path).lexically_normal();
    const std::filesystem::path normalized_root =
        std::filesystem::absolute(optional_asset_root).lexically_normal();
    const nlohmann::json document = read_index(normalized_catalog);
    try {
        if (document.at("schema").get<std::string_view>() != kSchema) {
            throw std::runtime_error("unsupported terrain source catalog schema");
        }
        const std::string default_id = document.at("default_preset").get<std::string>();
        const nlohmann::json& policy = document.at("policy");
        if (!safe_id(default_id) || policy.at("generated_assets_committed").get<bool>() ||
            policy.at("default_generation").get<std::string_view>() !=
                "single-preset-single-query" ||
            policy.at("optional_generation").get<std::string_view>() !=
                "one-preset-per-explicit-target" ||
            policy.at("study_generation").get<std::string_view>() != "developer-only") {
            throw std::runtime_error("terrain source catalog policy is incompatible");
        }
        const nlohmann::json& presets = document.at("presets");
        if (!presets.is_array() || presets.empty()) {
            throw std::runtime_error("terrain source catalog contains no presets");
        }

        std::set<std::string, std::less<>> ids;
        TerrainSourcePresetCatalog result;
        result.default_id = default_id;
        result.optional_presets.reserve(presets.size() - 1U);
        bool found_default = false;
        for (const nlohmann::json& record : presets) {
            const std::string id = record.at("id").get<std::string>();
            const std::string label = record.at("label").get<std::string>();
            const std::string tier = record.at("tier").get<std::string>();
            const std::string asset_directory = record.at("asset_directory").get<std::string>();
            const std::string generation_target = record.at("generation_target").get<std::string>();
            if (!safe_id(id) || label.empty() || !ids.insert(id).second) {
                throw std::runtime_error(
                    "terrain source catalog contains an invalid or duplicate id");
            }
            if (tier == "default") {
                if (found_default || id != default_id || asset_directory != "default" ||
                    generation_target != "cubey_terrain_generate_default_asset") {
                    throw std::runtime_error("terrain source catalog default is incompatible");
                }
                const nlohmann::json& generator = record.at("generator");
                const nlohmann::json& expected = record.at("expected");
                if (generator.at("mode").get<std::string_view>() != "canonical-default" ||
                    generator.at("climate_output").get<bool>() ||
                    !valid_sha256(expected.at("elevation_sha256").get<std::string>()) ||
                    !expected.at("climate_sha256").is_null()) {
                    throw std::runtime_error(
                        "terrain source catalog default generator is incompatible");
                }
                found_default = true;
                result.default_label = label;
                result.default_seed = generator.at("seed").get<std::uint64_t>();
                continue;
            }
            if (tier != "optional" || asset_directory != id) {
                throw std::runtime_error("terrain optional source catalog entry is incompatible");
            }
            std::string target_suffix = id;
            std::replace(target_suffix.begin(), target_suffix.end(), '-', '_');
            const std::string expected_target = "cubey_terrain_generate_" + target_suffix;
            if (generation_target != expected_target) {
                throw std::runtime_error("terrain source generation target is incompatible");
            }
            const nlohmann::json& generator = record.at("generator");
            if (generator.at("mode").get<std::string_view>() != "natural-region" ||
                !generator.at("climate_output").get<bool>()) {
                throw std::runtime_error("terrain source generator contract is incompatible");
            }
            const std::uint64_t seed = generator.at("candidate").at("seed").get<std::uint64_t>();
            const nlohmann::json& expected = record.at("expected");
            if (!valid_sha256(expected.at("elevation_sha256").get<std::string>()) ||
                !valid_sha256(expected.at("climate_sha256").get<std::string>())) {
                throw std::runtime_error("terrain source expected hashes are incompatible");
            }

            const std::filesystem::path directory = normalized_root / id;
            TerrainSourceCatalogEntry entry;
            entry.id = id;
            entry.label = label;
            entry.seed = seed;
            entry.generation_target = generation_target;
            entry.heightfield_path = directory / "heightfield.json";
            entry.surface_fields_path = directory / "surface-fields.json";
            entry.generation_marker_path = normalized_root / (id + ".generating");
            result.optional_presets.push_back(std::move(entry));
        }
        if (!found_default || result.default_label.empty()) {
            throw std::runtime_error("terrain source catalog default is missing");
        }
        return result;
    } catch (const nlohmann::json::exception& error) {
        throw std::runtime_error("terrain source catalog contract is invalid: " +
                                 std::string(error.what()));
    }
}

TerrainSourceAvailability terrain_source_availability(const TerrainSourceCatalogEntry& entry) {
    if (regular_file(entry.generation_marker_path)) {
        return TerrainSourceAvailability::Generating;
    }
    const bool heightfield = regular_file(entry.heightfield_path);
    const bool surface_fields = regular_file(entry.surface_fields_path);
    if (heightfield && surface_fields) {
        return TerrainSourceAvailability::Available;
    }
    std::error_code error;
    const bool directory_exists =
        std::filesystem::exists(entry.heightfield_path.parent_path(), error) && !error;
    if (!heightfield && !surface_fields && !directory_exists) {
        return TerrainSourceAvailability::NotGenerated;
    }
    return TerrainSourceAvailability::Incomplete;
}

std::string_view terrain_source_availability_name(TerrainSourceAvailability availability) noexcept {
    switch (availability) {
    case TerrainSourceAvailability::NotGenerated:
        return "not generated";
    case TerrainSourceAvailability::Generating:
        return "generating";
    case TerrainSourceAvailability::Available:
        return "available";
    case TerrainSourceAvailability::Incomplete:
        return "incomplete";
    }
    return "incomplete";
}

} // namespace cubey::projects::terrain
