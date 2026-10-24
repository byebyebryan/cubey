#include "sdf_history.h"
#include "sdf_workshop.h"
#include <chrono>
#include <iostream>
#include <nlohmann/json.hpp>
#include <stdexcept>
namespace sdf = cubey::projects::sdf;
using namespace cubey;
void require(bool v) {
    if (!v)
        throw std::runtime_error("SDF workshop contract failed");
}
int main() {
    sdf::SdfConfig c;
    c.workshop = "sphere";
    for (const char* id : {"plain", "limestone", "marble", "clay", "ceramic", "bronze", "glass"}) {
        c.workshop_material = id;
        auto workshop = sdf::make_material_workshop(c);
        auto legacy = sdf::load_authored_scene(std::filesystem::path(CUBEY_SOURCE_DIR) /
                                               "projects/sdf/scenes/material-study" /
                                               (std::string("sphere-") + id + ".json"));
        require(sdf::authored_geometry_hash(workshop) == sdf::authored_geometry_hash(legacy));
        require(sdf::authored_appearance_hash(workshop) == sdf::authored_appearance_hash(legacy));
        const auto round = sdf::parse_authored_scene(sdf::serialize_authored_scene(workshop));
        require(sdf::authored_appearance_hash(round) == sdf::authored_appearance_hash(workshop));
        const auto recipe = sdf::workshop_material_recipe(c, workshop);
        require(recipe.material_from_object == math::Mat4{1});
    }
    c.workshop = "csg";
    c.workshop_material = "marble";
    const auto base = sdf::make_material_workshop(c);
    require(base.leaf_count == 4 && base.program.size() == 7);
    c.workshop_cut_material = true;
    const auto cut = sdf::make_material_workshop(c);
    require(sdf::authored_geometry_hash(base) == sdf::authored_geometry_hash(cut));
    require(sdf::authored_appearance_hash(base) != sdf::authored_appearance_hash(cut));
    c.workshop_blend_materials = false;
    const auto unblended = sdf::make_material_workshop(c);
    require(sdf::authored_geometry_hash(cut) == sdf::authored_geometry_hash(unblended));
    require(sdf::authored_appearance_hash(cut) != sdf::authored_appearance_hash(unblended));
    auto mapped = base;
    mapped.material_domains[0] = math::translation({.07F, -.03F, .02F}) * math::rotation_z(.5F) *
                                 math::scale({2, .5F, 1.3F});
    const auto round = sdf::parse_authored_scene(sdf::serialize_authored_scene(mapped));
    require(sdf::authored_geometry_hash(mapped) == sdf::authored_geometry_hash(base));
    require(sdf::authored_appearance_hash(mapped) != sdf::authored_appearance_hash(base));
    require(round.material_domains == mapped.material_domains);
    auto pattern = mapped.patterns[0];
    pattern.detail.y = 16;
    const auto domain = mapped.material_domains[0];
    const math::Vec3 point{.13F, -.07F, .2F};
    const auto sample =
        procedural::sample_material_domain(mapped.materials[0], pattern, domain, point);
    const auto height = [&](math::Vec3 p) {
        const auto q = math::Vec3{domain * math::Vec4{p, 1}} + math::Vec3{pattern.offset};
        const auto seed = static_cast<std::uint32_t>(pattern.settings.y);
        return pattern.detail.x *
               (.7F * procedural::material_noise(q * 16.0F, seed + 3001).value +
                .3F * procedural::material_noise(q * (16.0F * 2.03F), seed + 4013).value);
    };
    for (int axis = 0; axis < 3; ++axis) {
        auto a = point, b = point;
        a[axis] += .0001F;
        b[axis] -= .0001F;
        require(std::abs((height(a) - height(b)) / .0002F - sample.height_gradient[axis]) <
                .00002F);
    }
    const auto temporary =
        std::filesystem::temp_directory_path() /
        ("cubey-v3-workshop-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".json");
    asset::MaterialRecipe v2;
    v2.version = 2;
    v2.id = "example";
    v2.label = "Resolved library example";
    v2.library.settings.x = 1;
    v2.library.wood.distortion = .73F;
    v2.material_from_object = domain;
    asset::save_material_recipe(temporary, v2);
    c.workshop = "sphere";
    c.material_recipe_path = temporary;
    auto resolved = sdf::make_material_workshop(c);
    require(resolved.libraries.at(0) == v2.library);
    auto restored = sdf::parse_authored_scene(sdf::serialize_authored_scene(resolved));
    const auto exported = sdf::workshop_material_recipe(c, restored);
    require(exported.version == 2 && exported.library == v2.library &&
            exported.material_from_object == v2.material_from_object);
    require(sdf::authored_appearance_hash(resolved) == sdf::authored_appearance_hash(restored));
    restored.libraries[0].wood.distortion += .1F;
    require(sdf::authored_geometry_hash(resolved) == sdf::authored_geometry_hash(restored));
    require(sdf::authored_appearance_hash(resolved) != sdf::authored_appearance_hash(restored));
    restored = sdf::parse_authored_scene(sdf::serialize_authored_scene(restored));
    require(restored.libraries[0].wood.distortion == v2.library.wood.distortion + .1F);
    // The embedded snapshot remains usable after its source file disappears.
    std::filesystem::remove(temporary);
    require(sdf::workshop_material_recipe(c, restored).library == restored.libraries[0]);
    // Changing the specimen uses the working snapshot even when the source is
    // gone. Preserve unsaved color, domain and library edits together.
    restored.materials[0].base_roughness.x = .23F;
    restored.libraries[0].settings.y = 812;
    const auto edited_recipe =
        asset::material_recipe_fingerprint(sdf::workshop_material_recipe(c, restored));
    c.workshop = "cut-block";
    auto changed_shape = sdf::make_material_workshop(c, &restored);
    require(sdf::authored_geometry_hash(changed_shape) != sdf::authored_geometry_hash(restored));
    require(asset::material_recipe_fingerprint(sdf::workshop_material_recipe(c, changed_shape)) ==
            edited_recipe);
    c.workshop = "swatch";
    changed_shape = sdf::make_material_workshop(c, &changed_shape);
    require(asset::material_recipe_fingerprint(sdf::workshop_material_recipe(c, changed_shape)) ==
            edited_recipe);
    c.material_recipe_path.clear();
    const auto catalog = asset::load_material_catalog(
        std::filesystem::path(CUBEY_MATERIAL_RECIPE_DIR) / "catalog.json");
    for (const auto& entry : catalog) {
        c.workshop_material = entry.id;
        c.workshop = "sphere";
        const auto scene = sdf::make_material_workshop(c);
        const auto saved = sdf::parse_authored_scene(sdf::serialize_authored_scene(scene));
        require(sdf::authored_appearance_hash(saved) == sdf::authored_appearance_hash(scene));
        require(asset::material_recipe_fingerprint(sdf::workshop_material_recipe(c, saved)) ==
                asset::material_recipe_fingerprint(entry.recipe));
        auto edited = saved;
        edited.optics[0].settings.w += .1F;
        edited.material_domains[0] = domain;
        const auto fingerprint =
            asset::material_recipe_fingerprint(sdf::workshop_material_recipe(c, edited));
        c.workshop = "csg";
        auto csg = sdf::make_material_workshop(c, &edited);
        require(asset::material_recipe_fingerprint(sdf::workshop_material_recipe(c, csg)) ==
                fingerprint);
        if (csg.materials.size() == 3) {
            csg.materials[1].base_roughness.x = .123F;
            csg.materials[2].base_roughness.w = .789F;
            csg.material_domains[2] = domain;
            if (entry.recipe.version == 2)
                csg.libraries[1].settings.y = 124;
            const auto appearance = sdf::authored_appearance_hash(csg);
            c.workshop_cut_material = !c.workshop_cut_material;
            c.workshop_blend_materials = !c.workshop_blend_materials;
            const auto toggled = sdf::make_material_workshop(c, &csg);
            require(sdf::authored_geometry_hash(toggled) == sdf::authored_geometry_hash(csg));
            require(sdf::authored_appearance_hash(toggled) != appearance);
            require(nlohmann::json::parse(sdf::serialize_authored_scene(toggled)).at("materials") ==
                    nlohmann::json::parse(sdf::serialize_authored_scene(csg)).at("materials"));
        }
        // An explicit reset still reloads the catalog recipe.
        const auto reset = sdf::make_material_workshop(c);
        require(asset::material_recipe_fingerprint(sdf::workshop_material_recipe(c, reset)) ==
                asset::material_recipe_fingerprint(entry.recipe));
    }
    c.workshop = "csg";
    c.workshop_material = "rusted-steel";
    c.workshop_cut_material = true;
    const auto rust = sdf::make_material_workshop(c);
    const auto fresh = sdf::authored_surface({.075F, -.025F, .15F}, rust);
    require(fresh.optics.settings.x == 1 &&
            fresh.material.base_roughness.w == rust.libraries[0].rust_roughness.x);
    const auto story =
        sdf::load_authored_scene(std::filesystem::path(CUBEY_SOURCE_DIR) /
                                 "projects/sdf/scenes/material-presentation/wood-cut.json");
    const auto story_round = sdf::parse_authored_scene(sdf::serialize_authored_scene(story));
    require(story_round.history.size() == 2 && story_round.history[1].cutters.size() == 1);
    const auto body_only = sdf::authored_history_scene(story_round, 1);
    require(body_only.material_domains == story_round.material_domains &&
            body_only.libraries == story_round.libraries);
    require(asset::material_recipe_fingerprint(sdf::workshop_material_recipe(c, body_only)) ==
            asset::material_recipe_fingerprint(sdf::workshop_material_recipe(c, story_round)));
    require(sdf::authored_distance({0, 0, 0}, body_only) < 0 &&
            sdf::authored_distance({0, 0, 0}, story_round) > 0);
    sdf::TimelineConfig story_time{
        .motion = "material-story", .duration = 8, .orbit_turns = 1.0F / 18};
    std::size_t last_prefix = 1;
    float last_yaw = 0;
    for (int frame = 0; frame <= 480; ++frame) {
        const auto sample = sdf::sample_authored_history(story_time, story_round, frame / 60.0);
        require(sample.timeline.prefix >= last_prefix && sample.timeline.prefix >= 1 &&
                sample.timeline.prefix <= 2 && sample.timeline.yaw >= last_yaw &&
                sample.timeline.debug == 0);
        if (!sample.cutters.empty())
            require(sample.timeline.prefix == 1);
        last_prefix = sample.timeline.prefix;
        last_yaw = sample.timeline.yaw;
    }
    require(sdf::sample_authored_history(story_time, story_round, 0).timeline.yaw == 0);
    require(sdf::sample_authored_history(story_time, story_round, 8).timeline.yaw ==
            sdf::sample_authored_history(story_time, story_round, 6.5).timeline.yaw);
    for (std::size_t step = 1; step <= 2; ++step)
        require(
            sdf::sample_authored_history(story_time, story_round,
                                         sdf::authored_history_seek(story_time, story_round, step))
                .timeline.prefix == step);
    std::cout << "Shared/legacy seven-recipe fingerprints exact; CSG appearance leaves geometry "
                 "cache identity intact; affine bump derivative verified; working materials "
                 "survive specimen and CSG option changes.\n";
}
