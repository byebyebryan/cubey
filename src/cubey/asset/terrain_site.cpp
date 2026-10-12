#include <cmath>
#include <cubey/asset/file_digest.h>
#include <cubey/asset/terrain_site.h>
#include <fstream>
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace cubey::asset {
namespace {
using Json = nlohmann::json;
Json read(const std::filesystem::path& path) {
    if (std::filesystem::file_size(path) > 65536)
        throw std::runtime_error("terrain site manifest too large");
    std::ifstream stream(path);
    return Json::parse(stream);
}
math::Vec2 point(const Json& value) {
    if (!value.is_array() || value.size() != 2)
        throw std::runtime_error("terrain site requires XZ pair");
    math::Vec2 p{value.at(0).get<float>(), value.at(1).get<float>()};
    if (!std::isfinite(p.x) || !std::isfinite(p.y))
        throw std::runtime_error("nonfinite terrain site point");
    return p;
}
TerrainHeightSourceBounds box(const Json& value) {
    TerrainHeightSourceBounds b{point(value.at("minimum")), point(value.at("maximum"))};
    validate_terrain_height_source_bounds(b);
    if (glm::any(glm::greaterThanEqual(b.minimum_xz, b.maximum_xz)))
        throw std::runtime_error("empty terrain site coverage");
    return b;
}
std::filesystem::path child(const std::filesystem::path& root, const std::string& name) {
    const std::filesystem::path p{name};
    if (p.empty() || p.is_absolute())
        throw std::runtime_error("terrain site path must be relative");
    for (const auto& part : p)
        if (part == "..")
            throw std::runtime_error("terrain site path escapes package");
    const auto candidate = std::filesystem::canonical(root / p);
    const auto relative = candidate.lexically_relative(std::filesystem::canonical(root));
    if (relative.empty() || *relative.begin() == "..")
        throw std::runtime_error("terrain site symlink escapes package");
    return candidate;
}
} // namespace

TerrainSite load_terrain_site(const std::filesystem::path& manifest) {
    const auto doc = read(manifest);
    const auto root = manifest.parent_path();
    const auto world = read(child(root, "world-recipe.json"));
    if (doc.at("schema") != "cubey.terrain.site-coverage.v1" ||
        world.at("schema") != "cubey.terrain.world-recipe.v1" ||
        doc.at("world_id") != world.at("world_id") || !doc.at("admitted").get<bool>() ||
        !doc.at("rejection_reason").is_null())
        throw std::runtime_error("terrain site world/admission mismatch");
    TerrainSite result;
    result.id = doc.at("site_id").get<std::string>();
    result.world_id = doc.at("world_id").get<std::string>();
    if (result.id.empty() || !is_sha256_hex(result.world_id))
        throw std::runtime_error("invalid terrain site identity");
    result.center_xz_m = point(doc.at("center_xz_m"));
    result.world_bounds = box(world.at("survey_bounds"));
    const auto& levels = doc.at("levels");
    if (!levels.is_array() || levels.size() != 4)
        throw std::runtime_error("terrain site requires four nested coverage levels");
    float previous = 0;
    for (std::size_t i = 0; i < 4; ++i) {
        const float span = levels.at(i).at("span_m").get<float>();
        const auto bounds = box(levels.at(i).at("bounds"));
        if (!std::isfinite(span) || span <= previous || span > 262144 ||
            glm::any(glm::greaterThan(
                glm::abs(bounds.minimum_xz - (result.center_xz_m - math::Vec2{span * .5F})),
                math::Vec2{.01F})) ||
            glm::any(glm::greaterThan(
                glm::abs(bounds.maximum_xz - (result.center_xz_m + math::Vec2{span * .5F})),
                math::Vec2{.01F})))
            throw std::runtime_error("invalid nested terrain site coverage");
        result.level_spans_m[i] = span;
        previous = span;
        result.backdrop_bounds = bounds;
    }
    const float camera = doc.at("camera_margin_m").get<float>();
    const float sampling = doc.at("sampling_margin_m").get<float>();
    if (!std::isfinite(camera) || !std::isfinite(sampling) || camera < 0 || sampling < 0 ||
        camera > 10000 || sampling > 10000 ||
        !terrain_height_source_bounds_contains_disk(result.world_bounds, result.center_xz_m,
                                                    previous * .5F + camera + sampling))
        throw std::runtime_error("terrain site backdrop/margins outside world");
    result.heightfield = child(root, doc.at("heightfield").get<std::string>());
    result.surface_fields = child(root, doc.at("surface_fields").get<std::string>());
    const auto field = read(result.heightfield);
    const auto& mapping = world.at("coordinate_mapping");
    const auto& grid = field.at("grid");
    const auto width = grid.at("width").get<std::uint32_t>();
    const auto height = grid.at("height").get<std::uint32_t>();
    if (width < 2 || height < 2 || std::uint64_t(width) * height > 25000000)
        throw std::runtime_error("terrain site native cell budget exceeded");
    const double spacing = mapping.at("native_spacing_m").get<double>();
    const auto& native = field.at("provenance").at("model_native_origin");
    const double expected_x =
        mapping.at("world_origin_xz_m").at(0).get<double>() +
        (native.at("j").get<double>() - mapping.at("model_origin_ij").at(1).get<double>()) *
            spacing;
    const double expected_z =
        mapping.at("world_origin_xz_m").at(1).get<double>() +
        (native.at("i").get<double>() - mapping.at("model_origin_ij").at(0).get<double>()) *
            spacing;
    if (field.at("schema") != "cubey.terrain.heightfield.v1" ||
        field.at("seed") != world.at("seed") || field.at("source") != world.at("producer") ||
        field.at("height") != world.at("height_transform") ||
        field.at("provenance").at("world_id") != result.world_id ||
        grid.at("axis_mapping") != Json{{"world_x", "model_j"}, {"world_z", "model_i"}} ||
        grid.at("sample_spacing_m").get<double>() != spacing ||
        std::abs(grid.at("sample_origin_x_m").get<double>() - expected_x) > .001 ||
        std::abs(grid.at("sample_origin_z_m").get<double>() - expected_z) > .001)
        throw std::runtime_error("terrain site producer/coordinate binding mismatch");
    const auto lo = math::Vec2{float(expected_x), float(expected_z)};
    const auto hi =
        lo + math::Vec2{grid.at("width").get<float>() - 1, grid.at("height").get<float>() - 1} *
                 float(spacing);
    if (glm::any(glm::greaterThan(lo, result.backdrop_bounds.minimum_xz - math::Vec2{sampling})) ||
        glm::any(glm::lessThan(hi, result.backdrop_bounds.maximum_xz + math::Vec2{sampling})))
        throw std::runtime_error("prepared terrain site coverage incomplete");
    return result;
}
} // namespace cubey::asset
