#include "gltf_scene_importer_internal.h"

#include <cubey/animation/gltf_animation.h>
#include <cubey/render/deformation.h>
#include <cubey/render/pbr.h>
#include <cubey/scene/scene.h>
#include <cubey/vulkan/descriptors.h>

#include <vulkan/vulkan.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <vector>

namespace cubey {
namespace {

[[nodiscard]] std::uint32_t binding(render::GpuDeformationBinding value) {
    return static_cast<std::uint32_t>(value);
}

[[nodiscard]] VkDeviceSize byte_size(std::size_t count, std::size_t element_size) {
    if (count == 0 || element_size == 0) {
        throw std::runtime_error("glTF deformation buffer byte size must be positive");
    }
    return static_cast<VkDeviceSize>(count * element_size);
}

template <typename T> [[nodiscard]] VkDeviceSize span_byte_size(std::span<const T> values) {
    return byte_size(values.size(), sizeof(T));
}

[[nodiscard]] vulkan::Buffer upload_storage_buffer(vulkan::GpuRuntime& gpu,
                                                   std::span<const render::PbrVertex> values) {
    return vulkan::upload_device_buffer(gpu, values.data(), span_byte_size(values),
                                        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
}

[[nodiscard]] vulkan::Buffer upload_storage_buffer(vulkan::GpuRuntime& gpu,
                                                   std::span<const float> values) {
    return vulkan::upload_device_buffer(gpu, values.data(), span_byte_size(values),
                                        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
}

[[nodiscard]] vulkan::Buffer upload_storage_buffer(vulkan::GpuRuntime& gpu,
                                                   std::span<const GltfSkinInfluence> values) {
    return vulkan::upload_device_buffer(gpu, values.data(), span_byte_size(values),
                                        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
}

template <typename T>
[[nodiscard]] vulkan::Buffer create_host_storage_buffer(const vulkan::Device& device,
                                                        std::span<const T> initial_values) {
    vulkan::Buffer buffer(device, vulkan::BufferConfig{
                                      .size = span_byte_size(initial_values),
                                      .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                      .memory_properties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                                           VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                  });
    buffer.upload(initial_values.data(), span_byte_size(initial_values));
    return buffer;
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

void append_vec3(std::vector<float>& values, math::Vec3 value) {
    values.push_back(value.x);
    values.push_back(value.y);
    values.push_back(value.z);
}

[[nodiscard]] math::Vec3 morph_delta_at(std::span<const math::Vec3> values,
                                        std::size_t vertex_index) {
    if (vertex_index >= values.size()) {
        return {0.0F, 0.0F, 0.0F};
    }
    return values[vertex_index];
}

void validate_skin_influences(const asset::GltfMeshPrimitive& primitive,
                              std::uint32_t joint_count) {
    if (joint_count == 0) {
        throw std::runtime_error("glTF skin requires at least one joint");
    }

    for (const asset::GltfVertex& vertex : primitive.vertices) {
        float weight_sum = 0.0F;
        for (int component = 0; component < 4; ++component) {
            const float weight = vertex.weights0[component];
            if (weight < 0.0F) {
                throw std::runtime_error("glTF skin influence weights must be non-negative");
            }
            weight_sum += weight;
            if (weight > 0.0F &&
                vertex.joints0[static_cast<std::size_t>(component)] >= joint_count) {
                throw std::runtime_error("glTF skin influence joint index is out of range");
            }
        }
        if (weight_sum <= 1.0e-6F) {
            throw std::runtime_error("glTF skin influence weights must be nonzero");
        }
    }
}

[[nodiscard]] math::Vec4 normalized_skin_weights(math::Vec4 weights) {
    const float sum = weights.x + weights.y + weights.z + weights.w;
    if (sum <= 1.0e-6F) {
        throw std::runtime_error("glTF skin influence weights must be nonzero");
    }
    return weights / sum;
}

[[nodiscard]] std::vector<float> pack_morph_targets(const asset::GltfMeshPrimitive& primitive) {
    if (primitive.morph_targets.empty()) {
        return {0.0F};
    }

    std::vector<float> result;
    result.reserve(primitive.morph_targets.size() * primitive.vertices.size() * 9U);
    for (const asset::GltfMorphTarget& target : primitive.morph_targets) {
        for (std::size_t vertex_index = 0; vertex_index < primitive.vertices.size();
             ++vertex_index) {
            append_vec3(result, morph_delta_at(target.position_deltas, vertex_index));
            append_vec3(result, morph_delta_at(target.normal_deltas, vertex_index));
            append_vec3(result, morph_delta_at(target.tangent_deltas, vertex_index));
        }
    }
    return result;
}

[[nodiscard]] std::vector<GltfSkinInfluence>
pack_skin_influences(const asset::GltfMeshPrimitive& primitive, bool has_skin,
                     std::uint32_t joint_count) {
    if (!has_skin) {
        return {GltfSkinInfluence{}};
    }
    validate_skin_influences(primitive, joint_count);

    std::vector<GltfSkinInfluence> result;
    result.reserve(primitive.vertices.size());
    for (const asset::GltfVertex& vertex : primitive.vertices) {
        result.push_back({
            .joints =
                {
                    static_cast<std::uint32_t>(vertex.joints0[0]),
                    static_cast<std::uint32_t>(vertex.joints0[1]),
                    static_cast<std::uint32_t>(vertex.joints0[2]),
                    static_cast<std::uint32_t>(vertex.joints0[3]),
                },
            .weights = normalized_skin_weights(vertex.weights0),
        });
    }
    return result;
}

void require_morph_weight_count(std::span<const float> weights, std::uint32_t morph_target_count) {
    if (!weights.empty() && weights.size() != morph_target_count) {
        throw std::runtime_error("glTF morph weight count must match primitive morph target count");
    }
}

[[nodiscard]] std::vector<float> default_morph_weights(const asset::GltfAsset& asset,
                                                       const GltfDeformablePrimitive3D& primitive,
                                                       std::uint32_t morph_target_count) {
    std::vector<float> weights(std::max(1U, morph_target_count), 0.0F);
    if (morph_target_count == 0) {
        return weights;
    }

    const asset::GltfNode& node = asset.nodes.at(primitive.node_index);
    const asset::GltfMesh& mesh = asset.meshes.at(primitive.mesh_index);
    const std::vector<float>& source_weights = node.weights.empty() ? mesh.weights : node.weights;
    require_morph_weight_count(source_weights, morph_target_count);
    const std::size_t copy_count = std::min<std::size_t>(source_weights.size(), morph_target_count);
    std::copy_n(source_weights.begin(), copy_count, weights.begin());
    return weights;
}

[[nodiscard]] std::vector<float> frame_morph_weights(const asset::GltfAsset& asset,
                                                     const GltfDeformablePrimitive3D& primitive,
                                                     std::uint32_t morph_target_count,
                                                     const animation::GltfAnimationSample* sample) {
    std::vector<float> weights = default_morph_weights(asset, primitive, morph_target_count);
    if (sample == nullptr || primitive.node_index >= sample->nodes.size()) {
        return weights;
    }

    const animation::GltfNodeAnimationSample& node_sample = sample->nodes[primitive.node_index];
    if (!node_sample.has_weights) {
        return weights;
    }
    require_morph_weight_count(node_sample.weights, morph_target_count);
    const std::size_t copy_count =
        std::min<std::size_t>(node_sample.weights.size(), morph_target_count);
    std::copy_n(node_sample.weights.begin(), copy_count, weights.begin());
    return weights;
}

[[nodiscard]] std::vector<math::Mat4> default_joint_palette(std::uint32_t joint_count) {
    return std::vector<math::Mat4>(std::max(1U, joint_count), math::Mat4{1.0F});
}

[[nodiscard]] std::vector<math::Mat4> node_world_matrices(const asset::GltfAsset& asset,
                                                          const GltfSceneImportResult& result,
                                                          const SceneReadView& scene_view) {
    std::vector<math::Mat4> matrices(asset.nodes.size(), math::Mat4{1.0F});
    const std::size_t node_count = std::min(asset.nodes.size(), result.node_entities.size());
    for (std::size_t node_index = 0; node_index < node_count; ++node_index) {
        const Entity entity = result.node_entities[node_index];
        if (!entity) {
            continue;
        }
        const TransformInstance3D transform = scene_view.transforms3d().instance(entity);
        matrices[node_index] = scene_view.transforms3d().world_affine_matrix(transform);
    }
    return matrices;
}

void upload_frame_morph_weights(GltfDeformationPrimitiveResources& resource,
                                render::FrameSlot frame_slot, std::span<const float> weights) {
    if (frame_slot.index >= resource.morph_weights.size()) {
        throw std::runtime_error("glTF morph weight frame slot is out of range");
    }
    resource.morph_weights.at(frame_slot.index).upload(weights.data(), span_byte_size(weights));
}

void upload_frame_joint_palette(GltfDeformationPrimitiveResources& resource,
                                render::FrameSlot frame_slot, std::span<const math::Mat4> joints) {
    if (frame_slot.index >= resource.joint_palettes.size()) {
        throw std::runtime_error("glTF joint palette frame slot is out of range");
    }
    resource.joint_palettes.at(frame_slot.index).upload(joints.data(), span_byte_size(joints));
}

[[nodiscard]] std::uint32_t deformation_flags(GltfPrimitiveDeformationKind kind) {
    std::uint32_t flags = 0;
    if (kind == GltfPrimitiveDeformationKind::Morph ||
        kind == GltfPrimitiveDeformationKind::MorphSkin) {
        flags |= static_cast<std::uint32_t>(render::GpuDeformationFlags::Morph);
    }
    if (kind == GltfPrimitiveDeformationKind::Skin ||
        kind == GltfPrimitiveDeformationKind::MorphSkin) {
        flags |= static_cast<std::uint32_t>(render::GpuDeformationFlags::Skin);
    }
    return flags;
}

[[nodiscard]] bool deformation_has_skin(GltfPrimitiveDeformationKind kind) {
    return kind == GltfPrimitiveDeformationKind::Skin ||
           kind == GltfPrimitiveDeformationKind::MorphSkin;
}

void update_deformation_descriptors(const vulkan::Device& device,
                                    GltfDeformationPrimitiveResources& resource) {
    if (!resource.base_vertices.has_value() || !resource.morph_targets.has_value() ||
        !resource.skin_influences.has_value() || resource.descriptor_sets == nullptr) {
        throw std::runtime_error("glTF deformation descriptors require initialized buffers");
    }

    for (std::uint32_t frame_index = 0; frame_index < resource.descriptor_sets->size();
         ++frame_index) {
        const VkDescriptorSet set = resource.descriptor_sets->set(frame_index);
        vulkan::DescriptorWriteBatch writes;
        writes
            .storage_buffer(set, binding(render::GpuDeformationBinding::BaseVertices),
                            resource.base_vertices->handle(), resource.base_vertices->size())
            .storage_buffer(set, binding(render::GpuDeformationBinding::MorphTargets),
                            resource.morph_targets->handle(), resource.morph_targets->size())
            .storage_buffer(set, binding(render::GpuDeformationBinding::MorphWeights),
                            resource.morph_weights.at(frame_index).handle(),
                            resource.morph_weights.at(frame_index).size())
            .storage_buffer(set, binding(render::GpuDeformationBinding::SkinInfluences),
                            resource.skin_influences->handle(), resource.skin_influences->size())
            .storage_buffer(set, binding(render::GpuDeformationBinding::JointPalette),
                            resource.joint_palettes.at(frame_index).handle(),
                            resource.joint_palettes.at(frame_index).size())
            .storage_buffer(set, binding(render::GpuDeformationBinding::OutputVertices),
                            resource.output_meshes.at(frame_index).vertex_buffer().handle(),
                            resource.output_meshes.at(frame_index).vertex_buffer().size());
        writes.update(device);
    }
}

void create_deformation_primitive_resources(const vulkan::Device& device, vulkan::GpuRuntime& gpu,
                                            GltfSceneImportResources& resources,
                                            const asset::GltfAsset& asset,
                                            const GltfDeformablePrimitive3D& primitive_record,
                                            const GltfSceneImportConfig& config) {
    if (primitive_record.mesh_index >= asset.meshes.size()) {
        throw std::runtime_error("glTF deformation primitive mesh index is out of range");
    }
    const asset::GltfMesh& mesh = asset.meshes[primitive_record.mesh_index];
    if (primitive_record.primitive_index >= mesh.primitives.size()) {
        throw std::runtime_error("glTF deformation primitive index is out of range");
    }
    if (primitive_record.node_index >= asset.nodes.size()) {
        throw std::runtime_error("glTF deformation primitive node index is out of range");
    }

    const asset::GltfMeshPrimitive& primitive = mesh.primitives[primitive_record.primitive_index];
    if (primitive.vertices.empty() || primitive.indices.empty()) {
        throw std::runtime_error("glTF deformation primitive requires indexed vertices");
    }

    const bool has_skin = deformation_has_skin(primitive_record.deformation);
    std::uint32_t joint_count = 0;
    if (has_skin) {
        if (primitive_record.skin_index >= asset.skins.size()) {
            throw std::runtime_error("glTF deformation primitive skin index is out of range");
        }
        joint_count =
            static_cast<std::uint32_t>(asset.skins[primitive_record.skin_index].joints.size());
    }

    GltfDeformationPrimitiveResources& resource = resources.deformation.primitives.emplace_back();
    resource.primitive = primitive_record;
    resource.push_constants = {
        .vertex_count = static_cast<std::uint32_t>(primitive.vertices.size()),
        .morph_target_count = static_cast<std::uint32_t>(primitive.morph_targets.size()),
        .joint_count = joint_count,
        .flags = deformation_flags(primitive_record.deformation),
    };

    const std::vector<render::PbrVertex> base_vertices = to_pbr_vertices(primitive.vertices);
    const std::vector<float> morph_targets = pack_morph_targets(primitive);
    const std::vector<GltfSkinInfluence> skin_influences =
        pack_skin_influences(primitive, has_skin, joint_count);
    resource.base_vertices =
        upload_storage_buffer(gpu, std::span<const render::PbrVertex>{base_vertices});
    resource.morph_targets = upload_storage_buffer(gpu, std::span<const float>{morph_targets});
    resource.skin_influences =
        upload_storage_buffer(gpu, std::span<const GltfSkinInfluence>{skin_influences});

    const std::vector<float> initial_weights =
        default_morph_weights(asset, primitive_record, resource.push_constants.morph_target_count);
    const std::vector<math::Mat4> initial_joints = default_joint_palette(joint_count);
    std::vector<render::PbrVertex> output_vertices(primitive.vertices.size());
    resource.morph_weights.reserve(config.frame_slot_count);
    resource.joint_palettes.reserve(config.frame_slot_count);
    resource.output_meshes.reserve(config.frame_slot_count);
    for (std::uint32_t frame_index = 0; frame_index < config.frame_slot_count; ++frame_index) {
        resource.morph_weights.push_back(
            create_host_storage_buffer(device, std::span<const float>{initial_weights}));
        resource.joint_palettes.push_back(
            create_host_storage_buffer(device, std::span<const math::Mat4>{initial_joints}));
        resource.output_meshes.emplace_back(
            gpu, render::indexed_mesh_config(std::span<const render::PbrVertex>{output_vertices},
                                             std::span<const std::uint32_t>{primitive.indices},
                                             VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
                                                 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT));
        resources.deformation.frame_meshes.bind(
            render::FrameSlot{
                .index = frame_index,
                .count = config.frame_slot_count,
            },
            primitive_record.output_mesh, &resource.output_meshes.back());
    }

    resource.descriptor_sets = std::make_unique<vulkan::DescriptorSetArray>(
        device, render::gpu_deformation_descriptor_set_info(config.frame_slot_count));
    update_deformation_descriptors(device, resource);
}

} // namespace

void create_deformation_resources(const vulkan::Device& device, vulkan::GpuRuntime& gpu,
                                  GltfSceneImportResources& resources,
                                  const asset::GltfAsset& asset,
                                  const GltfSceneImportConfig& config) {
    resources.deformation = {};
    if (resources.deformable_primitives.empty()) {
        return;
    }

    resources.deformation.frame_meshes.resize(config.frame_slot_count);
    resources.deformation.primitives.reserve(resources.deformable_primitives.size());
    for (const GltfDeformablePrimitive3D& primitive : resources.deformable_primitives) {
        create_deformation_primitive_resources(device, gpu, resources, asset, primitive, config);
    }
    if (config.deformation_compute_shader.empty()) {
        throw std::runtime_error("glTF deformation resources require a compute shader");
    }
    const std::array<VkDescriptorSetLayout, 1> descriptor_layouts{
        resources.deformation.primitives.front().descriptor_sets->layout(),
    };
    resources.deformation.pipeline = std::make_unique<render::ComputePipelineResource>(
        device, render::gpu_deformation_pipeline_config(config.deformation_compute_shader,
                                                        descriptor_layouts));
}

std::vector<render::GpuDeformationCommand>
gltf_deformation_commands_for_frame(const GltfSceneImportResources& resources,
                                    render::FrameSlot frame_slot) {
    if (resources.deformation.primitives.empty()) {
        return {};
    }
    render::validate_frame_slot(frame_slot);
    if (resources.deformation.pipeline == nullptr) {
        throw std::runtime_error("glTF deformation commands require a compute pipeline");
    }

    std::vector<render::GpuDeformationCommand> commands;
    commands.reserve(resources.deformation.primitives.size());
    for (const GltfDeformationPrimitiveResources& resource : resources.deformation.primitives) {
        if (resource.descriptor_sets == nullptr) {
            throw std::runtime_error("glTF deformation command requires descriptor sets");
        }
        if (frame_slot.count != resource.descriptor_sets->size() ||
            frame_slot.index >= resource.output_meshes.size()) {
            throw std::runtime_error("glTF deformation command frame slot is out of range");
        }
        commands.push_back({
            .mesh = resource.primitive.output_mesh,
            .output_mesh = &resource.output_meshes.at(frame_slot.index),
            .pipeline = resources.deformation.pipeline.get(),
            .descriptor_set = resource.descriptor_sets->set(frame_slot.index),
            .push_constants = resource.push_constants,
        });
    }
    return commands;
}

void update_gltf_deformation_frame(GltfSceneImportResources& resources,
                                   const asset::GltfAsset& asset,
                                   const GltfSceneImportResult& result,
                                   const SceneReadView& scene_view, render::FrameSlot frame_slot,
                                   const animation::GltfAnimationSample* sample) {
    if (resources.deformation.primitives.empty()) {
        return;
    }
    render::validate_frame_slot(frame_slot);

    const std::vector<math::Mat4> world_matrices = node_world_matrices(asset, result, scene_view);
    for (GltfDeformationPrimitiveResources& resource : resources.deformation.primitives) {
        if (frame_slot.count != resource.morph_weights.size() ||
            frame_slot.count != resource.joint_palettes.size()) {
            throw std::runtime_error("glTF deformation frame slot count does not match resources");
        }

        const std::vector<float> weights = frame_morph_weights(
            asset, resource.primitive, resource.push_constants.morph_target_count, sample);
        upload_frame_morph_weights(resource, frame_slot, weights);

        std::vector<math::Mat4> joints = default_joint_palette(resource.push_constants.joint_count);
        if (deformation_has_skin(resource.primitive.deformation)) {
            const asset::GltfSkin& skin = asset.skins.at(resource.primitive.skin_index);
            joints = animation::compute_gltf_joint_palette(skin, world_matrices,
                                                           resource.primitive.node_index);
            if (joints.empty()) {
                joints = default_joint_palette(0);
            }
        }
        upload_frame_joint_palette(resource, frame_slot, joints);
    }
}

} // namespace cubey
