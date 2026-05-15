#include <cubey/engine/gltf_scene_importer.h>

#include "gltf_scene_importer_internal.h"

#include <cubey/core/math.h>
#include <cubey/engine/engine.h>
#include <cubey/scene/scene.h>
#include <cubey/scene/transform_3d.h>

#include <vulkan/vulkan.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

namespace cubey {
namespace {

struct BoundsAccumulator {
    math::Vec3 min{0.0F};
    math::Vec3 max{0.0F};
    bool has_value = false;

    void add(const Bounds3D& bounds) {
        const math::Vec3 bounds_min = bounds.center - bounds.half_extent;
        const math::Vec3 bounds_max = bounds.center + bounds.half_extent;
        if (!has_value) {
            min = bounds_min;
            max = bounds_max;
            has_value = true;
            return;
        }
        min = glm::min(min, bounds_min);
        max = glm::max(max, bounds_max);
    }

    [[nodiscard]] Bounds3D bounds_or_default() const {
        if (!has_value) {
            return {
                .center = {0.0F, 0.0F, 0.0F},
                .half_extent = {1.0F, 1.0F, 1.0F},
            };
        }
        return {
            .center = (min + max) * 0.5F,
            .half_extent = (max - min) * 0.5F,
        };
    }
};

[[nodiscard]] Bounds3D to_scene_bounds(const asset::GltfBounds3D& bounds) {
    return {
        .center = bounds.center,
        .half_extent = bounds.half_extent,
    };
}

[[nodiscard]] Transform3D transform_from_node(const asset::GltfNode& node) {
    return {
        .translation = node.translation,
        .rotation = node.rotation,
        .scale = node.scale,
    };
}

[[nodiscard]] std::vector<render::PbrVertex>
to_pbr_vertices(std::span<const asset::GltfVertex> vertices) {
    std::vector<render::PbrVertex> result;
    result.reserve(vertices.size());
    for (const asset::GltfVertex& vertex : vertices) {
        result.push_back({
            .position = vertex.position,
            .normal = vertex.normal,
            .tangent = vertex.tangent,
            .uv0 = vertex.texcoord0,
            .uv1 = vertex.texcoord1,
            .color0 = vertex.color0,
        });
    }
    return result;
}

[[nodiscard]] render::MaterialHandle material_handle_for_index(const GltfSceneImportResult& result,
                                                               std::uint32_t index) {
    if (index >= result.material_handles.size()) {
        throw std::runtime_error("glTF primitive material index is out of range");
    }
    return result.material_handles[index];
}

void create_mesh_resources(Engine& engine, vulkan::GpuRuntime& gpu,
                           GltfSceneImportResources& resources, GltfSceneImportResult& result,
                           const asset::GltfAsset& asset, const GltfSceneImportConfig& config) {
    resources.mesh_primitives.clear();
    resources.mesh_primitives.resize(asset.meshes.size());
    for (std::size_t mesh_index = 0; mesh_index < asset.meshes.size(); ++mesh_index) {
        const asset::GltfMesh& mesh = asset.meshes[mesh_index];
        std::vector<GltfImportedPrimitive3D>& primitives = resources.mesh_primitives[mesh_index];
        primitives.reserve(mesh.primitives.size());
        for (std::size_t primitive_index = 0; primitive_index < mesh.primitives.size();
             ++primitive_index) {
            const asset::GltfMeshPrimitive& primitive = mesh.primitives[primitive_index];
            std::vector<render::PbrVertex> vertices = to_pbr_vertices(primitive.vertices);
            const render::MeshHandle mesh_handle = engine.render_resources().create_mesh(
                config.label_prefix + ".mesh." + std::to_string(mesh_index) + "." +
                std::to_string(primitive_index));
            resources.meshes.emplace(
                mesh_handle, gpu,
                render::indexed_mesh_config(std::span<const render::PbrVertex>{vertices},
                                            std::span<const std::uint32_t>{primitive.indices}));
            result.mesh_handles.push_back(mesh_handle);
            result.triangle_count += static_cast<std::uint32_t>(primitive.indices.size() / 3U);
            primitives.push_back({
                .mesh = mesh_handle,
                .material = material_handle_for_index(result, primitive.material_index),
                .local_bounds = to_scene_bounds(primitive.local_bounds),
                .mesh_index = static_cast<std::uint32_t>(mesh_index),
                .primitive_index = static_cast<std::uint32_t>(primitive_index),
                .deformation = gltf_primitive_deformation_kind(asset::GltfNode{}, primitive),
            });
        }
    }
}

[[nodiscard]] Bounds3D transform_bounds(const Bounds3D& bounds, const math::Mat4& transform) {
    const math::Vec4 center = transform * math::Vec4{bounds.center, 1.0F};
    const math::Vec3 half_extent{
        (std::abs(transform[0][0]) * bounds.half_extent.x) +
            (std::abs(transform[1][0]) * bounds.half_extent.y) +
            (std::abs(transform[2][0]) * bounds.half_extent.z),
        (std::abs(transform[0][1]) * bounds.half_extent.x) +
            (std::abs(transform[1][1]) * bounds.half_extent.y) +
            (std::abs(transform[2][1]) * bounds.half_extent.z),
        (std::abs(transform[0][2]) * bounds.half_extent.x) +
            (std::abs(transform[1][2]) * bounds.half_extent.y) +
            (std::abs(transform[2][2]) * bounds.half_extent.z),
    };
    return {
        .center = {center.x, center.y, center.z},
        .half_extent = half_extent,
    };
}

void accumulate_node_bounds(const asset::GltfAsset& asset,
                            const GltfSceneImportResources& resources, std::uint32_t node_index,
                            const math::Mat4& parent_world, BoundsAccumulator& accumulator) {
    if (node_index >= asset.nodes.size()) {
        throw std::runtime_error("glTF scene node index is out of range");
    }
    const asset::GltfNode& node = asset.nodes[node_index];
    const math::Mat4 world = parent_world * transform_from_node(node).affine_matrix();
    if (node.mesh_index != asset::kInvalidAssetIndex) {
        if (node.mesh_index >= resources.mesh_primitives.size()) {
            throw std::runtime_error("glTF node mesh index is out of range");
        }
        for (const GltfImportedPrimitive3D& primitive :
             resources.mesh_primitives[node.mesh_index]) {
            accumulator.add(transform_bounds(primitive.local_bounds, world));
        }
    }
    for (const std::uint32_t child : node.children) {
        accumulate_node_bounds(asset, resources, child, world, accumulator);
    }
}

[[nodiscard]] std::uint32_t scene_index_for_import(const asset::GltfAsset& asset,
                                                   const GltfSceneImportConfig& config) {
    const std::uint32_t scene_index =
        config.scene_index == asset::kInvalidAssetIndex ? asset.default_scene : config.scene_index;
    if (scene_index >= asset.scenes.size()) {
        throw std::runtime_error("glTF scene index is out of range");
    }
    return scene_index;
}

[[nodiscard]] Bounds3D calculate_scene_bounds(const asset::GltfAsset& asset,
                                              const GltfSceneImportResources& resources,
                                              std::uint32_t scene_index) {
    BoundsAccumulator accumulator;
    const asset::GltfScene& scene = asset.scenes[scene_index];
    for (const std::uint32_t root : scene.root_nodes) {
        accumulate_node_bounds(asset, resources, root, math::Mat4{1.0F}, accumulator);
    }
    return accumulator.bounds_or_default();
}

Entity create_node(Engine& engine, SceneTransaction& transaction, const asset::GltfAsset& asset,
                   GltfSceneImportResources& resources, GltfSceneImportResult& result,
                   const GltfSceneImportConfig& config, std::uint32_t node_index, Entity parent) {
    if (node_index >= asset.nodes.size()) {
        throw std::runtime_error("glTF scene node index is out of range");
    }
    const asset::GltfNode& node = asset.nodes[node_index];
    Entity entity = transaction.entities().create();
    if (node_index < result.node_entities.size()) {
        result.node_entities[node_index] = entity;
    }
    transaction.transforms3d().create(entity, transform_from_node(node), parent);

    if (node.mesh_index != asset::kInvalidAssetIndex) {
        if (node.mesh_index >= resources.mesh_primitives.size()) {
            throw std::runtime_error("glTF node mesh index is out of range");
        }
        std::vector<RenderablePrimitive3D> primitives;
        BoundsAccumulator bounds;
        for (const GltfImportedPrimitive3D& primitive :
             resources.mesh_primitives[node.mesh_index]) {
            render::MeshHandle render_mesh = primitive.mesh;
            const asset::GltfMeshPrimitive& asset_primitive =
                asset.meshes[node.mesh_index].primitives[primitive.primitive_index];
            const GltfPrimitiveDeformationKind deformation =
                gltf_primitive_deformation_kind(node, asset_primitive);
            if (gltf_primitive_requires_deformation(deformation)) {
                const render::MeshHandle output_mesh = engine.render_resources().create_mesh(
                    config.label_prefix + ".node." + std::to_string(node_index) + ".mesh." +
                    std::to_string(node.mesh_index) + ".primitive." +
                    std::to_string(primitive.primitive_index) + ".deformed");
                result.mesh_handles.push_back(output_mesh);
                resources.deformable_primitives.push_back({
                    .entity = entity,
                    .node_index = node_index,
                    .mesh_index = primitive.mesh_index,
                    .primitive_index = primitive.primitive_index,
                    .skin_index = node.skin_index,
                    .deformation = deformation,
                    .source_mesh = primitive.mesh,
                    .output_mesh = output_mesh,
                    .material = primitive.material,
                    .local_bounds = primitive.local_bounds,
                });
                render_mesh = output_mesh;
            }
            primitives.push_back({
                .mesh = render_mesh,
                .material = primitive.material,
            });
            bounds.add(primitive.local_bounds);
        }
        if (!primitives.empty()) {
            transaction.renderables3d().create(entity,
                                               Renderable3D{
                                                   .primitives = std::move(primitives),
                                                   .local_bounds = bounds.bounds_or_default(),
                                               });
        }
    }

    for (const std::uint32_t child : node.children) {
        create_node(engine, transaction, asset, resources, result, config, child, entity);
    }
    return entity;
}

} // namespace

GltfPrimitiveDeformationKind
gltf_primitive_deformation_kind(const asset::GltfNode& node,
                                const asset::GltfMeshPrimitive& primitive) {
    const bool has_morph_targets = !primitive.morph_targets.empty();
    const bool has_skin = node.skin_index != asset::kInvalidAssetIndex;
    if (has_morph_targets && has_skin) {
        return GltfPrimitiveDeformationKind::MorphSkin;
    }
    if (has_morph_targets) {
        return GltfPrimitiveDeformationKind::Morph;
    }
    if (has_skin) {
        return GltfPrimitiveDeformationKind::Skin;
    }
    return GltfPrimitiveDeformationKind::Static;
}

bool gltf_primitive_requires_deformation(GltfPrimitiveDeformationKind kind) {
    return kind != GltfPrimitiveDeformationKind::Static;
}

GltfSceneImportResult import_gltf_scene(Engine& engine, SceneTransaction& transaction,
                                        const asset::GltfAsset& asset, const vulkan::Device& device,
                                        vulkan::GpuRuntime& gpu,
                                        GltfSceneImportResources& resources,
                                        GltfSceneImportConfig config) {
    if (resources.active) {
        throw std::runtime_error("glTF scene import resources already contain an active import");
    }

    GltfSceneImportResult result;
    try {
        const std::uint32_t scene_index = scene_index_for_import(asset, config);
        create_default_textures(device, gpu, resources);
        create_material_resources(engine, device, gpu, resources, result, asset, config);
        create_mesh_resources(engine, gpu, resources, result, asset, config);
        result.bounds = calculate_scene_bounds(asset, resources, scene_index);
        result.node_entities.resize(asset.nodes.size());

        const asset::GltfScene& scene = asset.scenes[scene_index];
        result.root_entities.reserve(scene.root_nodes.size());
        for (const std::uint32_t root : scene.root_nodes) {
            result.root_entities.push_back(
                create_node(engine, transaction, asset, resources, result, config, root, {}));
        }
        create_deformation_resources(device, gpu, resources, asset, config);

        resources.active = true;
        return result;
    } catch (...) {
        resources.active = true;
        destroy_gltf_scene_import(engine, resources, result);
        throw;
    }
}

void destroy_gltf_scene_import(Engine& engine, GltfSceneImportResources& resources,
                               GltfSceneImportResult& result) {
    if (!resources.active) {
        return;
    }

    for (const render::MaterialHandle material : result.material_handles) {
        if (resources.material_instances.contains(material)) {
            resources.material_instances.erase(material);
        }
        if (engine.render_resources().is_alive(material)) {
            engine.render_resources().destroy_material(material);
        }
    }
    resources.material_factors.clear();

    resources.deformation = {};
    resources.deformable_primitives.clear();

    for (const render::MeshHandle mesh : result.mesh_handles) {
        if (resources.meshes.contains(mesh)) {
            resources.meshes.erase(mesh);
        }
        if (engine.render_resources().is_alive(mesh)) {
            engine.render_resources().destroy_mesh(mesh);
        }
    }

    resources.mesh_primitives.clear();
    resources.textures.clear();
    resources.emissive_default.reset();
    resources.specular_color_default.reset();
    resources.specular_default.reset();
    resources.occlusion_default.reset();
    resources.normal_default.reset();
    resources.metallic_roughness_default.reset();
    resources.base_color_default.reset();
    resources.active = false;
    result = {};
}

void apply_gltf_rigid_animation_sample(SceneEditQueue& edits, const asset::GltfAsset& asset,
                                       const GltfSceneImportResult& result,
                                       const animation::GltfAnimationSample& sample) {
    if (result.node_entities.size() < asset.nodes.size()) {
        throw std::runtime_error("glTF import result does not contain node entity mapping");
    }

    const std::size_t sample_count = std::min(sample.nodes.size(), asset.nodes.size());
    for (std::size_t node_index = 0; node_index < sample_count; ++node_index) {
        const animation::GltfNodeAnimationSample& node_sample = sample.nodes[node_index];
        if (!node_sample.has_translation && !node_sample.has_rotation && !node_sample.has_scale) {
            continue;
        }

        const Entity entity = result.node_entities[node_index];
        if (!entity) {
            continue;
        }

        const asset::GltfNode& node = asset.nodes[node_index];
        Transform3D transform = transform_from_node(node);
        if (node_sample.has_translation) {
            transform.translation = node_sample.translation;
        }
        if (node_sample.has_rotation) {
            transform.rotation = node_sample.rotation;
        }
        if (node_sample.has_scale) {
            transform.scale = node_sample.scale;
        }
        edits.transforms3d().set_local_transform(entity, transform);
    }
}

} // namespace cubey
