#include <cubey/engine/gltf_scene_importer.h>

#include <cubey/core/math.h>
#include <cubey/engine/engine.h>
#include <cubey/render/material.h>
#include <cubey/render/texture.h>
#include <cubey/scene/scene.h>
#include <cubey/scene/transform_3d.h>
#include <cubey/vulkan/sampler.h>

#include <vulkan/vulkan.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace cubey {
namespace {

struct TextureBinding {
    VkSampler sampler = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
};

struct TextureCacheKey {
    std::uint32_t texture_index = asset::kInvalidAssetIndex;
    asset::GltfTextureColorSpace color_space = asset::GltfTextureColorSpace::Linear;

    friend bool operator==(TextureCacheKey lhs, TextureCacheKey rhs) = default;
};

struct TextureCacheKeyHash {
    [[nodiscard]] std::size_t operator()(TextureCacheKey key) const noexcept {
        return (static_cast<std::size_t>(key.texture_index) << 1U) ^
               static_cast<std::size_t>(key.color_space);
    }
};

using TextureCache = std::unordered_map<TextureCacheKey, std::size_t, TextureCacheKeyHash>;

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

[[nodiscard]] VkFilter to_vk_filter(asset::GltfTextureFilter filter) {
    switch (filter) {
    case asset::GltfTextureFilter::Nearest:
        return VK_FILTER_NEAREST;
    case asset::GltfTextureFilter::Linear:
        return VK_FILTER_LINEAR;
    }
    return VK_FILTER_LINEAR;
}

[[nodiscard]] VkSamplerAddressMode to_vk_address_mode(asset::GltfTextureWrap wrap) {
    switch (wrap) {
    case asset::GltfTextureWrap::Repeat:
        return VK_SAMPLER_ADDRESS_MODE_REPEAT;
    case asset::GltfTextureWrap::ClampToEdge:
        return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    case asset::GltfTextureWrap::MirroredRepeat:
        return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
    }
    return VK_SAMPLER_ADDRESS_MODE_REPEAT;
}

[[nodiscard]] VkFormat image_format_for_color_space(asset::GltfTextureColorSpace color_space) {
    switch (color_space) {
    case asset::GltfTextureColorSpace::Srgb:
        return VK_FORMAT_R8G8B8A8_SRGB;
    case asset::GltfTextureColorSpace::Linear:
        return VK_FORMAT_R8G8B8A8_UNORM;
    }
    return VK_FORMAT_R8G8B8A8_UNORM;
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
        });
    }
    return result;
}

[[nodiscard]] render::Texture2D create_solid_texture(const vulkan::Device& device,
                                                     vulkan::GpuRuntime& gpu,
                                                     std::array<std::uint8_t, 4> color,
                                                     VkFormat format) {
    return render::create_uploaded_texture_2d(
        device, gpu,
        {
            .extent = {1, 1},
            .format = format,
            .rgba8 = std::span<const std::uint8_t>{color.data(), color.size()},
            .create_sampler = true,
            .sampler = {},
        });
}

void create_default_textures(const vulkan::Device& device, vulkan::GpuRuntime& gpu,
                             GltfSceneImportResources& resources) {
    if (resources.base_color_default.has_value()) {
        return;
    }
    resources.base_color_default.emplace(
        create_solid_texture(device, gpu, {255, 255, 255, 255}, VK_FORMAT_R8G8B8A8_SRGB));
    resources.metallic_roughness_default.emplace(
        create_solid_texture(device, gpu, {255, 255, 0, 255}, VK_FORMAT_R8G8B8A8_UNORM));
    resources.normal_default.emplace(
        create_solid_texture(device, gpu, {128, 128, 255, 255}, VK_FORMAT_R8G8B8A8_UNORM));
    resources.occlusion_default.emplace(
        create_solid_texture(device, gpu, {255, 255, 255, 255}, VK_FORMAT_R8G8B8A8_UNORM));
    resources.emissive_default.emplace(
        create_solid_texture(device, gpu, {0, 0, 0, 255}, VK_FORMAT_R8G8B8A8_SRGB));
}

