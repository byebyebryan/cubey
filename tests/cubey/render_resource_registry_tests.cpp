#include <cubey/render/resource_registry.h>

#include <stdexcept>
#include <string>
#include <unordered_map>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void require_throws(auto&& action, const char* message) {
    try {
        action();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error(message);
}

} // namespace

void test_render_resource_registry_tracks_handle_lifetime_and_labels() {
    cubey::render::RenderResourceRegistry registry;

    const cubey::render::MeshHandle mesh = registry.create_mesh("cube mesh");
    const cubey::render::MaterialHandle material = registry.create_material("cube material");

    require(static_cast<bool>(mesh), "created mesh handle should be non-null");
    require(static_cast<bool>(material), "created material handle should be non-null");
    require(registry.is_alive(mesh), "created mesh handle should be alive");
    require(registry.is_alive(material), "created material handle should be alive");
    require(registry.label(mesh) == "cube mesh", "mesh label should round-trip");
    require(registry.label(material) == "cube material", "material label should round-trip");

    registry.destroy_mesh(mesh);
    registry.destroy_material(material);

    require(!registry.is_alive(mesh), "destroyed mesh handle should stop being alive");
    require(!registry.is_alive(material), "destroyed material handle should stop being alive");
    require_throws([&registry, mesh] { (void)registry.label(mesh); },
                   "destroyed mesh label lookup should reject stale handles");
    require_throws([&registry, material] { (void)registry.label(material); },
                   "destroyed material label lookup should reject stale handles");

    const cubey::render::MeshHandle recreated_mesh = registry.create_mesh("replacement mesh");
    const cubey::render::MaterialHandle recreated_material =
        registry.create_material("replacement material");

    require(recreated_mesh.index == mesh.index, "registry should reuse retired mesh slots");
    require(recreated_material.index == material.index,
            "registry should reuse retired material slots");
    require(recreated_mesh.generation != mesh.generation,
            "reused mesh slot should advance generation");
    require(recreated_material.generation != material.generation,
            "reused material slot should advance generation");
    require(!registry.is_alive(mesh), "old mesh generation should remain stale after reuse");
    require(!registry.is_alive(material),
            "old material generation should remain stale after reuse");
    require(registry.is_alive(recreated_mesh), "replacement mesh should be alive");
    require(registry.is_alive(recreated_material), "replacement material should be alive");
}

void test_render_resource_handles_are_hashable_keys() {
    cubey::render::RenderResourceRegistry registry;
    const cubey::render::MeshHandle mesh = registry.create_mesh();
    const cubey::render::MaterialHandle material = registry.create_material();

    std::unordered_map<cubey::render::MeshHandle, std::string, cubey::render::MeshHandleHash>
        meshes;
    std::unordered_map<cubey::render::MaterialHandle, std::string,
                       cubey::render::MaterialHandleHash>
        materials;

    meshes[mesh] = "mesh";
    materials[material] = "material";

    require(meshes.at(mesh) == "mesh", "mesh handles should work as hash keys");
    require(materials.at(material) == "material", "material handles should work as hash keys");
}

void test_render_resource_registry_round_trips_mesh_and_material_info() {
    cubey::render::RenderResourceRegistry registry;

    const cubey::render::MeshHandle mesh = registry.create_mesh(cubey::render::MeshInfo{
        .label = "main cube mesh",
    });
    const cubey::render::MaterialHandle material =
        registry.create_material(cubey::render::MaterialInfo{
            .label = "transparent surface",
            .domain = cubey::render::MaterialDomain::Surface3D,
            .alpha_mode = cubey::render::MaterialAlphaMode::Blend,
            .blend = cubey::render::MaterialBlendMode::AlphaBlend,
            .sort_key = 42,
        });
    const cubey::render::MaterialHandle default_material =
        registry.create_material("default material");

    const cubey::render::MeshInfo mesh_info = registry.mesh_info(mesh);
    const cubey::render::MaterialInfo material_info = registry.material_info(material);
    const cubey::render::MaterialInfo default_info = registry.material_info(default_material);

    require(mesh_info.label == "main cube mesh", "mesh info label should round-trip");
    require(material_info.label == "transparent surface", "material info label should round-trip");
    require(material_info.domain == cubey::render::MaterialDomain::Surface3D,
            "material domain should round-trip");
    require(material_info.alpha_mode == cubey::render::MaterialAlphaMode::Blend,
            "material alpha mode should round-trip");
    require(material_info.blend == cubey::render::MaterialBlendMode::AlphaBlend,
            "material blend mode should round-trip");
    require(material_info.cull_mode == VK_CULL_MODE_BACK_BIT,
            "material cull mode should default to single-sided back-face culling");
    require(material_info.sort_key == 42, "material sort key should round-trip");
    require(cubey::render::material_supports_pass(material_info,
                                                  cubey::render::MaterialPassKind::DepthOnly),
            "material pass mask should default to depth-only support");
    require(cubey::render::material_supports_pass(material_info,
                                                  cubey::render::MaterialPassKind::ForwardColor),
            "material pass mask should default to forward-color support");
    require(default_info.blend == cubey::render::MaterialBlendMode::Opaque,
            "string material creation should use opaque blend by default");
    require(default_info.alpha_mode == cubey::render::MaterialAlphaMode::Opaque,
            "string material creation should use opaque alpha mode by default");

    const cubey::render::MaterialHandle forward_material =
        registry.create_material(cubey::render::MaterialInfo{
            .label = "forward only",
            .pass_mask =
                cubey::render::material_pass_mask(cubey::render::MaterialPassKind::ForwardColor),
        });
    const cubey::render::MaterialInfo forward_info = registry.material_info(forward_material);
    require(!cubey::render::material_supports_pass(forward_info,
                                                   cubey::render::MaterialPassKind::DepthOnly),
            "custom material pass mask should round-trip excluded depth-only pass");
    require(cubey::render::material_supports_pass(forward_info,
                                                  cubey::render::MaterialPassKind::ForwardColor),
            "custom material pass mask should round-trip included forward pass");

    registry.destroy_mesh(mesh);
    registry.destroy_material(material);

    require_throws([&registry, mesh] { (void)registry.mesh_info(mesh); },
                   "destroyed mesh info lookup should reject stale handles");
    require_throws([&registry, material] { (void)registry.material_info(material); },
                   "destroyed material info lookup should reject stale handles");
}
