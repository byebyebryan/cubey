#include <cubey/asset/file_digest.h>
#include <cubey/asset/terrain_site_water.h>
#include <fstream>
#include <nlohmann/json.hpp>
#include <set>
#include <stdexcept>

namespace cubey::asset {
namespace {
using Json = nlohmann::json;
std::string bytes(const std::filesystem::path& path, std::uint64_t limit = 65536) {
    if (std::filesystem::is_symlink(path) || !std::filesystem::is_regular_file(path) ||
        std::filesystem::file_size(path) > limit)
        throw std::runtime_error("invalid site-water metadata path or size");
    std::ifstream stream(path, std::ios::binary);
    return {std::istreambuf_iterator<char>{stream}, {}};
}
std::string digest(const std::filesystem::path& path, std::uint64_t limit = 65536) {
    const auto raw = bytes(path, limit);
    return sha256_hex(std::as_bytes(std::span{raw}));
}
void keys(const Json& object, const std::set<std::string>& expected) {
    if (!object.is_object() || object.size() != expected.size())
        throw std::runtime_error("incompatible site-water binding keys");
    for (const auto& [key, value] : object.items())
        if (!expected.contains(key))
            throw std::runtime_error("unknown site-water binding key");
}
} // namespace

std::filesystem::path load_terrain_site_water_binding(const std::filesystem::path& binding,
                                                      const std::filesystem::path& manifest,
                                                      const TerrainSite& site) {
    std::vector<std::set<std::string>> seen;
    const auto doc = Json::parse(bytes(binding), [&](int, Json::parse_event_t event, Json& value) {
        if (event == Json::parse_event_t::object_start)
            seen.emplace_back();
        else if (event == Json::parse_event_t::key &&
                 !seen.back().insert(value.get<std::string>()).second)
            throw std::runtime_error("duplicate site-water binding key");
        else if (event == Json::parse_event_t::object_end)
            seen.pop_back();
        return true;
    });
    keys(doc, {"schema", "site_id", "world_id", "site_manifest_sha256", "world_recipe_sha256",
               "heightfield_manifest_sha256", "capture"});
    keys(doc.at("capture"), {"path", "sha256"});
    if (doc.at("schema") != "cubey.terrain.site-water-binding.v1" || doc.at("site_id") != site.id ||
        doc.at("world_id") != site.world_id || doc.at("site_manifest_sha256") != digest(manifest) ||
        doc.at("world_recipe_sha256") != digest(manifest.parent_path() / "world-recipe.json") ||
        doc.at("heightfield_manifest_sha256") != digest(site.heightfield))
        throw std::runtime_error("site-water binding source/site/world identity mismatch");
    const std::filesystem::path relative = doc.at("capture").at("path").get<std::string>();
    if (relative.empty() || relative.is_absolute() || relative.has_parent_path() ||
        relative == "." || relative == "..")
        throw std::runtime_error("site-water capture must be a package-local filename");
    const auto capture = binding.parent_path() / relative;
    if (doc.at("capture").at("sha256") != digest(capture, 1024U * 1024U))
        throw std::runtime_error("site-water capture manifest changed");
    return capture;
}

void validate_terrain_site_water_capture(const TerrainWaterCapture& capture,
                                         const TerrainSite& site) {
    validate_terrain_water_capture(capture);
    const auto& grid = capture.grid;
    const auto radius = site.level_spans_m[3] * .5F;
    const auto upper = grid.sample_origin_xz_m +
                       math::Vec2{float(grid.width - 1), float(grid.height - 1)} * grid.cell_size_m;
    if (glm::any(
            glm::greaterThan(glm::abs(grid.center_xz_m() - site.center_xz_m), math::Vec2{.001F})) ||
        glm::any(glm::lessThan(grid.sample_origin_xz_m, site.center_xz_m - math::Vec2{radius})) ||
        glm::any(glm::greaterThan(upper, site.center_xz_m + math::Vec2{radius})))
        throw std::runtime_error("site-water capture centre or coverage does not match site");
}
} // namespace cubey::asset
