#include "raytracing_materials.h"
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
    if (c.material == "gallery")
        throw std::invalid_argument("the complete gallery requires the dielectric checkpoint");
    Scene scene;
    scene.workshop = std::make_shared<WorkshopState>(c);
    const auto& selected = scene.workshop->recipes.at(scene.workshop->selected);
    if (glass(selected))
        throw std::invalid_argument("glass requires the dielectric checkpoint");
    const std::array<std::string, 4> shapes{"sphere", "block", "swatch", "cut-block"};
    for (const auto& shape : shapes) {
        const auto source =
            shape == "sphere" ? render::make_material_sphere_mesh(.25F)
            : shape == "swatch"
                ? render::make_material_beveled_block_mesh({.25F, .20F, .018F}, .012F)
            : shape == "cut-block" ? render::make_material_cut_block_mesh()
                                   : render::make_material_beveled_block_mesh();
        Mesh mesh;
        for (const auto& v : source.vertices)
            mesh.vertices.push_back({{v.position, 1}, {v.normal, 0}});
        for (std::size_t j = 0; j < source.indices.size(); j += 3) {
            auto a = source.indices[j], b = source.indices[j + 1], d = source.indices[j + 2];
            const auto cross =
                glm::cross(source.vertices[b].position - source.vertices[a].position,
                           source.vertices[d].position - source.vertices[a].position);
            const auto normal =
                source.vertices[a].normal + source.vertices[b].normal + source.vertices[d].normal;
            // Forward's private specimens supplied shading normals without a
            // uniform boundary winding. Preserve positions/normals; orient RT
            // triangles consistently with those supplied outward normals.
            if (glm::dot(cross, normal) < 0)
                std::swap(b, d);
            mesh.indices.insert(mesh.indices.end(), {a, b, d});
        }
        const auto index = static_cast<std::uint32_t>(scene.meshes.size());
        scene.meshes.push_back(std::move(mesh));
        scene.routes.push_back({shape, index, 1});
        auto world = math::Mat4(1);
        world[0][0] = world[1][1] = world[2][2] = c.object_scale;
        scene.instances.push_back({index, world, {1, 1, 1, 1}, scene.workshop->selected});
    }
    scene.materials.clear();
    for (const auto& recipe : scene.workshop->recipes)
        scene.materials.push_back(
            {recipe.surface.base_roughness,
             {glass(recipe) ? 2.F : 0.F, recipe.optics.settings.z, recipe.optics.settings.w, 0},
             recipe.optics.attenuation,
             {1, 1, 1, 1}});
    return scene;
}
} // namespace cubey::projects::raytracing
