#include <cubey/engine/gltf_scene_importer.h>

#include "gltf_scene_importer_internal.h"

#include <cubey/core/math.h>
#include <cubey/engine/engine.h>
#include <cubey/scene/scene.h>
#include <cubey/scene/transform_3d.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
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
            return {.center = {0.0F, 0.0F, 0.0F}, .half_extent = {1.0F, 1.0F, 1.0F}};
        }
        return {.center = (min + max) * 0.5F, .half_extent = (max - min) * 0.5F};
    }
};

[[nodiscard]] Bounds3D to_scene_bounds(const asset::GltfBounds3D& bounds) {
    return {.center = bounds.center, .half_extent = bounds.half_extent};
}

[[nodiscard]] Transform3D trs_transform_from_node(const asset::GltfNode& node) {
    return {.translation = node.translation, .rotation = node.rotation, .scale = node.scale};
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

[[nodiscard]] std::uint32_t scene_index_for_import(const asset::GltfAsset& asset,
                                                   const GltfSceneImportConfig& config) {
    const std::uint32_t scene_index =
        config.scene_index == asset::kInvalidAssetIndex ? asset.default_scene : config.scene_index;
    if (scene_index >= asset.scenes.size()) {
        throw std::runtime_error("glTF scene index is out of range");
    }
    return scene_index;
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
    return {.center = {center.x, center.y, center.z}, .half_extent = half_extent};
}

void accumulate_node_bounds(const GltfPreparedScene& prepared, std::uint32_t node_index,
                            const math::Mat4& parent_world, BoundsAccumulator& accumulator) {
    if (node_index >= prepared.nodes.size()) {
        throw std::runtime_error("glTF scene node index is out of range");
    }
    const GltfPreparedNode& node = prepared.nodes[node_index];
    const math::Mat4 world = parent_world * node.transform.affine_matrix();
    if (node.mesh_index != asset::kInvalidAssetIndex) {
        if (node.mesh_index >= prepared.meshes.size()) {
            throw std::runtime_error("glTF node mesh index is out of range");
        }
        for (const GltfPreparedMeshPrimitive& primitive :
             prepared.meshes[node.mesh_index].primitives) {
            accumulator.add(transform_bounds(primitive.local_bounds, world));
        }
    }
    for (const std::uint32_t child : node.children) {
        accumulate_node_bounds(prepared, child, world, accumulator);
    }
}

[[nodiscard]] const GltfDeformablePrimitive3D*
deformable_primitive(const GltfSceneImportResources& resources, std::uint32_t node_index,
                     std::uint32_t mesh_index, std::uint32_t primitive_index) {
    const auto position = std::find_if(
        resources.deformable_primitives.begin(), resources.deformable_primitives.end(),
        [node_index, mesh_index, primitive_index](const GltfDeformablePrimitive3D& primitive) {
            return primitive.node_index == node_index && primitive.mesh_index == mesh_index &&
                   primitive.primitive_index == primitive_index;
        });
    return position == resources.deformable_primitives.end() ? nullptr : &*position;
}

[[nodiscard]] render::MaterialHandle
remap_material_handle(render::MaterialHandle handle,
                      const std::vector<render::MaterialHandle>& activated) {
    if (!handle || handle.generation != 0U || handle.index == 0U ||
        handle.index > activated.size()) {
        throw std::runtime_error("glTF resident material handle is invalid");
    }
    return activated[handle.index - 1U];
}

[[nodiscard]] render::MeshHandle
remap_mesh_handle(render::MeshHandle handle, std::size_t static_mesh_count,
                  const std::vector<render::MeshHandle>& activated_static,
                  const std::vector<render::MeshHandle>& activated_deformation) {
    if (!handle || handle.generation != 0U || handle.index == 0U) {
        throw std::runtime_error("glTF resident mesh handle is invalid");
    }
    const std::size_t index = handle.index - 1U;
    if (index < static_mesh_count) {
        return activated_static.at(index);
    }
    return activated_deformation.at(index - static_mesh_count);
}

Entity create_node(SceneTransaction& transaction, const GltfPreparedScene& prepared,
                   GltfSceneImportResources& resources, GltfSceneImportResult& result,
                   std::uint32_t node_index, Entity parent) {
    if (node_index >= prepared.nodes.size()) {
        throw std::runtime_error("glTF scene node index is out of range");
    }
    const GltfPreparedNode& node = prepared.nodes[node_index];
    Entity entity = transaction.entities().create();
    result.node_entities.at(node_index) = entity;
    transaction.transforms3d().create(entity, node.transform, parent);

    if (node.mesh_index != asset::kInvalidAssetIndex) {
        if (node.mesh_index >= resources.mesh_primitives.size()) {
            throw std::runtime_error("glTF node mesh index is out of range");
        }
        std::vector<RenderablePrimitive3D> primitives;
        BoundsAccumulator bounds;
        bool has_deformable_primitive = false;
        for (const GltfImportedPrimitive3D& primitive :
             resources.mesh_primitives[node.mesh_index]) {
            render::MeshHandle render_mesh = primitive.mesh;
            if (const GltfDeformablePrimitive3D* deformable = deformable_primitive(
                    resources, node_index, node.mesh_index, primitive.primitive_index);
                deformable != nullptr) {
                has_deformable_primitive = true;
                render_mesh = deformable->output_mesh;
            }
            primitives.push_back({.mesh = render_mesh, .material = primitive.material});
            bounds.add(primitive.local_bounds);
        }
        if (!primitives.empty()) {
            transaction.renderables3d().create(entity,
                                               {.primitives = std::move(primitives),
                                                .local_bounds = bounds.bounds_or_default(),
                                                .culling_enabled = !has_deformable_primitive});
        }
    }

    for (const std::uint32_t child : node.children) {
        create_node(transaction, prepared, resources, result, child, entity);
    }
    return entity;
}

} // namespace

