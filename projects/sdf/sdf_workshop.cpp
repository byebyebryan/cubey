#include "sdf_workshop.h"
#include <nlohmann/json.hpp>
namespace cubey::projects::sdf {
using J = nlohmann::json;
AuthoredScene make_material_workshop(const SdfConfig& c) {
    if (c.workshop != "sphere" && c.workshop != "block" && c.workshop != "csg")
        throw std::invalid_argument("workshop must be sphere, block or csg");
    auto recipe = asset::load_material_recipe(
        c.material_recipe_path.empty()
            ? std::filesystem::path(CUBEY_MATERIAL_RECIPE_DIR) / (c.workshop_material + ".json")
            : c.material_recipe_path);
    auto body = J::parse(asset::serialize_material_recipe(recipe)).at("surface");
    body["name"] = "body";
    body["material_from_object"] =
        J::parse(asset::serialize_material_recipe(recipe)).at("material_from_object");
    const bool glass = recipe.optics.settings.y > 0;
    J mats = J::array({body});
    if (!glass && c.workshop == "csg") {
        auto secondary = body;
        secondary["name"] = "fused";
        // Shared coordinates and pattern persist across fusion; a second base
        // color makes the optional smooth material blend visible.
        const auto base = math::Vec3{recipe.surface.base_roughness};
        secondary["base_color"] = {base.x * .55F, base.y * .55F, base.z * .55F};
        mats.push_back(secondary);
        auto cut = J::parse(asset::serialize_material_recipe(asset::load_material_recipe(
                                std::filesystem::path(CUBEY_MATERIAL_RECIPE_DIR) / "ceramic.json")))
                       .at("surface");
        cut["name"] = "cut";
        mats.push_back(cut);
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
        {"version", 2},
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
    r.id = document.at("authoring").at("recipe_id").get<std::string>();
    r.label = document.at("authoring").at("recipe_label").get<std::string>();
    (void)c;
    r.surface = scene.materials.at(0);
    r.pattern = scene.patterns.at(0);
    r.optics = scene.optics.at(0);
    r.material_from_object =
        scene.material_domains.empty() ? math::Mat4{1} : scene.material_domains.at(0);
    asset::validate_material_recipe(r);
    return r;
}
} // namespace cubey::projects::sdf
