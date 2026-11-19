#include "raytracing_materials.h"
#include "raytracing_material_geometry.h"
#include "raytracing_optical_asset.h"
#include "raytracing_scene.h"
#include <cubey/render/material_specimen_mesh.h>
#include <stdexcept>

namespace cubey::projects::raytracing {
namespace {
std::filesystem::path catalog_path() {
    return std::filesystem::path(CUBEY_MATERIAL_RECIPE_DIR) / "catalog.json";
}
bool glass(const asset::MaterialRecipe& r) {
    return r.optics.settings.y > 0;
}
void validate_workshop_recipe(const asset::MaterialRecipe& r) {
    asset::validate_material_recipe(r);
    if (glass(r) && (r.optics.settings.x != 0 || r.optics.settings.y != 1))
        throw std::invalid_argument(
            "workshop glass requires full dielectric transmission and no metallic lobe");
}
void resolve_materials(Scene& scene) {
    scene.materials.clear();
    for (const auto& recipe : scene.workshop->recipes)
        scene.materials.push_back(
            {recipe.surface.base_roughness,
             {glass(recipe) ? 2.F : 0.F, recipe.optics.settings.z, recipe.optics.settings.w, 0},
             recipe.optics.attenuation,
             {1, 1, 1, 1}});
}
} // namespace
GpuRecipe gpu_recipe(const asset::MaterialRecipe& r, std::uint32_t slot) {
    return {r.surface, r.pattern,
            r.optics,  r.material_from_object,
            r.library, {slot, glass(r) ? 1U : 0U, r.version, 0}};
}
asset::MaterialRecipe marble_base_recipe() {
    auto recipe = asset::load_material_recipe(std::filesystem::path(CUBEY_MATERIAL_RECIPE_DIR) /
                                              "marble.json");
    if (recipe.id != "marble" || glass(recipe) || recipe.optics.settings.x != 0)
        throw std::invalid_argument("marble base requires the opaque dielectric marble recipe");
    return recipe;
}
WorkshopState::WorkshopState(const Config& c)
    : catalog(asset::load_material_catalog(catalog_path())),
      opaque_only(c.scene == "dragon-materials"), sharp_only(c.scene == "dragon-optics") {
    bool found = false;
    for (std::size_t i = 0; i < catalog.size(); ++i) {
        validate_workshop_recipe(catalog[i].recipe);
        recipes.push_back(catalog[i].recipe);
        if (catalog[i].id == c.material) {
            selected = static_cast<std::uint32_t>(i);
            found = true;
        }
    }
    if (!found && c.material != "gallery")
        throw std::invalid_argument("unknown workshop material: " + c.material);
    if ((opaque_only || sharp_only) && (c.material == "gallery" || !supports(selected)))
        throw std::invalid_argument("unsupported material for this dragon presentation");
    if (!c.recipe_input.empty())
        replace(selected, asset::load_material_recipe(c.recipe_input));
    if (c.material_roughness >= 0) {
        auto candidate = recipes.at(selected);
        candidate.surface.base_roughness.w = c.material_roughness;
        replace(selected, std::move(candidate));
    }
    if (!c.recipe_output.empty())
        asset::save_material_recipe(c.recipe_output, recipes.at(selected));
}
bool WorkshopState::supports(std::size_t index) const {
    return (!opaque_only || !glass(catalog.at(index).recipe)) &&
           (!sharp_only || catalog.at(index).id != "frosted-glass");
}
void WorkshopState::replace(std::size_t index, asset::MaterialRecipe candidate) {
    if (!supports(index))
        throw std::invalid_argument("sampled dragon glass requires optical mesh qualification");
    validate_workshop_recipe(candidate);
    if (sharp_only && glass(candidate) && candidate.library.optical_options.x != 0)
        throw std::invalid_argument("dragon optics currently requires sharp glass interfaces");
    if (glass(candidate) != glass(catalog.at(index).recipe))
        throw std::invalid_argument("select a matching opaque or glass workshop slot");
    recipes.at(index) = std::move(candidate);
    ++revision;
}
void WorkshopState::reload() {
    auto candidate = asset::load_material_catalog(catalog_path());
    if (candidate.size() != catalog.size())
        throw std::invalid_argument("catalog roster change requires restart");
    std::vector<asset::MaterialRecipe> next;
    for (std::size_t i = 0; i < candidate.size(); ++i) {
        validate_workshop_recipe(candidate[i].recipe);
        if (candidate[i].id != catalog[i].id ||
            glass(candidate[i].recipe) != glass(catalog[i].recipe))
            throw std::invalid_argument("catalog identity or routing change requires restart");
        if (sharp_only && supports(i) && glass(candidate[i].recipe) &&
            candidate[i].recipe.library.optical_options.x != 0)
            throw std::invalid_argument("dragon optics reload requires sharp glass interfaces");
        next.push_back(candidate[i].recipe);
    }
    catalog = std::move(candidate);
    recipes = std::move(next);
    ++revision;
}
std::vector<GpuRecipe> WorkshopState::gpu_records() const {
    std::vector<GpuRecipe> result;
    result.reserve(recipes.size());
    for (std::size_t i = 0; i < recipes.size(); ++i) {
        result.push_back(gpu_recipe(recipes[i], static_cast<std::uint32_t>(i)));
    }
    return result;
}
nlohmann::json WorkshopState::identity() const {
    auto result = nlohmann::json::array();
    for (std::size_t i = 0; i < recipes.size(); ++i)
        result.push_back(
            {{"slot", i},
             {"catalog_id", catalog[i].id},
             {"source_fingerprint", asset::material_recipe_fingerprint(catalog[i].recipe)},
             {"resolved_fingerprint", asset::material_recipe_fingerprint(recipes[i])},
             {"resolved_recipe",
              nlohmann::json::parse(asset::serialize_material_recipe(recipes[i]))}});
    return result;
}
Scene make_material_scene(const Config& c) {
    Scene scene;
    scene.workshop = std::make_shared<WorkshopState>(c);
    if (c.optical_pair != "none") {
        for (std::size_t i = 0; i < scene.workshop->catalog.size(); ++i) {
            const auto& id = scene.workshop->catalog[i].id;
            if (id != "glass" && id != "frosted-glass")
                continue;
            auto recipe = scene.workshop->recipes[i];
            recipe.surface.base_roughness.w = .05F;
            recipe.library.optical_options.x = 0;
            const bool first = id == "glass";
            if (c.optical_pair == "index-matched") {
                recipe.optics.settings.z = 1;
                recipe.optics.settings.w = first ? .3F : .2F;
                recipe.optics.attenuation =
                    first ? math::Vec4(.8F, .9F, .98F, 0) : math::Vec4(.94F, .7F, .85F, 0);
            } else {
                recipe.optics.settings.z = first ? 1.33F : 1.8F;
                recipe.optics.settings.w = 1;
                recipe.optics.attenuation = {1, 1, 1, 0};
            }
            scene.workshop->replace(i, std::move(recipe));
        }
    }
    const std::array<std::string, 4> shapes{"sphere", "block", "swatch", "cut-block"};
    for (const auto& shape : shapes) {
        const auto source =
            shape == "sphere"
                ? render::make_material_sphere_mesh(.25F, c.sphere_density == "dense" ? 128 : 64,
                                                    c.sphere_density == "dense" ? 256 : 128)
            : shape == "swatch"
                ? render::make_material_beveled_block_mesh({.25F, .20F, .018F}, .012F)
            : shape == "cut-block" ? render::make_material_cut_block_mesh()
                                   : render::make_material_beveled_block_mesh();
        auto mesh = prepare_workshop_mesh(source);
        const auto index = static_cast<std::uint32_t>(scene.meshes.size());
        scene.meshes.push_back(std::move(mesh));
        scene.routes.push_back({shape, index, 1});
        auto world = math::Mat4(1);
        world[0][0] = world[1][1] = world[2][2] = c.object_scale;
        scene.instances.push_back(
            {index, world, {1, 1, 1, 1}, scene.workshop->selected, Instance::RecipeRole::Selected});
    }
    const auto gallery_mesh = static_cast<std::uint32_t>(scene.meshes.size());
    scene.meshes.push_back(prepare_workshop_mesh(render::make_material_sphere_mesh(
        .075F, c.sphere_density == "dense" ? 128 : 64, c.sphere_density == "dense" ? 256 : 128)));
    const auto first = static_cast<std::uint32_t>(scene.instances.size());
    for (std::size_t i = 0; i < scene.workshop->catalog.size(); ++i) {
        auto world = math::Mat4(1);
        world[3] = math::Vec4((float(i % 4) - 1.5F) * .252F, (1 - float(i / 4)) * .195F, 0, 1);
        scene.instances.push_back(
            {gallery_mesh, world, {1, 1, 1, 1}, static_cast<std::uint32_t>(i)});
    }
    scene.routes.push_back(
        {"gallery", first, static_cast<std::uint32_t>(scene.workshop->catalog.size())});
    if (c.optical_pair != "none") {
        const auto mesh = static_cast<std::uint32_t>(scene.meshes.size());
        scene.meshes.push_back(prepare_workshop_mesh(
            render::make_material_beveled_block_mesh({.12F, .12F, .025F}, .005F)));
        const auto start = static_cast<std::uint32_t>(scene.instances.size());
        for (const std::string id : {"glass", "frosted-glass"}) {
            const auto item =
                std::find_if(scene.workshop->catalog.begin(), scene.workshop->catalog.end(),
                             [&](const auto& entry) { return entry.id == id; });
            const auto slot = static_cast<std::uint32_t>(item - scene.workshop->catalog.begin());
            auto world = math::Mat4(1);
            world[3].z = id == "glass" ? .1F : -.1F;
            scene.instances.push_back({mesh, world, {1, 1, 1, 1}, slot});
        }
        scene.routes.push_back({"optical-pair", start, 2});
    }
    resolve_materials(scene);
    const auto target = math::Vec3(c.target_x, c.target_y, c.target_z);
    const float pitch = -c.pitch;
    const auto eye = target + math::Vec3(std::cos(pitch) * std::sin(c.yaw), std::sin(pitch),
                                         std::cos(pitch) * std::cos(c.yaw)) *
                                  c.distance;
    const auto volumes = workshop_volumes(scene);
    validate_workshop_view(volumes, scene.routes, *scene.workshop, c, eye);
    return scene;
}
Scene make_material_dragon(const Config& c) {
    if (c.dragon_support != "rock" || c.support_material != "marble" ||
        c.environment != "atmosphere")
        throw std::invalid_argument(
            "sampled dragon requires its marble slab and shared atmosphere");
    auto geometry = c;
    geometry.scene = "reflection-dragon-solo";
    const bool sphere = c.experimental_glass == "sphere";
    Scene scene;
    if (sphere) {
        // Alternating marble slats and open sky put high-contrast features
        // behind both the silhouette and the smoothly refracted sphere interior.
        scene.meshes.push_back(prepare_workshop_mesh(render::make_material_sphere_mesh(.25F, 64, 128)));
        scene.meshes.push_back(prepare_workshop_mesh(
            render::make_material_beveled_block_mesh({.035F, .6F, .025F}, .004F)));
        scene.instances.push_back({0, math::translation({0, .32F, 0}), {1, 1, 1, 1}});
        for (int strip = -10; strip <= 10; ++strip)
            scene.instances.push_back({1, math::translation({.13F * float(strip), .32F, -.48F}), {1, 1, 1, 1}});
        scene.support_recipe = marble_base_recipe();
    } else {
        scene = make_optical_scene(geometry);
    }
    scene.optical_dragon = c.scene == "dragon-optics";
    if (scene.optical_dragon && !sphere)
        admit_optical_dragon(scene.meshes.at(0), c.model);
    scene.workshop = std::make_shared<WorkshopState>(c);
    scene.material_layout = Scene::MaterialLayout::Dragon;
    scene.routes = {{workshop_route(c), 0, static_cast<std::uint32_t>(scene.instances.size())}};
    scene.instances.at(0).material = scene.workshop->selected;
    scene.instances.at(0).recipe_role = Instance::RecipeRole::Selected;
    // A separate immutable instance slot keeps edits to a marble dragon from
    // changing its base, even when both consume the same canonical recipe.
    for (std::size_t i = 1; i < scene.instances.size(); ++i) {
        scene.instances[i].material = static_cast<std::uint32_t>(scene.workshop->recipes.size());
        scene.instances[i].recipe_role = Instance::RecipeRole::Support;
    }
    resolve_materials(scene);
    const auto& base = *scene.support_recipe;
    scene.materials.push_back({base.surface.base_roughness,
                               {0, base.optics.settings.z, base.optics.settings.w, 0},
                               base.optics.attenuation,
                               {1, 1, 1, 1}});
    if (scene.optical_dragon) {
        const auto eye = math::Vec3(c.target_x, c.target_y, c.target_z) +
                         math::Vec3(std::cos(c.pitch) * std::sin(c.yaw), std::sin(c.pitch),
                                    std::cos(c.pitch) * std::cos(c.yaw)) *
                             c.distance;
        validate_workshop_view(workshop_volumes(scene), scene.routes, *scene.workshop, c, eye);
    }
    return scene;
}
} // namespace cubey::projects::raytracing
