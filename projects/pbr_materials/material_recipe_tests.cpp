#include <chrono>
#include <cubey/asset/material_recipe.h>
#include <fstream>
#include <iostream>
#include <limits>
#include <nlohmann/json.hpp>
#include <stdexcept>
using namespace cubey;
void require(bool v) {
    if (!v)
        throw std::runtime_error("material recipe contract failed");
}
template <class F> void rejects(F f) {
    bool threw = false;
    try {
        f();
    } catch (const std::exception&) {
        threw = true;
    }
    require(threw);
}
int main() {
    for (const char* id : {"plain", "limestone", "marble", "clay", "ceramic", "bronze", "glass"}) {
        const auto r = asset::load_material_recipe(
            std::filesystem::path(CUBEY_MATERIAL_RECIPE_DIR) / (std::string(id) + ".json"));
        const auto text = asset::serialize_material_recipe(r);
        const auto round = asset::parse_material_recipe(text);
        require(asset::serialize_material_recipe(round) == text);
        require(asset::material_recipe_fingerprint(r) == asset::material_recipe_fingerprint(round));
        require(r.material_from_object == math::Mat4{1});
        for (math::Vec3 p : {math::Vec3{0}, math::Vec3{.21F, -.13F, .08F}}) {
            const auto a = procedural::sample_material(r.surface, r.pattern, p, .001F);
            const auto b = procedural::sample_material(round.surface, round.pattern, p, .001F);
            require(a.material.base_roughness == b.material.base_roughness &&
                    a.height_gradient == b.height_gradient);
        }
        auto j = nlohmann::json::parse(text);
        j["typo"] = 3;
        rejects([&] { (void)asset::parse_material_recipe(j.dump()); });
        j = nlohmann::json::parse(text);
        j["surface"]["optics"]["unknown"] = 1;
        rejects([&] { (void)asset::parse_material_recipe(j.dump()); });
        auto bad = r;
        bad.material_from_object[0][0] = 0;
        rejects([&] { asset::validate_material_recipe(bad); });
        bad = r;
        bad.surface.base_roughness.x = std::numeric_limits<float>::quiet_NaN();
        rejects([&] { asset::validate_material_recipe(bad); });
        if (std::string(id) == "clay")
            require(r.surface.base_roughness.w == 1 && r.optics.attenuation.w == .6F &&
                    r.pattern.settings.x == 3);
        if (std::string(id) == "glass") {
            require(r.optics.settings.y == 1 && r.optics.settings.w == .5F);
            bad = r;
            bad.pattern.settings.x = 1;
            rejects([&] { asset::validate_material_recipe(bad); });
        }
    }
    // V2 is explicit: V1 cannot accidentally enable new shading or glass paths.
    auto glass = asset::load_material_recipe(std::filesystem::path(CUBEY_MATERIAL_RECIPE_DIR) /
                                             "glass.json");
    glass.surface.base_roughness.w = .4F;
    rejects([&] { asset::validate_material_recipe(glass); });
    glass.version = 2;
    rejects([&] { asset::validate_material_recipe(glass); });
    glass.library.optical_options.x = 1;
    const auto round_glass = asset::parse_material_recipe(asset::serialize_material_recipe(glass));
    require(round_glass.version == 2 && round_glass.library.optical_options.x == 1 &&
            round_glass.surface.base_roughness.w == .4F);
    glass.library.sheen.x = .1F;
    rejects([&] { asset::validate_material_recipe(glass); });
    for (float kind : {1.0F, 2.0F, 3.0F, 4.0F}) {
        asset::MaterialRecipe recipe;
        recipe.id = "test";
        recipe.label = "Test";
        recipe.version = 2;
        recipe.library.settings.x = kind;
        recipe.library.sheen = {.04F, .03F, .02F, .6F};
        recipe.library.wood.distortion = .87F;
        recipe.library.rust.coverage = .23F;
        recipe.library.weave.yarn_spacing = .017F;
        recipe.library.paper.directionality = .33F;
        recipe.material_from_object = math::translation({.02F, .03F, .04F}) * math::rotation_y(.4F);
        const auto round = asset::parse_material_recipe(asset::serialize_material_recipe(recipe));
        require(round.library == recipe.library &&
                round.material_from_object == recipe.material_from_object);
        auto json = nlohmann::json::parse(asset::serialize_material_recipe(recipe));
        json["library"]["weave"]["yarn_spcaing"] = .01;
        rejects([&] { (void)asset::parse_material_recipe(json.dump()); });
        recipe.version = 1;
        rejects([&] { asset::validate_material_recipe(recipe); });
        recipe.version = 2;
        recipe.library.weave.yarn_spacing = 0;
        rejects([&] { asset::validate_material_recipe(recipe); });
    }
    const auto directory = std::filesystem::path(CUBEY_MATERIAL_RECIPE_DIR);
    const auto catalog = asset::load_material_catalog(directory / "catalog.json");
    require(catalog.size() >= 7);
    const auto fixture =
        std::filesystem::temp_directory_path() /
        ("cubey-material-catalog-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directory(fixture);
    for (const auto& entry : catalog)
        std::filesystem::copy_file(entry.recipe_path, fixture / entry.recipe_path.filename());
    auto json = nlohmann::json::parse(std::ifstream(directory / "catalog.json"));
    const auto write = [&] { std::ofstream(fixture / "catalog.json") << json.dump(); };
    json["entries"].push_back(json["entries"].at(0));
    write();
    rejects([&] { (void)asset::load_material_catalog(fixture / "catalog.json"); });
    json["entries"].erase(json["entries"].size() - 1);
    json["entries"][0]["recipe"] = "../plain.json";
    write();
    rejects([&] { (void)asset::load_material_catalog(fixture / "catalog.json"); });
    json["entries"][0]["recipe"] = "plain.json";
    json["entries"][0]["label"] = "incorrect identity";
    write();
    rejects([&] { (void)asset::load_material_catalog(fixture / "catalog.json"); });
    std::filesystem::remove_all(fixture);
    std::cout
        << "Versioned recipes and catalog: deterministic round-trip, finite validation, strict "
           "fields, supported glass.\n";
}
