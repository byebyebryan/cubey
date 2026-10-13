#include <algorithm>
#include <cubey/asset/file_digest.h>
#include <cubey/asset/terrain_landscape_catalog.h>
#include <cubey/asset/terrain_site_water.h>
#include <fstream>
#include <nlohmann/json.hpp>
#include <set>
#include <stdexcept>

namespace cubey::asset {
namespace {
using Json = nlohmann::json;
std::string read(const std::filesystem::path& path, std::uint64_t limit = 65536) {
    if (std::filesystem::is_symlink(path) || !std::filesystem::is_regular_file(path) ||
        std::filesystem::file_size(path) > limit)
        throw std::runtime_error("invalid landscape metadata path or size: " + path.string());
    std::ifstream stream(path, std::ios::binary);
    if (!stream)
        throw std::runtime_error("unreadable landscape metadata: " + path.string());
    return {std::istreambuf_iterator<char>{stream}, {}};
}
Json parse(const std::string& raw) {
    std::vector<std::set<std::string>> seen;
    return Json::parse(raw, [&](int, Json::parse_event_t event, Json& value) {
        if (event == Json::parse_event_t::object_start)
            seen.emplace_back();
        else if (event == Json::parse_event_t::key &&
                 !seen.back().insert(value.get<std::string>()).second)
            throw std::runtime_error("duplicate landscape metadata key");
        else if (event == Json::parse_event_t::object_end)
            seen.pop_back();
        return true;
    });
}
void keys(const Json& doc, const std::set<std::string>& expected) {
    if (!doc.is_object() || doc.size() != expected.size())
        throw std::runtime_error("incompatible landscape metadata keys");
    for (const auto& [key, value] : doc.items())
        if (!expected.contains(key))
            throw std::runtime_error("unknown landscape metadata key");
}
void relative_path(const std::filesystem::path& path) {
    if (path.empty() || path.is_absolute() || path.has_root_name())
        throw std::runtime_error("landscape reference must be cache-relative");
    for (const auto& component : path)
        if (component == ".." || component == ".")
            throw std::runtime_error("landscape reference escapes cache");
}
TerrainLandscapeReference reference(const Json& doc) {
    keys(doc, {"path", "sha256"});
    TerrainLandscapeReference result{doc.at("path").get<std::string>(),
                                     doc.at("sha256").get<std::string>()};
    relative_path(result.path);
    if (!is_sha256_hex(result.sha256))
        throw std::runtime_error("invalid landscape reference digest");
    return result;
}
std::filesystem::path contained(const std::filesystem::path& root,
                                const std::filesystem::path& relative) {
    relative_path(relative);
    const auto base = std::filesystem::weakly_canonical(root);
    auto current = base;
    for (const auto& component : relative) {
        current /= component;
        std::error_code error;
        const auto status = std::filesystem::symlink_status(current, error);
        if (error && error != std::errc::no_such_file_or_directory)
            throw std::filesystem::filesystem_error("unable to inspect landscape path", current,
                                                    error);
        // Reject dangling links too: weakly_canonical alone can leave an
        // unresolved final symlink looking like an ordinary missing payload.
        if (std::filesystem::is_symlink(status))
            throw std::runtime_error("landscape package symlink rejected: " + current.string());
    }
    const auto candidate = std::filesystem::weakly_canonical(base / relative);
    const auto local = candidate.lexically_relative(base);
    if (local.empty() || *local.begin() == "..")
        throw std::runtime_error("landscape symlink escapes cache");
    return candidate;
}
std::filesystem::path pinned(const std::filesystem::path& root,
                             const TerrainLandscapeReference& ref) {
    const auto path = contained(root, ref.path);
    const auto raw = read(path);
    if (sha256_hex(std::as_bytes(std::span{raw})) != ref.sha256)
        throw std::runtime_error("landscape reference hash mismatch: " + ref.path.string());
    return path;
}
bool present(const std::filesystem::path& path, std::vector<std::string>& missing) {
    std::error_code error;
    const auto status = std::filesystem::status(path, error);
    if (error && error != std::errc::no_such_file_or_directory)
        throw std::filesystem::filesystem_error("unable to inspect landscape asset", path, error);
    if (!std::filesystem::exists(status)) {
        missing.push_back(path.string());
        return false;
    }
    return true;
}
void payloads(const std::filesystem::path& manifest, const char* collection,
              std::vector<std::string>& missing) {
    const auto doc = parse(read(manifest, 1048576));
    if (!doc.at(collection).is_object() || doc.at(collection).empty())
        throw std::runtime_error("empty landscape payload collection");
    for (const auto& [name, field] : doc.at(collection).items()) {
        const std::filesystem::path relative = field.at("path").get<std::string>();
        const auto path = contained(manifest.parent_path(), relative);
        if (!present(path, missing))
            continue;
        if (!std::filesystem::is_regular_file(path) ||
            std::filesystem::file_size(path) != field.at("byte_count").get<std::uint64_t>())
            throw std::runtime_error("landscape payload invalid or wrong size: " + path.string());
    }
}
} // namespace

TerrainLandscapeCatalog load_terrain_landscape_catalog(const std::filesystem::path& catalog) {
    const auto doc = parse(read(catalog));
    keys(doc, {"schema", "proposed_default", "presets"});
    if (doc.at("schema") != "cubey.terrain.landscape-catalog.v1" || !doc.at("presets").is_array() ||
        doc.at("presets").empty() || doc.at("presets").size() > 64)
        throw std::runtime_error("invalid landscape catalog schema or count");
    TerrainLandscapeCatalog result;
    result.proposed_default = doc.at("proposed_default").get<std::string>();
    std::set<std::string> ids;
    for (const auto& item : doc.at("presets")) {
        keys(item, {"id", "label", "site_id", "water_label", "site", "water"});
        TerrainLandscapePreset p;
        p.id = item.at("id").get<std::string>();
        p.label = item.at("label").get<std::string>();
        p.site_id = item.at("site_id").get<std::string>();
        p.water_label = item.at("water_label").get<std::string>();
        if (p.id.empty() || p.id.size() > 64 || p.id == "auto" || p.id == "legacy" ||
            !std::all_of(p.id.begin(), p.id.end(),
                         [](char c) {
                             return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-';
                         }) ||
            p.label.empty() || p.label.size() > 128 || p.site_id.empty() || p.water_label.empty() ||
            !ids.insert(p.id).second)
            throw std::runtime_error("invalid or duplicate landscape preset identity");
        p.site = reference(item.at("site"));
        if (!item.at("water").is_null())
            p.water = reference(item.at("water"));
        result.presets.push_back(std::move(p));
    }
    (void)terrain_landscape_preset(result, result.proposed_default);
    return result;
}
const TerrainLandscapePreset& terrain_landscape_preset(const TerrainLandscapeCatalog& catalog,
                                                       std::string_view id) {
    for (const auto& preset : catalog.presets)
        if (preset.id == id)
            return preset;
    throw std::runtime_error("unknown terrain landscape preset: " + std::string(id));
}
TerrainLandscapeSelection resolve_terrain_landscape(const TerrainLandscapePreset& preset,
                                                    const std::filesystem::path& cache_root) {
    TerrainLandscapeSelection result;
    result.site_manifest = pinned(cache_root, preset.site);
    result.site = load_terrain_site(result.site_manifest);
    if (result.site.id != preset.site_id)
        throw std::runtime_error("landscape preset site identity mismatch");
    if (preset.water) {
        result.water_binding = pinned(cache_root, *preset.water);
        result.capture = load_terrain_site_water_binding(*result.water_binding,
                                                         result.site_manifest, result.site);
    }
    return result;
}
TerrainLandscapeProbe probe_terrain_landscape(const TerrainLandscapePreset& preset,
                                              const std::filesystem::path& cache_root) {
    bool any = false;
    try {
        std::vector<std::string> missing;
        const auto site = contained(cache_root, preset.site.path);
        any = std::filesystem::exists(site.parent_path());
        const bool site_present = present(site, missing);
        bool metadata_complete = site_present;
        std::optional<std::filesystem::path> source_manifest;
        if (site_present) {
            // Check every present pin before reporting absence. An absent water
            // package must not mask a changed site manifest (or vice versa).
            const auto doc = parse(read(pinned(cache_root, preset.site)));
            if (doc.at("site_id") != preset.site_id ||
                doc.at("schema") != "cubey.terrain.site-coverage.v1" ||
                !doc.at("admitted").get<bool>() || !doc.at("rejection_reason").is_null())
                throw std::runtime_error("landscape site identity/admission mismatch");
            source_manifest =
                contained(site.parent_path(), doc.at("heightfield").get<std::string>());
            for (const auto& name :
                 {std::string("world-recipe.json"), doc.at("heightfield").get<std::string>(),
                  doc.at("surface_fields").get<std::string>()}) {
                const auto path = contained(site.parent_path(), name);
                if (!present(path, missing)) {
                    metadata_complete = false;
                    continue;
                }
                if (name == "world-recipe.json")
                    (void)parse(read(path));
                else
                    payloads(path, "files", missing);
            }
        }
        if (preset.water) {
            const auto water = contained(cache_root, preset.water->path);
            any = any || std::filesystem::exists(water.parent_path());
            if (!present(water, missing)) {
                metadata_complete = false;
            } else {
                const auto doc = parse(read(pinned(cache_root, *preset.water)));
                keys(doc, {"schema", "site_id", "world_id", "site_manifest_sha256",
                           "world_recipe_sha256", "heightfield_manifest_sha256", "capture"});
                if (doc.at("schema") != "cubey.terrain.site-water-binding.v1" ||
                    doc.at("site_id") != preset.site_id)
                    throw std::runtime_error("landscape water identity/schema mismatch");
                const auto check_present_pin = [&](const char* key,
                                                   const std::filesystem::path& path) {
                    const auto expected = doc.at(key).get<std::string>();
                    if (!is_sha256_hex(expected))
                        throw std::runtime_error("invalid landscape source digest");
                    if (std::filesystem::exists(path)) {
                        const auto raw = read(path);
                        if (sha256_hex(std::as_bytes(std::span{raw})) != expected)
                            throw std::runtime_error("landscape present source hash mismatch: " +
                                                     path.string());
                    }
                };
                check_present_pin("site_manifest_sha256", site);
                check_present_pin("world_recipe_sha256",
                                  contained(site.parent_path(), "world-recipe.json"));
                if (source_manifest)
                    check_present_pin("heightfield_manifest_sha256", *source_manifest);
                const auto ref = reference(doc.at("capture"));
                if (ref.path.has_parent_path())
                    throw std::runtime_error("landscape capture must be package-local");
                const auto capture = contained(water.parent_path(), ref.path);
                if (!present(capture, missing))
                    metadata_complete = false;
                else
                    payloads(pinned(water.parent_path(), ref), "fields", missing);
            }
        }
        // Full small-metadata admission still runs when only binary payloads
        // are missing, so source/site/world mismatches remain errors.
        if (metadata_complete)
            (void)resolve_terrain_landscape(preset, cache_root);
        if (!missing.empty())
            return {any ? TerrainLandscapeAvailability::Incomplete
                        : TerrainLandscapeAvailability::Missing,
                    "Prepared asset is not installed: " + missing.front()};
        return {TerrainLandscapeAvailability::Available,
                "Metadata pinned; payloads checked on Apply"};
    } catch (const std::exception& error) {
        return {TerrainLandscapeAvailability::Invalid, error.what()};
    }
}
std::string_view terrain_landscape_availability_name(TerrainLandscapeAvailability availability) {
    switch (availability) {
    case TerrainLandscapeAvailability::Available:
        return "available";
    case TerrainLandscapeAvailability::Missing:
        return "not prepared";
    case TerrainLandscapeAvailability::Incomplete:
        return "incomplete";
    case TerrainLandscapeAvailability::Invalid:
        return "invalid";
    }
    return "invalid";
}
} // namespace cubey::asset
