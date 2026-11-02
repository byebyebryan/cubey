#include "sdf_cached_stroke.h"
#include <cstring>
#include <cubey/asset/material_recipe.h>
#include <nlohmann/json.hpp>
#include <stdexcept>
using namespace cubey::projects::sdf;
using Json = nlohmann::json;
namespace {
void require(bool ok, const char* message) {
    if (!ok)
        throw std::runtime_error(message);
}
template <class F> void rejects(F f) {
    bool rejected = false;
    try {
        f();
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    require(rejected, "invalid/stale prepared input accepted");
}
Json sphere(const char* name, float x, float radius) {
    return {{"name", name},
            {"type", "sphere"},
            {"position", {x, 0, 0}},
            {"radius", radius},
            {"material", "red"}};
}
Json document() {
    return {{"format", "cubey.sdf.scene"},
            {"version", 2},
            {"name", "preparation"},
            {"root", sphere("body", 0, .1F)},
            {"materials",
             {{{"name", "red"}, {"base_color", {.5, .2, .1}}, {"roughness", .5}},
              {{"name", "blue"}, {"base_color", {.1, .2, .5}}, {"roughness", .8}}}}};
}
void palette_equal(const AuthoredScene& s) {
    require(Json::parse(serialize_authored_palette(s)) ==
                Json::parse(serialize_authored_scene(s)).at("materials"),
            "palette differs from faithful persistence");
    const auto reload = parse_authored_scene(serialize_authored_scene(s));
    require(serialize_authored_scene(reload) == serialize_authored_scene(s),
            "persistence changed on round trip");
}
} // namespace
int main() {
    auto base_json = document();
    auto edit_json = base_json;
    edit_json["root"] = {{"name", "append"},
                         {"type", "union"},
                         {"children", {base_json["root"], sphere("brush", .12F, .03F)}}};
    auto base = parse_authored_scene(base_json.dump()),
         edited = parse_authored_scene(edit_json.dump());
    for (int kind = 0; kind < 8; ++kind) {
        auto s = base;
        if (kind == 1)
            s.materials[0].base_roughness.x = .7F;
        if (kind == 2)
            s.materials[0].emission.x = .2F;
        if (kind == 3)
            s.patterns[0].settings.x = 1;
        if (kind == 4)
            s.optics[0].settings.x = .2F;
        if (kind == 5)
            s.material_domains[0][3][0] = .25F;
        if (kind == 6) {
            auto j = base_json;
            j["materials"][0]["annotation"] = "retained metadata";
            s = parse_authored_scene(j.dump());
        }
        if (kind == 7) {
            auto j = base_json;
            std::swap(j["materials"][0], j["materials"][1]);
            s = parse_authored_scene(j.dump());
        }
        palette_equal(s);
    }
    cubey::asset::MaterialRecipe recipe;
    recipe.id = "test-recipe";
    recipe.label = "Recipe metadata";
    recipe.version = 2;
    recipe.surface = base.materials[0];
    recipe.pattern = base.patterns[0];
    recipe.optics = base.optics[0];
    auto v3 = base_json;
    v3["version"] = 3;
    v3["materials"] = {{{"name", "red"},
                        {"recipe", Json::parse(cubey::asset::serialize_material_recipe(recipe))}}};
    auto native_v3 = parse_authored_scene(v3.dump());
    palette_equal(native_v3);
    native_v3.materials[0].base_roughness.y = .6F;
    native_v3.patterns[0].settings.x = 1;
    native_v3.material_domains[0][3][0] = .1F;
    palette_equal(native_v3);
    const auto legacy = prepare_cached_stroke(base, edited);
    const auto palette = prepare_cached_stroke(base, edited, false, StrokePreparation::Palette);
    require(!std::memcmp(&legacy.controls, &palette.controls, sizeof(legacy.controls)),
            "preparation control bytes differ");
    auto changed = edited;
    changed.materials[0].base_roughness.x = .7F;
    rejects([&] { (void)prepare_cached_stroke(base, changed, false, StrokePreparation::Palette); });
    changed = edited;
    changed.program[0].parameters.x += .01F;
    rejects([&] { (void)prepare_cached_stroke(base, changed, false, StrokePreparation::Palette); });
    const StrokeVersion version{1, 2, 3, {4}};
    const auto sealed = seal_cached_stroke(base, edited, version);
    const auto hash = authored_geometry_hash(sealed.program().edited);
    base.document.clear();
    edited.program.clear(); // Caller mutations cannot alter the owned result.
    require(authored_geometry_hash(sealed.program().edited) == hash &&
                !sealed.program().base.document.empty(),
            "prepared source is not deeply owned");
    sealed.require_version(version);
    for (int kind = 0; kind < 4; ++kind) {
        auto stale = version;
        if (kind == 0)
            ++stale.session; // Undo/load ABA with identical geometry.
        if (kind == 1)
            ++stale.base;
        if (kind == 2)
            ++stale.palette;
        if (kind == 3)
            stale.pending_ids[0] = 5;
        rejects([&] { sealed.require_version(stale); });
    }
    auto bad = version;
    bad.pending_ids.clear();
    rejects([&] { (void)seal_cached_stroke(legacy.base, legacy.edited, bad); });
    bad = version;
    bad.session = 0;
    rejects([&] { (void)seal_cached_stroke(legacy.base, legacy.edited, bad); });
}