GltfPrimitiveDeformationKind
gltf_primitive_deformation_kind(const asset::GltfNode& node,
                                const asset::GltfMeshPrimitive& primitive) {
    const bool morph = !primitive.morph_targets.empty();
    const bool skin = node.skin_index != asset::kInvalidAssetIndex;
    if (morph && skin) {
        return GltfPrimitiveDeformationKind::MorphSkin;
    }
    if (morph) {
        return GltfPrimitiveDeformationKind::Morph;
    }
    return skin ? GltfPrimitiveDeformationKind::Skin : GltfPrimitiveDeformationKind::Static;
}

Transform3D gltf_node_transform_3d(const asset::GltfNode& node) {
    return node.has_matrix ? Transform3D::from_affine_matrix(node.local_matrix)
                           : trs_transform_from_node(node);
}

bool gltf_primitive_requires_deformation(GltfPrimitiveDeformationKind kind) {
    return kind != GltfPrimitiveDeformationKind::Static;
}

GltfPreparedScene prepare_gltf_scene(const asset::GltfAsset& asset, GltfSceneImportConfig config,
                                     GltfSceneImportCapabilities capabilities) {
    const std::uint32_t scene_index = scene_index_for_import(asset, config);
    GltfPreparedScene prepared;
    prepared.root_nodes = asset.scenes[scene_index].root_nodes;
    prepared.nodes.reserve(asset.nodes.size());
    for (const asset::GltfNode& node : asset.nodes) {
        prepared.nodes.push_back({
            .transform = gltf_node_transform_3d(node),
            .mesh_index = node.mesh_index,
            .skin_index = node.skin_index,
            .weights = node.weights,
            .children = node.children,
        });
    }
    prepare_gltf_materials(prepared, asset, config, capabilities);
    prepared.meshes.reserve(asset.meshes.size());
    for (const asset::GltfMesh& mesh : asset.meshes) {
        GltfPreparedMesh prepared_mesh;
        prepared_mesh.primitives.reserve(mesh.primitives.size());
        for (const asset::GltfMeshPrimitive& primitive : mesh.primitives) {
            if (primitive.material_index >= prepared.materials.size()) {
                throw std::runtime_error("glTF primitive material index is out of range");
            }
            prepared_mesh.primitives.push_back({
                .vertices = to_pbr_vertices(primitive.vertices),
                .indices = primitive.indices,
                .local_bounds = to_scene_bounds(primitive.local_bounds),
                .material_index = primitive.material_index,
            });
            prepared.triangle_count += static_cast<std::uint32_t>(primitive.indices.size() / 3U);
        }
        prepared.meshes.push_back(std::move(prepared_mesh));
    }
    BoundsAccumulator bounds;
    for (const std::uint32_t root : prepared.root_nodes) {
        accumulate_node_bounds(prepared, root, math::Mat4{1.0F}, bounds);
    }
    prepared.bounds = bounds.bounds_or_default();
    prepare_gltf_deformation_primitives(prepared, asset);
    return prepared;
}

