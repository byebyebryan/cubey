#include <algorithm>
#include <cmath>
#include <cubey/asset/material_recipe.h>
#include <fstream>
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace cubey::asset {
namespace {
using J = nlohmann::json;
void keys(const J& j, std::initializer_list<const char*> allowed) {
    if (!j.is_object())
        throw std::invalid_argument("material recipe needs an object");
    for (const auto& [k, v] : j.items()) {
        (void)v;
        if (std::none_of(allowed.begin(), allowed.end(), [&](const char* a) { return k == a; }))
            throw std::invalid_argument("unknown material recipe field: " + k);
    }
}
math::Vec3 v3(const J& j) {
    if (!j.is_array() || j.size() != 3)
        throw std::invalid_argument("material vector needs three numbers");
    return {j.at(0).get<float>(), j.at(1).get<float>(), j.at(2).get<float>()};
}
J arr(math::Vec3 v) {
    return J::array({v.x, v.y, v.z});
}
void parameters(const J& j, std::initializer_list<std::pair<const char*, float*>> fields) {
    if (!j.is_object())
        throw std::invalid_argument("material parameters must be an object");
    for (auto it = j.begin(); it != j.end(); ++it) {
        const auto f =
            std::find_if(fields.begin(), fields.end(), [&](auto f) { return it.key() == f.first; });
        if (f == fields.end())
            throw std::invalid_argument("unknown material parameter: " + it.key());
        *f->second = it.value().get<float>();
    }
}
procedural::SurfaceLibrary library(const J& j) {
    keys(j, {"type", "seed", "scale", "relief", "secondary_color", "wood", "rust", "weave", "paper",
             "rust_roughness", "sheen_color", "sheen_roughness", "rough_glass"});
    const std::array names{"none", "wood", "rust", "weave", "paper"};
    const auto type = j.value("type", std::string{"none"});
    const auto found = std::find(names.begin(), names.end(), type);
    if (found == names.end())
        throw std::invalid_argument("unknown library surface type");
    procedural::SurfaceLibrary p;
    p.settings = {static_cast<float>(found - names.begin()), j.value("seed", 7.0F),
                  j.value("scale", 1.0F), j.value("relief", .0005F)};
    if (j.contains("secondary_color"))
        p.secondary = {v3(j.at("secondary_color")), 0};
    if (j.contains("wood"))
        parameters(j.at("wood"), {{"ring_frequency", &p.wood.ring_frequency},
                                  {"distortion", &p.wood.distortion},
                                  {"grain_frequency", &p.wood.grain_frequency},
                                  {"pore_strength", &p.wood.pore_strength}});
    if (j.contains("rust"))
        parameters(j.at("rust"), {{"coverage", &p.rust.coverage},
                                  {"patch_frequency", &p.rust.patch_frequency},
                                  {"edge_breakup", &p.rust.edge_breakup},
                                  {"pit_strength", &p.rust.pit_strength}});
    if (j.contains("weave"))
        parameters(j.at("weave"), {{"yarn_spacing", &p.weave.yarn_spacing},
                                   {"yarn_width", &p.weave.yarn_width},
                                   {"irregularity", &p.weave.irregularity},
                                   {"fiber_strength", &p.weave.fiber_strength}});
    if (j.contains("paper"))
        parameters(j.at("paper"), {{"fiber_frequency", &p.paper.fiber_frequency},
                                   {"directionality", &p.paper.directionality},
                                   {"fleck_strength", &p.paper.fleck_strength},
                                   {"fiber_strength", &p.paper.fiber_strength}});
    if (j.contains("rust_roughness"))
        p.rust_roughness = {v3(j.at("rust_roughness")), 0};
    if (j.contains("sheen_color"))
        p.sheen = {v3(j.at("sheen_color")), p.sheen.w};
    p.sheen.w = j.value("sheen_roughness", p.sheen.w);
    p.optical_options.x = j.value("rough_glass", false) ? 1.0F : 0.0F;
    return p;
}
J library_json(const procedural::SurfaceLibrary& p) {
    const std::array names{"none", "wood", "rust", "weave", "paper"};
    return {{"type", names.at(static_cast<std::size_t>(p.settings.x))},
            {"seed", p.settings.y},
            {"scale", p.settings.z},
            {"relief", p.settings.w},
            {"secondary_color", arr(math::Vec3{p.secondary})},
            {"wood",
             {{"ring_frequency", p.wood.ring_frequency},
              {"distortion", p.wood.distortion},
              {"grain_frequency", p.wood.grain_frequency},
              {"pore_strength", p.wood.pore_strength}}},
            {"rust",
             {{"coverage", p.rust.coverage},
              {"patch_frequency", p.rust.patch_frequency},
              {"edge_breakup", p.rust.edge_breakup},
              {"pit_strength", p.rust.pit_strength}}},
            {"weave",
             {{"yarn_spacing", p.weave.yarn_spacing},
              {"yarn_width", p.weave.yarn_width},
              {"irregularity", p.weave.irregularity},
              {"fiber_strength", p.weave.fiber_strength}}},
            {"paper",
             {{"fiber_frequency", p.paper.fiber_frequency},
              {"directionality", p.paper.directionality},
              {"fleck_strength", p.paper.fleck_strength},
              {"fiber_strength", p.paper.fiber_strength}}},
            {"rust_roughness", arr(math::Vec3{p.rust_roughness})},
            {"sheen_color", arr(math::Vec3{p.sheen})},
            {"sheen_roughness", p.sheen.w},
            {"rough_glass", p.optical_options.x > .5F}};
}
} // namespace
void validate_material_recipe(const MaterialRecipe& r) {
    if (r.id.empty() || r.id.size() > 64 || r.label.empty() || r.label.size() > 128 ||
        !std::all_of(r.id.begin(), r.id.end(), [](unsigned char c) {
            return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
        }))
        throw std::invalid_argument("invalid material recipe identity");
    for (int i = 0; i < 4; ++i) {
        const float v = r.surface.base_roughness[i], e = r.surface.emission[i];
        if (!std::isfinite(v) || v < 0 || v > 1 || (i == 3 && v < .05F) || !std::isfinite(e) ||
            e < 0 || e > 32 || (i == 3 && e != 0))
            throw std::invalid_argument("invalid material surface values");
    }
    procedural::validate_material_domain(r.material_from_object);
    procedural::validate_material_pattern(r.pattern);
    procedural::validate_material_optics(r.optics);
    procedural::validate_surface_library(r.library);
    if ((r.version != 1 && r.version != 2) ||
        (r.version == 1 && !procedural::default_surface_library(r.library)) ||
        (r.library.settings.x > 0 && r.pattern.settings.x > 0))
        throw std::invalid_argument("unsupported recipe version or combined pattern models");
    const bool rough_glass = r.library.optical_options.x > .5F;
    if (rough_glass && r.optics.settings.y == 0)
        throw std::invalid_argument("rough glass requires transmission");
    if (r.optics.settings.y > 0 &&
        (r.optics.settings.x != 0 || r.pattern.settings.x != 0 || r.library.settings.x != 0 ||
         math::Vec3{r.surface.base_roughness} != math::Vec3{1} ||
         (!rough_glass && r.surface.base_roughness.w != .05F) ||
         math::Vec3{r.library.sheen} != math::Vec3{0} || r.surface.emission != math::Vec4{0} ||
         r.optics.attenuation.w != 0))
        throw std::invalid_argument("glass requires plain white, zero metal/emission/sheen/diffuse "
                                    "roughness; roughness above 0.05 requires V2 rough_glass");
}
MaterialRecipe parse_material_recipe(std::string_view text) {
    const J j = J::parse(text);
    keys(j, {"format", "version", "id", "label", "surface", "material_from_object", "library"});
    if (j.at("format") != "cubey.material.recipe" || !j.at("version").is_number_integer() ||
        (j.at("version") != 1 && j.at("version") != 2) ||
        (j.at("version") == 1 && j.contains("library")))
        throw std::invalid_argument("unsupported material recipe version");
    MaterialRecipe r;
    r.version = j.at("version").get<std::uint32_t>();
    if (j.contains("library"))
        r.library = library(j.at("library"));
    r.id = j.at("id").get<std::string>();
    r.label = j.at("label").get<std::string>();
    const auto& s = j.at("surface");
    keys(s, {"base_color", "roughness", "emission", "optics", "procedural"});
    r.surface.base_roughness = {v3(s.at("base_color")), s.value("roughness", .5F)};
    if (s.contains("emission"))
        r.surface.emission = {v3(s.at("emission")), 0};
    if (s.contains("optics")) {
        const auto& o = s.at("optics");
        keys(o, {"metallic", "transmission", "ior", "attenuation_color", "attenuation_distance",
                 "diffuse_roughness"});
        r.optics.settings = {o.value("metallic", 0.0F), o.value("transmission", 0.0F),
                             o.value("ior", 1.5F), o.value("attenuation_distance", 1.0F)};
        r.optics.attenuation = {o.contains("attenuation_color") ? v3(o.at("attenuation_color"))
                                                                : math::Vec3{1},
                                o.value("diffuse_roughness", 0.0F)};
    }
    if (s.contains("procedural")) {
        const auto& p = s.at("procedural");
        keys(p, {"type", "seed", "scale", "secondary_color", "color_strength",
                 "roughness_variation", "vein_direction", "vein_frequency", "bump_strength",
                 "grain_scale", "vein_width", "warp_strength", "offset", "color_variation"});
        const auto type = p.at("type").get<std::string>();
        if (type != "limestone" && type != "marble" && type != "granular")
            throw std::invalid_argument("unknown material pattern");
        r.pattern.settings = {type == "limestone" ? 1.0F : (type == "marble" ? 2.0F : 3.0F),
                              p.value("seed", 7.0F), p.value("scale", 6.0F),
                              p.value("color_strength", .5F)};
        r.pattern.secondary = {p.contains("secondary_color") ? v3(p.at("secondary_color"))
                                                             : math::Vec3{r.pattern.secondary},
                               p.value("roughness_variation", .08F)};
        r.pattern.veins = {p.contains("vein_direction") ? v3(p.at("vein_direction"))
                                                        : math::Vec3{r.pattern.veins},
                           p.value("vein_frequency", 5.0F)};
        r.pattern.detail = {p.value("bump_strength", .0005F), p.value("grain_scale", 160.0F),
                            p.value("vein_width", .1F), p.value("warp_strength", 5.0F)};
        r.pattern.offset = {p.contains("offset") ? v3(p.at("offset")) : math::Vec3{0},
                            p.value("color_variation", 0.0F)};
    }
    if (j.contains("material_from_object")) {
        const auto& m = j.at("material_from_object");
        if (!m.is_array() || m.size() != 16)
            throw std::invalid_argument("material domain needs 16 column-major numbers");
        for (int i = 0; i < 16; ++i)
            r.material_from_object[i / 4][i % 4] = m.at(static_cast<std::size_t>(i)).get<float>();
    }
    validate_material_recipe(r);
    return r;
}
std::string serialize_material_recipe(const MaterialRecipe& r) {
    validate_material_recipe(r);
    J s = {{"base_color", arr(math::Vec3{r.surface.base_roughness})},
           {"roughness", r.surface.base_roughness.w},
           {"emission", arr(math::Vec3{r.surface.emission})}};
    s["optics"] = {{"metallic", r.optics.settings.x},
                   {"transmission", r.optics.settings.y},
                   {"ior", r.optics.settings.z},
                   {"attenuation_distance", r.optics.settings.w},
                   {"attenuation_color", arr(math::Vec3{r.optics.attenuation})},
                   {"diffuse_roughness", r.optics.attenuation.w}};
    const auto& p = r.pattern;
    if (p.settings.x != 0)
        s["procedural"] = {
            {"type", p.settings.x == 1 ? "limestone" : (p.settings.x == 2 ? "marble" : "granular")},
            {"seed", p.settings.y},
            {"scale", p.settings.z},
            {"color_strength", p.settings.w},
            {"secondary_color", arr(math::Vec3{p.secondary})},
            {"roughness_variation", p.secondary.w},
            {"vein_direction", arr(math::Vec3{p.veins})},
            {"vein_frequency", p.veins.w},
            {"bump_strength", p.detail.x},
            {"grain_scale", p.detail.y},
            {"vein_width", p.detail.z},
            {"warp_strength", p.detail.w},
            {"offset", arr(math::Vec3{p.offset})},
            {"color_variation", p.offset.w}};
    J m = J::array();
    for (int i = 0; i < 16; ++i)
        m.push_back(r.material_from_object[i / 4][i % 4]);
    J document{{"format", "cubey.material.recipe"},
               {"version", r.version},
               {"id", r.id},
               {"label", r.label},
               {"surface", s},
               {"material_from_object", m}};
    if (r.version == 2)
        document["library"] = library_json(r.library);
    return document.dump(2) + "\n";
}
MaterialRecipe load_material_recipe(const std::filesystem::path& path) {
    std::ifstream in(path);
    if (!in)
        throw std::runtime_error("cannot open material recipe: " + path.string());
    return parse_material_recipe(std::string{std::istreambuf_iterator<char>{in}, {}});
}
void save_material_recipe(const std::filesystem::path& path, const MaterialRecipe& r) {
    const auto text = serialize_material_recipe(r);
    if (!path.parent_path().empty())
        std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path);
    if (!out || !(out << text))
        throw std::runtime_error("cannot write material recipe: " + path.string());
}
std::uint64_t material_recipe_fingerprint(const MaterialRecipe& r) {
    std::uint64_t h = 14695981039346656037ULL;
    for (char c : serialize_material_recipe(r)) {
        h ^= static_cast<unsigned char>(c);
        h *= 1099511628211ULL;
    }
    return h;
}
std::vector<MaterialCatalogEntry> load_material_catalog(const std::filesystem::path& path) {
    std::ifstream in(path);
    if (!in)
        throw std::runtime_error("cannot open material catalog: " + path.string());
    const J j = J::parse(in);
    keys(j, {"format", "version", "entries"});
    if (j.at("format") != "cubey.material.catalog" || j.at("version") != 1 ||
        !j.at("entries").is_array() || j.at("entries").empty() || j.at("entries").size() > 32)
        throw std::invalid_argument("unsupported material catalog or entry count");
    std::vector<MaterialCatalogEntry> result;
    for (const auto& e : j.at("entries")) {
        keys(e, {"id", "label", "family", "description", "recipe"});
        MaterialCatalogEntry item;
        item.id = e.at("id").get<std::string>();
        item.label = e.at("label").get<std::string>();
        item.family = e.at("family").get<std::string>();
        item.description = e.at("description").get<std::string>();
        const std::filesystem::path relative = e.at("recipe").get<std::string>();
        if (relative.is_absolute() || relative.has_parent_path() || relative != item.id + ".json" ||
            item.family.empty() || item.family.size() > 64 || item.description.size() > 512 ||
            std::any_of(result.begin(), result.end(),
                        [&](const auto& old) { return old.id == item.id; }))
            throw std::invalid_argument("invalid material catalog entry: " + item.id);
        item.recipe_path = path.parent_path() / relative;
        item.recipe = load_material_recipe(item.recipe_path);
        if (item.recipe.id != item.id || item.recipe.label != item.label)
            throw std::invalid_argument("catalog and recipe identity disagree: " + item.id);
        result.push_back(std::move(item));
    }
    return result;
}
} // namespace cubey::asset
