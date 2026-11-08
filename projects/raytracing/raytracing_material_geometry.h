#pragma once
#include "raytracing_materials.h"
#include "raytracing_scene.h"
#include <cubey/render/pbr.h>
#include <span>

namespace cubey::projects::raytracing {
Mesh prepare_workshop_mesh(const render::PrimitiveMeshData<render::PbrVertex>& source);
std::vector<glm::uvec4> optical_edge_neighbors(const Mesh& mesh);
struct WorkshopVolume {
    math::Vec3 minimum, maximum;
    std::uint32_t material;
    Instance::RecipeRole recipe_role = Instance::RecipeRole::Fixed;
};
std::vector<WorkshopVolume> workshop_volumes(const Scene& scene);
std::string workshop_route(const Config& config);
void validate_workshop_view(std::span<const WorkshopVolume>, std::span<const Scene::Route>,
                            const WorkshopState&, const Config&, math::Vec3 eye);
nlohmann::json mesh_admission_identity(const Mesh&);
} // namespace cubey::projects::raytracing