[[nodiscard]] const render::Texture2D&
default_texture(const GltfSceneImportResources& resources, render::PbrMaterialBinding binding) {
    const std::optional<render::Texture2D>* texture = nullptr;
    switch (binding) {
    case render::PbrMaterialBinding::BaseColor:
        texture = &resources.base_color_default;
        break;
    case render::PbrMaterialBinding::MetallicRoughness:
        texture = &resources.metallic_roughness_default;
        break;
    case render::PbrMaterialBinding::Normal:
        texture = &resources.normal_default;
        break;
    case render::PbrMaterialBinding::Occlusion:
        texture = &resources.occlusion_default;
        break;
    case render::PbrMaterialBinding::Emissive:
        texture = &resources.emissive_default;
        break;
    }
    if (texture == nullptr || !texture->has_value()) {
        throw std::runtime_error("default PBR texture is not initialized");
    }
    return texture->value();
}

[[nodiscard]] TextureBinding texture_binding(const render::Texture2D& texture) {
    return {
        .sampler = texture.sampler().handle(),
        .view = texture.view(),
    };
}

[[nodiscard]] vulkan::SamplerConfig sampler_config_for_texture(const asset::GltfAsset& asset,
                                                               const asset::GltfTexture& texture) {
    if (texture.sampler_index >= asset.samplers.size()) {
        return {};
    }
    const asset::GltfSampler& sampler = asset.samplers[texture.sampler_index];
    return {
        .min_filter = to_vk_filter(sampler.min_filter),
        .mag_filter = to_vk_filter(sampler.mag_filter),
        .address_mode = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .address_mode_u = to_vk_address_mode(sampler.wrap_s),
        .address_mode_v = to_vk_address_mode(sampler.wrap_t),
        .address_mode_w = VK_SAMPLER_ADDRESS_MODE_REPEAT,
    };
}

[[nodiscard]] TextureBinding texture_binding_for_ref(
    const vulkan::Device& device, vulkan::GpuRuntime& gpu, GltfSceneImportResources& resources,
    const asset::GltfAsset& asset, const asset::GltfTextureRef& ref,
    asset::GltfTextureColorSpace color_space, render::PbrMaterialBinding fallback_slot,
    TextureCache& texture_cache) {
    if (!ref.has_value()) {
        return texture_binding(default_texture(resources, fallback_slot));
    }
    if (ref.texture_index >= asset.textures.size()) {
        throw std::runtime_error("glTF material texture index is out of range");
    }
    const TextureCacheKey cache_key{
        .texture_index = ref.texture_index,
        .color_space = color_space,
    };
    const auto cached = texture_cache.find(cache_key);
    if (cached != texture_cache.end()) {
        return texture_binding(resources.textures.at(cached->second));
    }

    const asset::GltfTexture& texture = asset.textures[ref.texture_index];
    if (texture.image_index >= asset.images.size()) {
        throw std::runtime_error("glTF texture image index is out of range");
    }
    const asset::GltfImage& image = asset.images[texture.image_index];
    render::Texture2D uploaded = render::create_uploaded_texture_2d(
        device, gpu,
        {
            .extent = {image.width, image.height},
            .format = image_format_for_color_space(color_space),
            .rgba8 = std::span<const std::uint8_t>{image.rgba8.data(), image.rgba8.size()},
            .create_sampler = true,
            .sampler = sampler_config_for_texture(asset, texture),
        });
    resources.textures.push_back(std::move(uploaded));
    texture_cache.emplace(cache_key, resources.textures.size() - 1U);
    return texture_binding(resources.textures.back());
}