GltfSceneImportResult activate_gltf_scene(Engine& engine, SceneTransaction& transaction,
                                          const GltfPreparedScene& prepared,
                                          GltfSceneResident&& resident,
                                          GltfSceneImportResources& resources,
                                          const GltfSceneImportConfig& config) {
    if (resources.active) {
        throw std::runtime_error("glTF scene import resources already contain an active import");
    }
    resources = std::move(resident.resources);
    resources.active = true;
    GltfSceneImportResult result;
    try {
        result.bounds = prepared.bounds;
        result.triangle_count = prepared.triangle_count;
        result.mesh_upload_byte_count = resident.mesh_upload_byte_count;
        result.mesh_upload_transfer_submission_count =
            resident.mesh_upload_transfer_submission_count;

        std::vector<render::MaterialHandle> activated_materials;
        activated_materials.reserve(resident.material_handles.size());
        result.material_handles.reserve(resident.material_handles.size());
        for (std::size_t index = 0; index < resident.material_handles.size(); ++index) {
            const render::MaterialHandle actual =
                engine.render_resources().create_material(prepared.materials.at(index).info);
            result.material_handles.push_back(actual);
            resources.materials.rebind(resident.material_handles[index], actual);
            activated_materials.push_back(actual);
        }
        if (result.material_handles.empty()) {
            throw std::runtime_error("glTF scene import requires at least one material");
        }
        result.first_material_handle = result.material_handles.front();

        std::vector<render::MeshHandle> activated_static;
        activated_static.reserve(resident.static_mesh_handles.size());
        result.mesh_handles.reserve(resident.static_mesh_handles.size() +
                                    resident.deformation_mesh_handles.size());
        std::size_t static_mesh_index = 0;
        for (std::size_t mesh_index = 0; mesh_index < prepared.meshes.size(); ++mesh_index) {
            const GltfPreparedMesh& mesh = prepared.meshes[mesh_index];
            for (std::size_t primitive_index = 0; primitive_index < mesh.primitives.size();
                 ++primitive_index) {
                const render::MeshHandle actual = engine.render_resources().create_mesh(
                    config.label_prefix + ".mesh." + std::to_string(mesh_index) + "." +
                    std::to_string(primitive_index));
                result.mesh_handles.push_back(actual);
                resources.meshes.rebind(resident.static_mesh_handles.at(static_mesh_index), actual);
                ++static_mesh_index;
                activated_static.push_back(actual);
            }
        }
        std::vector<render::MeshHandle> activated_deformation;
        activated_deformation.reserve(resident.deformation_mesh_handles.size());
        for (std::size_t index = 0; index < resident.deformation_mesh_handles.size(); ++index) {
            const GltfPreparedDeformationPrimitive& primitive =
                prepared.deformable_primitives.at(index);
            const render::MeshHandle actual = engine.render_resources().create_mesh(
                config.label_prefix + ".node." + std::to_string(primitive.node_index) + ".mesh." +
                std::to_string(primitive.mesh_index) + ".primitive." +
                std::to_string(primitive.primitive_index) + ".deformed");
            result.mesh_handles.push_back(actual);
            activated_deformation.push_back(actual);
        }

        for (std::vector<GltfImportedPrimitive3D>& mesh : resources.mesh_primitives) {
            for (GltfImportedPrimitive3D& primitive : mesh) {
                primitive.mesh = remap_mesh_handle(primitive.mesh, activated_static.size(),
                                                   activated_static, activated_deformation);
                primitive.material = remap_material_handle(primitive.material, activated_materials);
            }
        }
        for (GltfDeformablePrimitive3D& primitive : resources.deformable_primitives) {
            primitive.source_mesh =
                remap_mesh_handle(primitive.source_mesh, activated_static.size(), activated_static,
                                  activated_deformation);
            primitive.output_mesh =
                remap_mesh_handle(primitive.output_mesh, activated_static.size(), activated_static,
                                  activated_deformation);
            primitive.material = remap_material_handle(primitive.material, activated_materials);
        }
        for (GltfDeformationPrimitiveResources& primitive : resources.deformation.primitives) {
            primitive.primitive.source_mesh =
                remap_mesh_handle(primitive.primitive.source_mesh, activated_static.size(),
                                  activated_static, activated_deformation);
            primitive.primitive.output_mesh =
                remap_mesh_handle(primitive.primitive.output_mesh, activated_static.size(),
                                  activated_static, activated_deformation);
            primitive.primitive.material =
                remap_material_handle(primitive.primitive.material, activated_materials);
        }
        rebuild_gltf_deformation_frame_meshes(resources, config);

        result.node_entities.resize(prepared.nodes.size());
        result.root_entities.reserve(prepared.root_nodes.size());
        for (const std::uint32_t root : prepared.root_nodes) {
            result.root_entities.push_back(
                create_node(transaction, prepared, resources, result, root, {}));
        }
        return result;
    } catch (...) {
        destroy_gltf_scene_import(engine, resources, result);
        throw;
    }
}

