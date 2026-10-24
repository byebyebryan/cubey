#include "sdf_workshop.h"
#include <algorithm>
#include <nlohmann/json.hpp>
namespace cubey::projects::sdf {
using J = nlohmann::json;
AuthoredScene make_material_workshop(const SdfConfig& c, const AuthoredScene* working) {
    if (c.workshop != "sphere" && c.workshop != "block" && c.workshop != "csg" &&
        c.workshop != "swatch" && c.workshop != "cut-block")
        throw std::invalid_argument("workshop must be sphere, block, csg, swatch or cut-block");
    const auto recipe = [&] {
        if (working)
            return workshop_material_recipe(c, *working);
        if (!c.material_recipe_path.empty())
            return asset::load_material_recipe(c.material_recipe_path);
        const auto catalog = asset::load_material_catalog(
            std::filesystem::path(CUBEY_MATERIAL_RECIPE_DIR) / "catalog.json");
        const auto selected = std::find_if(catalog.begin(), catalog.end(), [&](const auto& e) {
            return e.id == c.workshop_material;
        });
        if (selected == catalog.end())
            throw std::invalid_argument("unknown workshop material");
        return selected->recipe;
    }();
    const auto snapshot = working ? J::parse(serialize_authored_scene(*working)) : J{};
    const bool resolved = recipe.version == 2 || (working && snapshot.at("version") == 3);
    const auto record = [&](const asset::MaterialRecipe& r, const char* name) -> J {
        const auto json = J::parse(asset::serialize_material_recipe(r));
        if (resolved)
            return {{"name", name}, {"recipe", json}};
        auto surface = json.at("surface");
        surface["name"] = name;
        surface["material_from_object"] = json.at("material_from_object");
        return surface;
    };
    auto body = record(recipe, "body");
    const bool glass = recipe.optics.settings.y > 0;
    J mats = J::array({body});
    if (!glass && c.workshop == "csg") {
        // Shared coordinates and pattern persist across fusion; a second base
        // color makes the optional smooth material blend visible.
        const auto base = math::Vec3{recipe.surface.base_roughness};
        auto secondary = recipe;
        secondary.surface.base_roughness = {base * .55F, recipe.surface.base_roughness.w};
        mats.push_back(record(secondary, "fused"));
        J cut_record;
        if (working)
            for (const auto& saved : snapshot.at("materials"))
                if (saved.at("name") == "cut")
                    cut_record = saved;
        if (cut_record.is_null()) {
            auto cut = asset::load_material_recipe(
                std::filesystem::path(CUBEY_MATERIAL_RECIPE_DIR) / "ceramic.json");
            if (recipe.library.settings.x == 2) {
                cut = asset::MaterialRecipe{};
                cut.id = "steel-cut";
                cut.label = "Fresh steel cut";
                cut.surface.base_roughness = {.42F, .45F, .48F, recipe.library.rust_roughness.x};
                cut.optics.settings.x = 1;
            }
            cut_record = record(cut, "cut");
        }
        mats.push_back(std::move(cut_record));
    }
    // Preserve role edits, including domains and V2 library parameters, rather
    // than reconstructing them from the on-disk recipe during geometry edits.
    if (working)
        for (auto& material : mats)
            for (const auto& saved : snapshot.at("materials"))
                if (saved.at("name") == material.at("name")) {
                    material = saved;
                    break;
                }
    const auto sphere = [](const char* name, float x, float radius, const char* material) {
        return J{{"name", name},
                 {"type", "sphere"},
                 {"position", {x, 0, 0}},
                 {"radius", radius},
                 {"material", material}};
    };
    J root = sphere("sphere", 0, .25F, "body");
    if (c.workshop == "block")
        root = {{"name", "beveled-block"},
                {"type", "box"},
                {"half_extent", {.205F, .205F, .205F}},
                {"rounding", .045F},
                {"material", "body"}};
    if (c.workshop == "swatch")
        root = {{"name", "oriented-swatch"},
                {"type", "box"},
                {"half_extent", {.238F, .188F, .006F}},
                {"rounding", .012F},
                {"material", "body"}};
    if (c.workshop == "cut-block") {
        J block = {{"name", "solid-block"},
                   {"type", "box"},
                   {"half_extent", {.235F, .20F, .16F}},
                   {"material", "body"}};
        J bore = {{"name", "bore"},
                  {"type", "cylinder"},
                  {"radius", .065F},
                  {"half_length", .4F},
                  {"rotation_xyzw", {.70710678, 0, 0, .70710678}}};
        root = {
            {"name", "bored-block"}, {"type", "subtract"}, {"children", J::array({block, bore})}};
    }
    if (c.workshop == "csg") {
        J fuse = {{"name", "fusion"},
                  {"type", "smooth-union"},
                  {"radius", .06F},
                  {"blend_materials", c.workshop_blend_materials},
                  {"children", J::array({sphere("left", -.1F, .2F, "body"),
                                         sphere("right", .1F, .2F, glass ? "body" : "fused")})}};
        J groove = {{"name", "groove"},
                    {"type", "torus"},
                    {"position", {0, .12F, 0}},
                    {"major_radius", .20F},
                    {"minor_radius", .035F}};
        J carved = {
            {"name", "grooved"}, {"type", "subtract"}, {"children", J::array({fuse, groove})}};
        J bore = {{"name", "bore"},
                  {"type", "cylinder"},
                  {"position", {0, -.025F, 0}},
                  {"radius", .075F},
                  {"half_length", .5F},
                  {"rotation_xyzw", {.70710678, 0, 0, .70710678}}};
        root = {
            {"name", "through-cut"}, {"type", "subtract"}, {"children", J::array({carved, bore})}};
        if (!glass && c.workshop_cut_material) {
            root["cut_material"] = "cut";
            root["children"][0]["cut_material"] = "cut";
        }
    }
    auto scene = parse_authored_scene(J{
        {"format", "cubey.sdf.scene"},
        {"version", resolved ? 3 : 2},
        {"name", "SDF material workshop / " + c.workshop + " / " + recipe.label},
        {"authoring", {{"recipe_id", recipe.id}, {"recipe_label", recipe.label}}},
        {"materials", mats},
        {"root", root}}.dump());
    if (!c.export_material_recipe_path.empty())
        asset::save_material_recipe(c.export_material_recipe_path, recipe);
    return scene;
}
asset::MaterialRecipe workshop_material_recipe(const SdfConfig& c, const AuthoredScene& scene) {
    asset::MaterialRecipe r;
    const auto document = J::parse(scene.document);
    if (document.at("version") == 3)
        r = asset::parse_material_recipe(document.at("materials").at(0).at("recipe").dump());
    r.id = document.at("authoring").at("recipe_id").get<std::string>();
    r.label = document.at("authoring").at("recipe_label").get<std::string>();
    (void)c;
    r.surface = scene.materials.at(0);
    r.pattern = scene.patterns.at(0);
    r.optics = scene.optics.at(0);
    r.material_from_object =
        scene.material_domains.empty() ? math::Mat4{1} : scene.material_domains.at(0);
    if (!scene.libraries.empty())
        r.library = scene.libraries.at(0);
    asset::validate_material_recipe(r);
    return r;
}
} // namespace cubey::projects::sdf