void write_material_descriptors(const vulkan::Device& device, vulkan::GpuRuntime& gpu,
                                GltfSceneImportResources& resources,
                                const asset::GltfAsset& asset,
                                const asset::GltfMaterial& source,
                                render::MaterialInstance& instance,
                                TextureCache& texture_cache) {
    const TextureBinding base_color = texture_binding_for_ref(
        device, gpu, resources, asset, source.base_color_texture,
        asset::gltf_texture_color_space_for_base_color(), render::PbrMaterialBinding::BaseColor,
        texture_cache);
    const TextureBinding metallic_roughness = texture_binding_for_ref(
        device, gpu, resources, asset, source.metallic_roughness_texture,
        asset::GltfTextureColorSpace::Linear, render::PbrMaterialBinding::MetallicRoughness,
        texture_cache);
    const TextureBinding normal = texture_binding_for_ref(
        device, gpu, resources, asset, source.normal_texture, asset::GltfTextureColorSpace::Linear,
        render::PbrMaterialBinding::Normal, texture_cache);
    const TextureBinding occlusion = texture_binding_for_ref(
        device, gpu, resources, asset, source.occlusion_texture,
        asset::GltfTextureColorSpace::Linear, render::PbrMaterialBinding::Occlusion,
        texture_cache);
    const TextureBinding emissive = texture_binding_for_ref(
        device, gpu, resources, asset, source.emissive_texture, asset::GltfTextureColorSpace::Srgb,
        render::PbrMaterialBinding::Emissive, texture_cache);

    render::MaterialDescriptorWriter(instance.set())
        .combined_image_sampler(static_cast<std::uint32_t>(render::PbrMaterialBinding::BaseColor),
                                base_color.sampler, base_color.view)
        .combined_image_sampler(
            static_cast<std::uint32_t>(render::PbrMaterialBinding::MetallicRoughness),
            metallic_roughness.sampler, metallic_roughness.view)
        .combined_image_sampler(static_cast<std::uint32_t>(render::PbrMaterialBinding::Normal),
                                normal.sampler, normal.view)
        .combined_image_sampler(static_cast<std::uint32_t>(render::PbrMaterialBinding::Occlusion),
                                occlusion.sampler, occlusion.view)
        .combined_image_sampler(static_cast<std::uint32_t>(render::PbrMaterialBinding::Emissive),
                                emissive.sampler, emissive.view)
        .update(device);
}

[[nodiscard]] std::string import_label(const GltfSceneImportConfig& config, const char* kind,
                                       std::size_t index) {
    return config.label_prefix + "." + kind + "." + std::to_string(index);
}