GltfSceneImportResult import_gltf_scene(Engine& engine, SceneTransaction& transaction,
                                        const asset::GltfAsset& asset, const vulkan::Device& device,
                                        vulkan::GpuRuntime& gpu,
                                        GltfSceneImportResources& resources,
                                        GltfSceneImportConfig config) {
    auto prepared = std::make_shared<GltfPreparedScene>(prepare_gltf_scene(
        asset, config,
        {.supports_texture_compression_bc = device.supports_texture_compression_bc()}));
    std::shared_ptr<GltfSceneUploadSession> session =
        begin_gltf_scene_upload_session(prepared, config, gpu);
    try {
        while (!session->poll(gpu, true)) {
        }
        GltfSceneResident resident = session->take_resident();
        return activate_gltf_scene(engine, transaction, *prepared, std::move(resident), resources,
                                   config);
    } catch (...) {
        // Dropping an untaken session queues owner-only destruction after its
        // final same-queue ticket. The blocking poll path has already waited
        // that ticket on a failed generation before reaching here.
        session.reset();
        throw;
    }
}

void destroy_gltf_scene_import(Engine& engine, GltfSceneImportResources& resources,
                               GltfSceneImportResult& result) {
    if (!resources.active) {
        return;
    }
    for (const render::MaterialHandle material : result.material_handles) {
        if (resources.materials.contains_instance(material) ||
            resources.materials.contains_factors(material)) {
            resources.materials.erase(material);
        }
        if (engine.render_resources().is_alive(material)) {
            engine.render_resources().destroy_material(material);
        }
    }
    resources.materials.clear();
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
    resources.meshes.clear();
    resources.mesh_primitives.clear();
    resources.textures.clear();
    resources.default_textures.reset();
    resources.active = false;
    result = {};
}

void apply_gltf_rigid_animation_sample(SceneEditQueue& edits, const asset::GltfAsset& asset,
                                       const GltfSceneImportResult& result,
                                       const animation::GltfAnimationSample& sample) {
    if (result.node_entities.size() < asset.nodes.size()) {
        throw std::runtime_error("glTF import result does not contain node entity mapping");
    }
    const std::size_t count = std::min(sample.nodes.size(), asset.nodes.size());
    for (std::size_t node_index = 0; node_index < count; ++node_index) {
        const animation::GltfNodeAnimationSample& node_sample = sample.nodes[node_index];
        if (!node_sample.has_translation && !node_sample.has_rotation && !node_sample.has_scale) {
            continue;
        }
        const Entity entity = result.node_entities[node_index];
        if (!entity) {
            continue;
        }
        Transform3D transform = trs_transform_from_node(asset.nodes[node_index]);
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
