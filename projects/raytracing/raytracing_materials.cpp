#include "raytracing_materials.h"
#include "raytracing_scene.h"
#include <cubey/render/primitive_mesh.h>
#include <stdexcept>

namespace cubey::projects::raytracing {
namespace {
std::filesystem::path catalog_path() {
    return std::filesystem::path(CUBEY_MATERIAL_RECIPE_DIR) / "catalog.json";
}
bool glass(const asset::MaterialRecipe& r) {
    return r.optics.settings.y > 0;
}
} // namespace
WorkshopState::WorkshopState(const Config& c)
    : catalog(asset::load_material_catalog(catalog_path())) {
    bool found = false;
    for (std::size_t i = 0; i < catalog.size(); ++i) {
        recipes.push_back(catalog[i].recipe);
        if (catalog[i].id == c.material) {
            selected = static_cast<std::uint32_t>(i);
            found = true;
        }
    }
    if (!found && c.material != "gallery")
        throw std::invalid_argument("unknown workshop material: " + c.material);
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
void WorkshopState::replace(std::size_t index, asset::MaterialRecipe candidate) {
    asset::validate_material_recipe(candidate);
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
        if (candidate[i].id != catalog[i].id ||
            glass(candidate[i].recipe) != glass(catalog[i].recipe))
            throw std::invalid_argument("catalog identity or routing change requires restart");
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
        const auto& r = recipes[i];
        result.push_back({r.surface,
                          r.pattern,
                          r.optics,
                          r.material_from_object,
                          r.library,
                          {static_cast<std::uint32_t>(i), glass(r) ? 1U : 0U, r.version, 0}});
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
    if (c.material != "bronze" || c.specimen != "sphere")
        throw std::invalid_argument("this workshop checkpoint supports an isolated bronze sphere");
    Scene scene;
    scene.workshop = std::make_shared<WorkshopState>(c);
    const auto& selected = scene.workshop->recipes.at(scene.workshop->selected);
    if (selected.optics.attenuation.w != 0 || math::Vec3(selected.library.sheen) != math::Vec3(0))
        throw std::invalid_argument(
            "rough diffuse and sheen require the opaque-library checkpoint");
    const auto sphere = render::make_uv_sphere_position_color_normal_uv_mesh(
        {.radius = .25F, .latitude_segments = 64, .longitude_segments = 128});
    Mesh mesh;
    mesh.indices.assign(sphere.indices.begin(), sphere.indices.end());
    for (const auto& v : sphere.vertices)
        mesh.vertices.push_back({{v.position[0], v.position[1], v.position[2], 1},
                                 {v.normal[0], v.normal[1], v.normal[2], 0}});
    scene.meshes.push_back(std::move(mesh));
    scene.materials.clear();
    for (const auto& recipe : scene.workshop->recipes)
        scene.materials.push_back(
            {recipe.surface.base_roughness,
             {glass(recipe) ? 2.F : 0.F, recipe.optics.settings.z, recipe.optics.settings.w, 0},
             recipe.optics.attenuation,
             {1, 1, 1, 1}});
    scene.instances.push_back({0, math::Mat4(1), {1, 1, 1, 1}, scene.workshop->selected});
    return scene;
}
} // namespace cubey::projects::raytracing