void create_material_resources(Engine& engine, const vulkan::Device& device,
                               vulkan::GpuRuntime& gpu, GltfSceneImportResources& resources,
                               GltfSceneImportResult& result, const asset::GltfAsset& asset,
                               const GltfSceneImportConfig& config) {
    const render::MaterialPassInfo pass = render::pbr_forward_pass_info();
    TextureCache texture_cache;
    result.material_handles.reserve(asset.materials.size());
    for (std::size_t index = 0; index < asset.materials.size(); ++index) {
        const asset::GltfMaterial& source = asset.materials[index];
        const std::string label =
            source.label.empty() ? import_label(config, "material", index) : source.label;
        const render::MaterialHandle material =
            engine.render_resources().create_material(render::MaterialInfo{
                .label = label,
                .blend = source.alpha_mode == asset::GltfAlphaMode::Blend
                             ? render::MaterialBlendMode::AlphaBlend
                             : render::MaterialBlendMode::Opaque,
                .sort_key = static_cast<std::uint32_t>(index),
                .pass_mask = source.alpha_mode == asset::GltfAlphaMode::Blend
                                 ? render::material_pass_mask(
                                       render::MaterialPassKind::ForwardColor)
                                 : render::default_material_pass_mask(),
            });
        result.material_handles.push_back(material);
        resources.material_factors.emplace(
            material, render::PbrMaterialFactors{
                          .base_color_factor = source.base_color_factor,
                          .emissive_factor = source.emissive_factor,
                          .alpha_cutoff = source.alpha_mode == asset::GltfAlphaMode::Mask
                                              ? source.alpha_cutoff
                                              : 0.0F,
                          .metallic_factor = source.metallic_factor,
                          .roughness_factor = source.roughness_factor,
                          .normal_scale = source.normal_scale,
                          .occlusion_strength = source.occlusion_strength,
                      });
        render::MaterialInstance& instance =
            resources.material_instances.emplace(material, device,
                                                 render::MaterialInstanceConfig{
                                                     .material_pass = pass,
                                                     .descriptor_set = 1,
                                                 });
        write_material_descriptors(device, gpu, resources, asset, source, instance, texture_cache);
    }
    if (result.material_handles.empty()) {
        throw std::runtime_error("glTF scene import requires at least one material");
    }
    result.first_material_handle = result.material_handles.front();
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
                           const asset::GltfAsset& asset,
                           const GltfSceneImportConfig& config) {
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
            resources.meshes.emplace(mesh_handle, gpu,
                                     render::indexed_mesh_config(
                                         std::span<const render::PbrVertex>{vertices},
                                         std::span<const std::uint32_t>{primitive.indices}));
            result.mesh_handles.push_back(mesh_handle);
            result.triangle_count += static_cast<std::uint32_t>(primitive.indices.size() / 3U);
            primitives.push_back({
                .mesh = mesh_handle,
                .material = material_handle_for_index(result, primitive.material_index),
                .local_bounds = to_scene_bounds(primitive.local_bounds),
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
        for (const GltfImportedPrimitive3D& primitive : resources.mesh_primitives[node.mesh_index]) {
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

Entity create_node(SceneTransaction& transaction, const asset::GltfAsset& asset,
                   const GltfSceneImportResources& resources, std::uint32_t node_index,
                   Entity parent) {
    if (node_index >= asset.nodes.size()) {
        throw std::runtime_error("glTF scene node index is out of range");
    }
    const asset::GltfNode& node = asset.nodes[node_index];
    Entity entity = transaction.entities().create();
    transaction.transforms3d().create(entity, transform_from_node(node), parent);

    if (node.mesh_index != asset::kInvalidAssetIndex) {
        if (node.mesh_index >= resources.mesh_primitives.size()) {
            throw std::runtime_error("glTF node mesh index is out of range");
        }
        std::vector<RenderablePrimitive3D> primitives;
        BoundsAccumulator bounds;
        for (const GltfImportedPrimitive3D& primitive : resources.mesh_primitives[node.mesh_index]) {
            primitives.push_back({
                .mesh = primitive.mesh,
                .material = primitive.material,
            });
            bounds.add(primitive.local_bounds);
        }
        if (!primitives.empty()) {
            transaction.renderables3d().create(entity, Renderable3D{
                                                           .primitives = std::move(primitives),
                                                           .local_bounds =
                                                               bounds.bounds_or_default(),
                                                       });
        }
    }

    for (const std::uint32_t child : node.children) {
        create_node(transaction, asset, resources, child, entity);
    }
    return entity;
}

} // namespace

GltfSceneImportResult import_gltf_scene(Engine& engine, SceneTransaction& transaction,
                                        const asset::GltfAsset& asset,
                                        const vulkan::Device& device, vulkan::GpuRuntime& gpu,
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

        const asset::GltfScene& scene = asset.scenes[scene_index];
        result.root_entities.reserve(scene.root_nodes.size());
        for (const std::uint32_t root : scene.root_nodes) {
            result.root_entities.push_back(create_node(transaction, asset, resources, root, {}));
        }

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
    resources.occlusion_default.reset();
    resources.normal_default.reset();
    resources.metallic_roughness_default.reset();
    resources.base_color_default.reset();
    resources.active = false;
    result = {};
}

} // namespace cubey
