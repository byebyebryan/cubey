#include <cubey/asset/gltf_asset.h>

#include "gltf_asset_internal.h"

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/matrix_decompose.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <memory>
#include <span>
#include <stdexcept>
#include <string_view>
#include <utility>

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wold-style-cast"
#pragma GCC diagnostic ignored "-Wsign-conversion"
#pragma GCC diagnostic ignored "-Wshadow"
#endif
#define CGLTF_IMPLEMENTATION
#include <cgltf.h>
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace cubey::asset {
using namespace gltf_internal;
namespace {

[[nodiscard]] std::uint32_t checked_index(std::ptrdiff_t index, const char* label) {
    if (index < 0 || static_cast<std::uint64_t>(index) >
                         static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max())) {
        throw gltf_error(std::string(label) + " index is out of range");
    }
    return static_cast<std::uint32_t>(index);
}

template <typename T>
[[nodiscard]] std::uint32_t pointer_index(const T* pointer, const T* base, cgltf_size count,
                                          const char* label) {
    if (pointer == nullptr) {
        return kInvalidAssetIndex;
    }
    if (base == nullptr || pointer < base || pointer >= base + count) {
        throw gltf_error(std::string(label) + " pointer is outside the glTF data");
    }
    return checked_index(pointer - base, label);
}

[[nodiscard]] GltfTextureFilter min_filter(cgltf_int value) noexcept {
    switch (value) {
    case 9728:
    case 9984:
    case 9986:
        return GltfTextureFilter::Nearest;
    default:
        return GltfTextureFilter::Linear;
    }
}

[[nodiscard]] GltfTextureFilter mag_filter(cgltf_int value) noexcept {
    return value == 9728 ? GltfTextureFilter::Nearest : GltfTextureFilter::Linear;
}

[[nodiscard]] GltfTextureWrap wrap_mode(cgltf_int value) noexcept {
    switch (value) {
    case 33071:
        return GltfTextureWrap::ClampToEdge;
    case 33648:
        return GltfTextureWrap::MirroredRepeat;
    default:
        return GltfTextureWrap::Repeat;
    }
}

[[nodiscard]] GltfSampler load_sampler(const cgltf_sampler& sampler) {
    return {
        .label = label_or_empty(sampler.name),
        .min_filter = min_filter(sampler.min_filter),
        .mag_filter = mag_filter(sampler.mag_filter),
        .wrap_s = wrap_mode(sampler.wrap_s),
        .wrap_t = wrap_mode(sampler.wrap_t),
    };
}

[[nodiscard]] GltfTextureRef load_texture_ref(const cgltf_texture_view& view,
                                              const cgltf_texture* texture_base,
                                              cgltf_size texture_count) {
    GltfTextureRef ref{
        .texture_index = pointer_index(view.texture, texture_base, texture_count, "texture"),
        .texcoord = static_cast<std::uint32_t>(view.texcoord),
    };
    if (view.has_transform != 0) {
        ref.offset = {view.transform.offset[0], view.transform.offset[1]};
        ref.rotation = view.transform.rotation;
        ref.scale = {view.transform.scale[0], view.transform.scale[1]};
        if (view.transform.has_texcoord != 0) {
            ref.texcoord = static_cast<std::uint32_t>(view.transform.texcoord);
        }
    }
    if (ref.has_value() && ref.texcoord > 1U) {
        throw gltf_error("texture coordinate sets above TEXCOORD_1 are not supported");
    }
    return ref;
}

[[nodiscard]] bool normalized_unsigned_accessor(const cgltf_accessor* accessor) noexcept {
    return accessor != nullptr && accessor->normalized != 0 &&
           (accessor->component_type == cgltf_component_type_r_8u ||
            accessor->component_type == cgltf_component_type_r_16u);
}

void require_optional_texcoord_accessor(const cgltf_accessor* accessor, const char* label) {
    if (accessor == nullptr) {
        return;
    }
    if (cgltf_num_components(accessor->type) != 2) {
        throw gltf_error(std::string(label) + " attribute has unsupported component count");
    }
    if (accessor->component_type != cgltf_component_type_r_32f &&
        !normalized_unsigned_accessor(accessor)) {
        throw gltf_error(std::string(label) +
                         " attribute must use FLOAT or normalized unsigned components");
    }
}

void require_optional_color_accessor(const cgltf_accessor* accessor, const char* label) {
    if (accessor == nullptr) {
        return;
    }
    const cgltf_size component_count = cgltf_num_components(accessor->type);
    if (component_count != 3 && component_count != 4) {
        throw gltf_error(std::string(label) + " attribute has unsupported component count");
    }
    if (accessor->component_type != cgltf_component_type_r_32f &&
        !normalized_unsigned_accessor(accessor)) {
        throw gltf_error(std::string(label) +
                         " attribute must use FLOAT or normalized unsigned components");
    }
}

[[nodiscard]] std::vector<float> read_float_accessor_values(const cgltf_accessor* accessor,
                                                            cgltf_size component_count,
                                                            const char* label);

[[nodiscard]] std::vector<math::Vec4> read_color_accessor_values(const cgltf_accessor* accessor,
                                                                 const char* label) {
    require_optional_color_accessor(accessor, label);
    if (accessor == nullptr) {
        return {};
    }
    const cgltf_size component_count = cgltf_num_components(accessor->type);
    const std::vector<float> unpacked =
        read_float_accessor_values(accessor, component_count, label);
    std::vector<math::Vec4> values;
    values.reserve(accessor->count);
    for (cgltf_size i = 0; i < accessor->count; ++i) {
        const std::size_t offset = static_cast<std::size_t>(i * component_count);
        values.push_back({
            unpacked[offset + 0U],
            unpacked[offset + 1U],
            unpacked[offset + 2U],
            component_count == 4 ? unpacked[offset + 3U] : 1.0F,
        });
    }
    return values;
}

[[nodiscard]] GltfAlphaMode load_alpha_mode(cgltf_alpha_mode mode) {
    switch (mode) {
    case cgltf_alpha_mode_mask:
        return GltfAlphaMode::Mask;
    case cgltf_alpha_mode_blend:
        return GltfAlphaMode::Blend;
    case cgltf_alpha_mode_opaque:
    default:
        return GltfAlphaMode::Opaque;
    }
}

[[nodiscard]] GltfMaterial default_material() {
    return {
        .label = "default",
        .base_color_factor = {1.0F, 1.0F, 1.0F, 1.0F},
        .metallic_factor = 1.0F,
        .roughness_factor = 1.0F,
    };
}

[[nodiscard]] float reflectance_from_ior(float ior) {
    const float clamped_ior = std::max(ior, 1.0F);
    const float root_f0 = (clamped_ior - 1.0F) / (clamped_ior + 1.0F);
    return std::clamp(std::sqrt((root_f0 * root_f0) / 0.16F), 0.0F, 1.0F);
}

[[nodiscard]] GltfMaterial load_material(const cgltf_material& material,
                                         const cgltf_texture* texture_base,
                                         cgltf_size texture_count) {
    const cgltf_pbr_metallic_roughness& pbr = material.pbr_metallic_roughness;
    const math::Vec3 specular_color =
        material.has_specular != 0
            ? math::Vec3{
                  material.specular.specular_color_factor[0],
                  material.specular.specular_color_factor[1],
                  material.specular.specular_color_factor[2],
              }
            : math::Vec3{1.0F, 1.0F, 1.0F};
    const float emissive_strength =
        material.has_emissive_strength != 0 ? material.emissive_strength.emissive_strength : 1.0F;
    return {
        .label = label_or_empty(material.name),
        .base_color_factor =
            {
                pbr.base_color_factor[0],
                pbr.base_color_factor[1],
                pbr.base_color_factor[2],
                pbr.base_color_factor[3],
            },
        .metallic_factor = pbr.metallic_factor,
        .roughness_factor = pbr.roughness_factor,
        .specular_color_factor = specular_color,
        .specular_factor = material.has_specular != 0 ? material.specular.specular_factor : 1.0F,
        .specular_texture =
            material.has_specular != 0
                ? load_texture_ref(material.specular.specular_texture, texture_base, texture_count)
                : GltfTextureRef{},
        .specular_color_texture = material.has_specular != 0
                                      ? load_texture_ref(material.specular.specular_color_texture,
                                                         texture_base, texture_count)
                                      : GltfTextureRef{},
        .reflectance = material.has_ior != 0 ? reflectance_from_ior(material.ior.ior) : 0.5F,
        .emissive_factor =
            {
                material.emissive_factor[0] * emissive_strength,
                material.emissive_factor[1] * emissive_strength,
                material.emissive_factor[2] * emissive_strength,
            },
        .normal_scale = material.normal_texture.scale,
        .occlusion_strength = material.occlusion_texture.scale,
        .base_color_texture = load_texture_ref(pbr.base_color_texture, texture_base, texture_count),
        .metallic_roughness_texture =
            load_texture_ref(pbr.metallic_roughness_texture, texture_base, texture_count),
        .normal_texture = load_texture_ref(material.normal_texture, texture_base, texture_count),
        .occlusion_texture =
            load_texture_ref(material.occlusion_texture, texture_base, texture_count),
        .emissive_texture =
            load_texture_ref(material.emissive_texture, texture_base, texture_count),
        .alpha_mode = load_alpha_mode(material.alpha_mode),
        .alpha_cutoff = material.alpha_cutoff,
        .double_sided = material.double_sided != 0,
        .unlit = material.unlit != 0,
    };
}

[[nodiscard]] const cgltf_accessor* find_attribute(std::span<const cgltf_attribute> attributes,
                                                   cgltf_attribute_type type,
                                                   cgltf_int index = 0) noexcept {
    for (const cgltf_attribute& attribute : attributes) {
        if (attribute.type == type && attribute.index == index) {
            return attribute.data;
        }
    }
    return nullptr;
}

[[nodiscard]] const cgltf_accessor* find_attribute(const cgltf_primitive& primitive,
                                                   cgltf_attribute_type type,
                                                   cgltf_int index = 0) noexcept {
    return find_attribute(
        std::span<const cgltf_attribute>{primitive.attributes, primitive.attributes_count}, type,
        index);
}

void require_accessor_components(const cgltf_accessor* accessor, cgltf_size components,
                                 const char* label) {
    if (accessor == nullptr) {
        throw gltf_error(std::string("primitive is missing required ") + label + " attribute");
    }
    if (cgltf_num_components(accessor->type) != components) {
        throw gltf_error(std::string(label) + " attribute has unsupported component count");
    }
    if (accessor->component_type != cgltf_component_type_r_32f) {
        throw gltf_error(std::string(label) + " attribute must use FLOAT components");
    }
}

void require_optional_accessor_components(const cgltf_accessor* accessor, cgltf_size components,
                                          const char* label) {
    if (accessor == nullptr) {
        return;
    }
    if (cgltf_num_components(accessor->type) != components) {
        throw gltf_error(std::string(label) + " attribute has unsupported component count");
    }
}

void require_float_accessor(const cgltf_accessor* accessor, cgltf_type type, const char* label) {
    if (accessor == nullptr) {
        throw gltf_error(std::string(label) + " accessor is missing");
    }
    if (accessor->type != type) {
        throw gltf_error(std::string(label) + " accessor has unsupported type");
    }
    if (accessor->component_type != cgltf_component_type_r_32f) {
        throw gltf_error(std::string(label) + " accessor must use FLOAT components");
    }
}

void require_optional_float_accessor(const cgltf_accessor* accessor, cgltf_type type,
                                     const char* label) {
    if (accessor == nullptr) {
        return;
    }
    require_float_accessor(accessor, type, label);
}

void require_optional_morph_accessor_count(const cgltf_accessor* accessor, cgltf_size vertex_count,
                                           const char* label) {
    if (accessor == nullptr) {
        return;
    }
    if (accessor->count != vertex_count) {
        throw gltf_error(std::string(label) + " morph target count must match POSITION count");
    }
}

[[nodiscard]] std::vector<float> read_float_accessor_values(const cgltf_accessor* accessor,
                                                            cgltf_size component_count,
                                                            const char* label) {
    if (accessor == nullptr) {
        throw gltf_error(std::string(label) + " accessor is missing");
    }
    std::vector<float> values(accessor->count * component_count);
    if (values.empty()) {
        return values;
    }
    const cgltf_size read_count =
        cgltf_accessor_unpack_floats(accessor, values.data(), values.size());
    if (read_count != values.size()) {
        throw gltf_error(std::string("failed to read ") + label + " accessor");
    }
    return values;
}

[[nodiscard]] math::Vec2 vec2_at(std::span<const float> values, std::size_t index) {
    const std::size_t offset = index * 2U;
    return {values[offset + 0U], values[offset + 1U]};
}

[[nodiscard]] math::Vec3 vec3_at(std::span<const float> values, std::size_t index) {
    const std::size_t offset = index * 3U;
    return {values[offset + 0U], values[offset + 1U], values[offset + 2U]};
}

[[nodiscard]] math::Vec4 vec4_at(std::span<const float> values, std::size_t index) {
    const std::size_t offset = index * 4U;
    return {values[offset + 0U], values[offset + 1U], values[offset + 2U], values[offset + 3U]};
}

[[nodiscard]] std::array<std::uint16_t, 4> u16_vec4_at(std::span<const float> values,
                                                       std::size_t index, const char* label) {
    const std::size_t offset = index * 4U;
    std::array<std::uint16_t, 4> result{};
    for (std::size_t component = 0; component < result.size(); ++component) {
        const float value = values[offset + component];
        if (value < 0.0F || value > static_cast<float>(std::numeric_limits<std::uint16_t>::max()) ||
            std::floor(value) != value) {
            throw gltf_error(std::string(label) + " value is out of range");
        }
        result[component] = static_cast<std::uint16_t>(value);
    }
    return result;
}

[[nodiscard]] std::vector<std::array<std::uint16_t, 4>>
read_u16_vec4_accessor_values(const cgltf_accessor* accessor, const char* label) {
    const std::vector<float> values = read_float_accessor_values(accessor, 4, label);
    std::vector<std::array<std::uint16_t, 4>> result;
    result.reserve(accessor->count);
    for (cgltf_size index = 0; index < accessor->count; ++index) {
        result.push_back(u16_vec4_at(values, index, label));
    }
    return result;
}

[[nodiscard]] math::Mat4 read_mat4(const cgltf_accessor* accessor, cgltf_size index) {
    cgltf_float values[16]{};
    if (cgltf_accessor_read_float(accessor, index, values, 16) == 0) {
        const std::vector<float> unpacked = read_float_accessor_values(accessor, 16, "MAT4");
        std::memcpy(values, unpacked.data() + (index * 16U), sizeof(values));
    }

    math::Mat4 matrix{1.0F};
    std::memcpy(&matrix[0][0], values, sizeof(values));
    return matrix;
}

[[nodiscard]] GltfBounds3D bounds_for_positions(std::span<const GltfVertex> vertices) {
    if (vertices.empty()) {
        return {};
    }

    math::Vec3 min_position = vertices.front().position;
    math::Vec3 max_position = vertices.front().position;
    for (const GltfVertex& vertex : vertices) {
        min_position = glm::min(min_position, vertex.position);
        max_position = glm::max(max_position, vertex.position);
    }

    return {
        .center = (min_position + max_position) * 0.5F,
        .half_extent = (max_position - min_position) * 0.5F,
    };
}

void generate_tangents(GltfMeshPrimitive& primitive) {
    std::vector<math::Vec3> accum(primitive.vertices.size(), math::Vec3{0.0F, 0.0F, 0.0F});
    for (std::size_t i = 0; i + 2 < primitive.indices.size(); i += 3) {
        const std::uint32_t i0 = primitive.indices[i + 0];
        const std::uint32_t i1 = primitive.indices[i + 1];
        const std::uint32_t i2 = primitive.indices[i + 2];
        if (i0 >= primitive.vertices.size() || i1 >= primitive.vertices.size() ||
            i2 >= primitive.vertices.size()) {
            throw gltf_error("primitive index is out of vertex range");
        }
        const GltfVertex& v0 = primitive.vertices[i0];
        const GltfVertex& v1 = primitive.vertices[i1];
        const GltfVertex& v2 = primitive.vertices[i2];
        const math::Vec3 edge1 = v1.position - v0.position;
        const math::Vec3 edge2 = v2.position - v0.position;
        const math::Vec2 delta_uv1 = v1.texcoord0 - v0.texcoord0;
        const math::Vec2 delta_uv2 = v2.texcoord0 - v0.texcoord0;
        const float determinant = delta_uv1.x * delta_uv2.y - delta_uv1.y * delta_uv2.x;
        if (std::abs(determinant) < 1.0e-6F) {
            continue;
        }
        const math::Vec3 tangent = (edge1 * delta_uv2.y - edge2 * delta_uv1.y) / determinant;
        accum[i0] += tangent;
        accum[i1] += tangent;
        accum[i2] += tangent;
    }

    for (std::size_t i = 0; i < primitive.vertices.size(); ++i) {
        const math::Vec3 normal = primitive.vertices[i].normal;
        math::Vec3 tangent = accum[i] - normal * glm::dot(normal, accum[i]);
        if (glm::length(tangent) < 1.0e-6F) {
            tangent = std::abs(normal.y) < 0.9F ? glm::cross(normal, math::Vec3{0.0F, 1.0F, 0.0F})
                                                : glm::cross(normal, math::Vec3{1.0F, 0.0F, 0.0F});
        }
        tangent = glm::normalize(tangent);
        primitive.vertices[i].tangent = {tangent.x, tangent.y, tangent.z, 1.0F};
    }
}

[[nodiscard]] math::Vec3 triangle_normal(const GltfVertex& v0, const GltfVertex& v1,
                                         const GltfVertex& v2) {
    const math::Vec3 edge1 = v1.position - v0.position;
    const math::Vec3 edge2 = v2.position - v0.position;
    const math::Vec3 normal = glm::cross(edge1, edge2);
    const float length = glm::length(normal);
    if (length < 1.0e-6F) {
        return {0.0F, 1.0F, 0.0F};
    }
    return normal / length;
}

void remap_morph_values(std::vector<math::Vec3>& values,
                        std::span<const std::uint32_t> source_indices, const char* label) {
    if (values.empty()) {
        return;
    }

    std::vector<math::Vec3> remapped;
    remapped.reserve(source_indices.size());
    for (const std::uint32_t source_index : source_indices) {
        if (source_index >= values.size()) {
            throw gltf_error(std::string(label) + " morph target source index is out of range");
        }
        remapped.push_back(values[source_index]);
    }
    values = std::move(remapped);
}

void generate_flat_normals(GltfMeshPrimitive& primitive) {
    if (primitive.indices.size() % 3 != 0) {
        throw gltf_error("triangle primitive index count must be divisible by 3");
    }

    std::vector<GltfVertex> expanded_vertices;
    std::vector<std::uint32_t> expanded_indices;
    std::vector<std::uint32_t> source_indices;
    expanded_vertices.reserve(primitive.indices.size());
    expanded_indices.reserve(primitive.indices.size());
    source_indices.reserve(primitive.indices.size());

    for (std::size_t i = 0; i < primitive.indices.size(); i += 3) {
        const std::uint32_t i0 = primitive.indices[i + 0];
        const std::uint32_t i1 = primitive.indices[i + 1];
        const std::uint32_t i2 = primitive.indices[i + 2];
        if (i0 >= primitive.vertices.size() || i1 >= primitive.vertices.size() ||
            i2 >= primitive.vertices.size()) {
            throw gltf_error("primitive index is out of vertex range");
        }

        const math::Vec3 normal =
            triangle_normal(primitive.vertices[i0], primitive.vertices[i1], primitive.vertices[i2]);
        for (const std::uint32_t source_index : {i0, i1, i2}) {
            GltfVertex vertex = primitive.vertices[source_index];
            vertex.normal = normal;
            expanded_vertices.push_back(vertex);
            const std::size_t expanded_index = expanded_vertices.size() - 1;
            if (expanded_index > std::numeric_limits<std::uint32_t>::max()) {
                throw gltf_error("expanded primitive vertex count is out of range");
            }
            expanded_indices.push_back(static_cast<std::uint32_t>(expanded_index));
            source_indices.push_back(source_index);
        }
    }

    for (GltfMorphTarget& target : primitive.morph_targets) {
        remap_morph_values(target.position_deltas, source_indices, "POSITION");
        remap_morph_values(target.normal_deltas, source_indices, "NORMAL");
        remap_morph_values(target.tangent_deltas, source_indices, "TANGENT");
    }

    primitive.vertices = std::move(expanded_vertices);
    primitive.indices = std::move(expanded_indices);
}

void require_supported_skin_attributes(const cgltf_primitive& primitive) {
    if (find_attribute(primitive, cgltf_attribute_type_joints, 1) != nullptr ||
        find_attribute(primitive, cgltf_attribute_type_weights, 1) != nullptr) {
        throw gltf_error("JOINTS_1 and WEIGHTS_1 are not supported");
    }
}

void require_supported_morph_target_attributes(const cgltf_morph_target& target) {
    for (cgltf_size i = 0; i < target.attributes_count; ++i) {
        const cgltf_attribute& attribute = target.attributes[i];
        const bool supported =
            attribute.index == 0 && (attribute.type == cgltf_attribute_type_position ||
                                     attribute.type == cgltf_attribute_type_normal ||
                                     attribute.type == cgltf_attribute_type_tangent);
        if (!supported) {
            throw gltf_error("unsupported morph target attribute");
        }
    }
}

[[nodiscard]] std::vector<math::Vec3> read_vec3_accessor_values(const cgltf_accessor* accessor,
                                                                const char* label) {
    require_optional_float_accessor(accessor, cgltf_type_vec3, label);
    if (accessor == nullptr) {
        return {};
    }
    const std::vector<float> unpacked = read_float_accessor_values(accessor, 3, label);
    std::vector<math::Vec3> values;
    values.reserve(accessor->count);
    for (cgltf_size i = 0; i < accessor->count; ++i) {
        values.push_back(vec3_at(unpacked, i));
    }
    return values;
}

[[nodiscard]] std::string morph_target_label(char* const* target_names,
                                             cgltf_size target_names_count,
                                             cgltf_size target_index) {
    if (target_index >= target_names_count) {
        return {};
    }
    return label_or_empty(target_names[target_index]);
}

[[nodiscard]] GltfMorphTarget load_morph_target(const cgltf_morph_target& target,
                                                cgltf_size vertex_count, std::string label) {
    require_supported_morph_target_attributes(target);
    const auto attributes =
        std::span<const cgltf_attribute>{target.attributes, target.attributes_count};
    const cgltf_accessor* positions = find_attribute(attributes, cgltf_attribute_type_position);
    const cgltf_accessor* normals = find_attribute(attributes, cgltf_attribute_type_normal);
    const cgltf_accessor* tangents = find_attribute(attributes, cgltf_attribute_type_tangent);
    require_optional_morph_accessor_count(positions, vertex_count, "POSITION");
    require_optional_morph_accessor_count(normals, vertex_count, "NORMAL");
    require_optional_morph_accessor_count(tangents, vertex_count, "TANGENT");
    return {
        .label = std::move(label),
        .position_deltas = read_vec3_accessor_values(positions, "POSITION morph target"),
        .normal_deltas = read_vec3_accessor_values(normals, "NORMAL morph target"),
        .tangent_deltas = read_vec3_accessor_values(tangents, "TANGENT morph target"),
    };
}

void expand_bounds_for_morph_targets(GltfMeshPrimitive& primitive) {
    if (primitive.vertices.empty() || primitive.morph_targets.empty()) {
        return;
    }

    math::Vec3 min_position = primitive.vertices.front().position;
    math::Vec3 max_position = primitive.vertices.front().position;
    const auto add_position = [&](math::Vec3 position) {
        min_position = glm::min(min_position, position);
        max_position = glm::max(max_position, position);
    };

    for (std::size_t vertex_index = 0; vertex_index < primitive.vertices.size(); ++vertex_index) {
        const math::Vec3 base_position = primitive.vertices[vertex_index].position;
        add_position(base_position);
        for (const GltfMorphTarget& target : primitive.morph_targets) {
            if (target.position_deltas.empty()) {
                continue;
            }
            add_position(base_position + target.position_deltas[vertex_index]);
        }
    }

    primitive.local_bounds = {
        .center = (min_position + max_position) * 0.5F,
        .half_extent = (max_position - min_position) * 0.5F,
    };
}

[[nodiscard]] GltfMeshPrimitive load_primitive(const cgltf_primitive& primitive,
                                               const cgltf_material* material_base,
                                               cgltf_size material_count, char* const* target_names,
                                               cgltf_size target_names_count,
                                               const GltfLoadConfig& config) {
    if (primitive.type != cgltf_primitive_type_triangles) {
        throw gltf_error("only triangle primitives are supported");
    }

    const cgltf_accessor* positions = find_attribute(primitive, cgltf_attribute_type_position);
    const cgltf_accessor* normals = find_attribute(primitive, cgltf_attribute_type_normal);
    const cgltf_accessor* tangents = find_attribute(primitive, cgltf_attribute_type_tangent);
    const cgltf_accessor* texcoord0 = find_attribute(primitive, cgltf_attribute_type_texcoord, 0);
    const cgltf_accessor* texcoord1 = find_attribute(primitive, cgltf_attribute_type_texcoord, 1);
    const cgltf_accessor* color0 = find_attribute(primitive, cgltf_attribute_type_color, 0);
    const cgltf_accessor* joints0 = find_attribute(primitive, cgltf_attribute_type_joints, 0);
    const cgltf_accessor* weights0 = find_attribute(primitive, cgltf_attribute_type_weights, 0);

    require_accessor_components(positions, 3, "POSITION");
    if (normals != nullptr) {
        require_accessor_components(normals, 3, "NORMAL");
    } else if (!config.generate_missing_normals) {
        throw gltf_error("primitive is missing required NORMAL attribute");
    }
    if (tangents != nullptr) {
        require_accessor_components(tangents, 4, "TANGENT");
    }
    require_optional_texcoord_accessor(texcoord0, "TEXCOORD_0");
    require_optional_texcoord_accessor(texcoord1, "TEXCOORD_1");
    require_optional_color_accessor(color0, "COLOR_0");
    require_supported_skin_attributes(primitive);
    require_optional_accessor_components(joints0, 4, "JOINTS_0");
    require_optional_accessor_components(weights0, 4, "WEIGHTS_0");
    if ((joints0 == nullptr) != (weights0 == nullptr)) {
        throw gltf_error("JOINTS_0 and WEIGHTS_0 must be provided together");
    }
    if (joints0 != nullptr && (joints0->component_type != cgltf_component_type_r_8u &&
                               joints0->component_type != cgltf_component_type_r_16u)) {
        throw gltf_error("JOINTS_0 must use UNSIGNED_BYTE or UNSIGNED_SHORT components");
    }

    const std::vector<float> position_values = read_float_accessor_values(positions, 3, "POSITION");
    const std::vector<float> normal_values = normals != nullptr
                                                 ? read_float_accessor_values(normals, 3, "NORMAL")
                                                 : std::vector<float>{};
    const std::vector<float> tangent_values =
        tangents != nullptr ? read_float_accessor_values(tangents, 4, "TANGENT")
                            : std::vector<float>{};
    const std::vector<float> texcoord0_values =
        texcoord0 != nullptr ? read_float_accessor_values(texcoord0, 2, "TEXCOORD_0")
                             : std::vector<float>{};
    const std::vector<float> texcoord1_values =
        texcoord1 != nullptr ? read_float_accessor_values(texcoord1, 2, "TEXCOORD_1")
                             : std::vector<float>{};
    const std::vector<math::Vec4> color0_values = read_color_accessor_values(color0, "COLOR_0");
    const std::vector<std::array<std::uint16_t, 4>> joints0_values =
        joints0 != nullptr ? read_u16_vec4_accessor_values(joints0, "JOINTS_0")
                           : std::vector<std::array<std::uint16_t, 4>>{};
    const std::vector<float> weights0_values =
        weights0 != nullptr ? read_float_accessor_values(weights0, 4, "WEIGHTS_0")
                            : std::vector<float>{};

    GltfMeshPrimitive result;
    result.vertices.resize(positions->count);
    for (cgltf_size i = 0; i < positions->count; ++i) {
        result.vertices[i].position = vec3_at(position_values, i);
        if (normals != nullptr) {
            result.vertices[i].normal = glm::normalize(vec3_at(normal_values, i));
        }
        if (tangents != nullptr) {
            result.vertices[i].tangent = vec4_at(tangent_values, i);
        }
        if (texcoord0 != nullptr) {
            result.vertices[i].texcoord0 = vec2_at(texcoord0_values, i);
        }
        if (texcoord1 != nullptr) {
            result.vertices[i].texcoord1 = vec2_at(texcoord1_values, i);
        }
        if (color0 != nullptr) {
            result.vertices[i].color0 = color0_values[i];
        }
        if (joints0 != nullptr) {
            result.vertices[i].joints0 = joints0_values[i];
            result.vertices[i].weights0 = vec4_at(weights0_values, i);
        }
    }

    if (primitive.indices != nullptr) {
        if (primitive.indices->is_sparse != 0) {
            throw gltf_error("primitive index sparse accessors are not supported");
        }
        result.indices.resize(primitive.indices->count);
        for (cgltf_size i = 0; i < primitive.indices->count; ++i) {
            const cgltf_size index = cgltf_accessor_read_index(primitive.indices, i);
            if (index >= positions->count) {
                throw gltf_error("primitive index is out of range");
            }
            result.indices[i] = static_cast<std::uint32_t>(index);
        }
    } else {
        result.indices.resize(result.vertices.size());
        for (std::size_t i = 0; i < result.indices.size(); ++i) {
            result.indices[i] = static_cast<std::uint32_t>(i);
        }
    }

    result.material_index =
        pointer_index(primitive.material, material_base, material_count, "material");
    if (result.material_index == kInvalidAssetIndex) {
        result.material_index = 0;
    } else {
        ++result.material_index;
    }
    result.morph_targets.reserve(primitive.targets_count);
    for (cgltf_size i = 0; i < primitive.targets_count; ++i) {
        result.morph_targets.push_back(
            load_morph_target(primitive.targets[i], positions->count,
                              morph_target_label(target_names, target_names_count, i)));
    }
    if (normals == nullptr) {
        generate_flat_normals(result);
    }
    if (tangents == nullptr && config.generate_missing_tangents && texcoord0 != nullptr) {
        generate_tangents(result);
    }
    result.local_bounds = bounds_for_positions(result.vertices);
    expand_bounds_for_morph_targets(result);
    return result;
}

[[nodiscard]] GltfMesh load_mesh(const cgltf_mesh& mesh, const cgltf_material* material_base,
                                 cgltf_size material_count, const GltfLoadConfig& config) {
    GltfMesh result{
        .label = label_or_empty(mesh.name),
    };
    result.primitives.reserve(mesh.primitives_count);
    for (cgltf_size i = 0; i < mesh.primitives_count; ++i) {
        result.primitives.push_back(load_primitive(mesh.primitives[i], material_base,
                                                   material_count, mesh.target_names,
                                                   mesh.target_names_count, config));
    }
    result.weights.reserve(mesh.weights_count);
    for (cgltf_size i = 0; i < mesh.weights_count; ++i) {
        result.weights.push_back(mesh.weights[i]);
    }
    return result;
}

[[nodiscard]] math::Mat4 trs_matrix(math::Vec3 translation, math::Quat rotation, math::Vec3 scale) {
    math::Mat4 matrix{1.0F};
    matrix = glm::translate(matrix, translation);
    matrix *= glm::mat4_cast(rotation);
    matrix = glm::scale(matrix, scale);
    return matrix;
}

[[nodiscard]] GltfNode load_node(const cgltf_node& node, const cgltf_node* node_base,
                                 cgltf_size node_count, const cgltf_mesh* mesh_base,
                                 cgltf_size mesh_count, const cgltf_skin* skin_base,
                                 cgltf_size skin_count) {
    GltfNode result{
        .label = label_or_empty(node.name),
    };

    if (node.has_translation) {
        result.translation = {node.translation[0], node.translation[1], node.translation[2]};
    }
    if (node.has_rotation) {
        result.rotation = {node.rotation[3], node.rotation[0], node.rotation[1], node.rotation[2]};
    }
    if (node.has_scale) {
        result.scale = {node.scale[0], node.scale[1], node.scale[2]};
    }
    result.local_matrix = trs_matrix(result.translation, result.rotation, result.scale);

    if (node.has_matrix) {
        math::Mat4 matrix{1.0F};
        std::memcpy(&matrix[0][0], node.matrix, sizeof(node.matrix));
        result.local_matrix = matrix;

        math::Vec3 skew{};
        math::Vec4 perspective{};
        glm::decompose(matrix, result.scale, result.rotation, result.translation, skew,
                       perspective);
    }

    result.mesh_index = pointer_index(node.mesh, mesh_base, mesh_count, "mesh");
    result.skin_index = pointer_index(node.skin, skin_base, skin_count, "skin");
    result.weights.reserve(node.weights_count);
    for (cgltf_size i = 0; i < node.weights_count; ++i) {
        result.weights.push_back(node.weights[i]);
    }
    result.children.reserve(node.children_count);
    for (cgltf_size i = 0; i < node.children_count; ++i) {
        result.children.push_back(pointer_index(node.children[i], node_base, node_count, "node"));
    }
    return result;
}

[[nodiscard]] GltfSkin load_skin(const cgltf_skin& skin, const cgltf_skin* skin_base,
                                 cgltf_size skin_count, const cgltf_node* node_base,
                                 cgltf_size node_count) {
    static_cast<void>(skin_base);
    static_cast<void>(skin_count);
    GltfSkin result{
        .label = label_or_empty(skin.name),
        .skeleton_node_index = pointer_index(skin.skeleton, node_base, node_count, "node"),
    };

    result.joints.reserve(skin.joints_count);
    for (cgltf_size i = 0; i < skin.joints_count; ++i) {
        result.joints.push_back(pointer_index(skin.joints[i], node_base, node_count, "node"));
    }

    if (skin.inverse_bind_matrices != nullptr) {
        require_float_accessor(skin.inverse_bind_matrices, cgltf_type_mat4, "inverseBindMatrices");
        if (skin.inverse_bind_matrices->count != skin.joints_count) {
            throw gltf_error("inverseBindMatrices count must match skin joint count");
        }
        result.inverse_bind_matrices.reserve(skin.inverse_bind_matrices->count);
        for (cgltf_size i = 0; i < skin.inverse_bind_matrices->count; ++i) {
            result.inverse_bind_matrices.push_back(read_mat4(skin.inverse_bind_matrices, i));
        }
    } else {
        result.inverse_bind_matrices.resize(skin.joints_count, math::Mat4{1.0F});
    }
    return result;
}

[[nodiscard]] GltfScene load_scene(const cgltf_scene& scene, const cgltf_node* node_base,
                                   cgltf_size node_count) {
    GltfScene result{
        .label = label_or_empty(scene.name),
    };
    result.root_nodes.reserve(scene.nodes_count);
    for (cgltf_size i = 0; i < scene.nodes_count; ++i) {
        result.root_nodes.push_back(pointer_index(scene.nodes[i], node_base, node_count, "node"));
    }
    return result;
}

[[nodiscard]] GltfAnimationInterpolation
load_animation_interpolation(cgltf_interpolation_type interpolation) {
    switch (interpolation) {
    case cgltf_interpolation_type_step:
        return GltfAnimationInterpolation::Step;
    case cgltf_interpolation_type_cubic_spline:
        return GltfAnimationInterpolation::CubicSpline;
    case cgltf_interpolation_type_linear:
    default:
        return GltfAnimationInterpolation::Linear;
    }
}

[[nodiscard]] GltfAnimationTargetPath load_animation_target_path(cgltf_animation_path_type path) {
    switch (path) {
    case cgltf_animation_path_type_translation:
        return GltfAnimationTargetPath::Translation;
    case cgltf_animation_path_type_rotation:
        return GltfAnimationTargetPath::Rotation;
    case cgltf_animation_path_type_scale:
        return GltfAnimationTargetPath::Scale;
    case cgltf_animation_path_type_weights:
        return GltfAnimationTargetPath::Weights;
    case cgltf_animation_path_type_invalid:
    default:
        throw gltf_error("unsupported animation target path");
    }
}

[[nodiscard]] GltfAnimationSampler load_animation_sampler(const cgltf_animation_sampler& sampler) {
    require_float_accessor(sampler.input, cgltf_type_scalar, "animation input");
    if (sampler.output == nullptr) {
        throw gltf_error("animation output accessor is missing");
    }
    if (sampler.output->component_type != cgltf_component_type_r_32f) {
        throw gltf_error("animation output accessor must use FLOAT components");
    }
    const cgltf_size component_count = cgltf_num_components(sampler.output->type);
    if (component_count == 0) {
        throw gltf_error("animation output accessor has unsupported type");
    }
    if (component_count > std::numeric_limits<std::uint32_t>::max()) {
        throw gltf_error("animation output component count is out of range");
    }

    return {
        .interpolation = load_animation_interpolation(sampler.interpolation),
        .input_times = read_float_accessor_values(sampler.input, 1, "animation input"),
        .output_values =
            read_float_accessor_values(sampler.output, component_count, "animation output"),
        .component_count = static_cast<std::uint32_t>(component_count),
    };
}

[[nodiscard]] GltfAnimation load_animation(const cgltf_animation& animation,
                                           const cgltf_node* node_base, cgltf_size node_count) {
    GltfAnimation result{
        .label = label_or_empty(animation.name),
    };

    result.samplers.reserve(animation.samplers_count);
    for (cgltf_size i = 0; i < animation.samplers_count; ++i) {
        GltfAnimationSampler sampler = load_animation_sampler(animation.samplers[i]);
        if (!sampler.input_times.empty()) {
            result.duration_seconds =
                std::max(result.duration_seconds,
                         *std::max_element(sampler.input_times.begin(), sampler.input_times.end()));
        }
        result.samplers.push_back(std::move(sampler));
    }

    result.channels.reserve(animation.channels_count);
    for (cgltf_size i = 0; i < animation.channels_count; ++i) {
        const cgltf_animation_channel& channel = animation.channels[i];
        result.channels.push_back(GltfAnimationChannel{
            .sampler_index = pointer_index(channel.sampler, animation.samplers,
                                           animation.samplers_count, "animation sampler"),
            .node_index = pointer_index(channel.target_node, node_base, node_count, "node"),
            .target_path = load_animation_target_path(channel.target_path),
        });
    }
    return result;
}

[[nodiscard]] bool supports_required_extension(std::string_view extension) noexcept {
    static constexpr std::array<std::string_view, 5> kSupportedRequiredExtensions{
        "KHR_materials_emissive_strength", "KHR_materials_ior",   "KHR_materials_specular",
        "KHR_texture_transform",           "KHR_materials_unlit",
    };
    return std::ranges::find(kSupportedRequiredExtensions, extension) !=
           kSupportedRequiredExtensions.end();
}

void reject_unsupported_features(const cgltf_data& data) {
    for (cgltf_size i = 0; i < data.extensions_required_count; ++i) {
        const std::string_view extension = data.extensions_required[i] != nullptr
                                               ? std::string_view{data.extensions_required[i]}
                                               : std::string_view{};
        if (!supports_required_extension(extension)) {
            throw gltf_error("required glTF extension is not supported: " + std::string(extension));
        }
    }
}

struct CgltfDataDeleter {
    void operator()(cgltf_data* data) const noexcept {
        cgltf_free(data);
    }
};

using CgltfDataPtr = std::unique_ptr<cgltf_data, CgltfDataDeleter>;

} // namespace

GltfAsset load_gltf_asset(const std::filesystem::path& path, GltfLoadConfig config) {
    cgltf_options options{};
    cgltf_data* raw_data = nullptr;
    const std::string path_string = path.string();
    cgltf_result result = cgltf_parse_file(&options, path_string.c_str(), &raw_data);
    if (result != cgltf_result_success) {
        throw gltf_error("failed to parse " + path_string);
    }
    CgltfDataPtr data(raw_data);

    result = cgltf_load_buffers(&options, data.get(), path_string.c_str());
    if (result != cgltf_result_success) {
        throw gltf_error("failed to load buffers for " + path_string);
    }

    result = cgltf_validate(data.get());
    if (result != cgltf_result_success) {
        throw gltf_error("validation failed for " + path_string);
    }

    reject_unsupported_features(*data);

    GltfAsset asset{
        .source_path = path,
    };

    asset.samplers.reserve(data->samplers_count);
    for (cgltf_size i = 0; i < data->samplers_count; ++i) {
        asset.samplers.push_back(load_sampler(data->samplers[i]));
    }

    asset.images.reserve(data->images_count);
    for (cgltf_size i = 0; i < data->images_count; ++i) {
        asset.images.push_back(decode_image(data->images[i], path));
    }

    asset.textures.reserve(data->textures_count);
    for (cgltf_size i = 0; i < data->textures_count; ++i) {
        const cgltf_texture& texture = data->textures[i];
        asset.textures.push_back(GltfTexture{
            .label = label_or_empty(texture.name),
            .image_index = pointer_index(texture.image, data->images, data->images_count, "image"),
            .sampler_index =
                pointer_index(texture.sampler, data->samplers, data->samplers_count, "sampler"),
        });
    }

    asset.materials.reserve(data->materials_count + 1);
    asset.materials.push_back(default_material());
    for (cgltf_size i = 0; i < data->materials_count; ++i) {
        asset.materials.push_back(
            load_material(data->materials[i], data->textures, data->textures_count));
    }

    asset.meshes.reserve(data->meshes_count);
    for (cgltf_size i = 0; i < data->meshes_count; ++i) {
        asset.meshes.push_back(
            load_mesh(data->meshes[i], data->materials, data->materials_count, config));
    }

    asset.nodes.reserve(data->nodes_count);
    for (cgltf_size i = 0; i < data->nodes_count; ++i) {
        asset.nodes.push_back(load_node(data->nodes[i], data->nodes, data->nodes_count,
                                        data->meshes, data->meshes_count, data->skins,
                                        data->skins_count));
    }

    asset.skins.reserve(data->skins_count);
    for (cgltf_size i = 0; i < data->skins_count; ++i) {
        asset.skins.push_back(load_skin(data->skins[i], data->skins, data->skins_count, data->nodes,
                                        data->nodes_count));
    }

    asset.scenes.reserve(data->scenes_count);
    for (cgltf_size i = 0; i < data->scenes_count; ++i) {
        asset.scenes.push_back(load_scene(data->scenes[i], data->nodes, data->nodes_count));
    }

    asset.animations.reserve(data->animations_count);
    for (cgltf_size i = 0; i < data->animations_count; ++i) {
        asset.animations.push_back(
            load_animation(data->animations[i], data->nodes, data->nodes_count));
    }

    asset.default_scene = pointer_index(data->scene, data->scenes, data->scenes_count, "scene");
    if (asset.default_scene == kInvalidAssetIndex && !asset.scenes.empty()) {
        asset.default_scene = 0;
    }
    return asset;
}

const char* gltf_alpha_mode_name(GltfAlphaMode mode) noexcept {
    switch (mode) {
    case GltfAlphaMode::Mask:
        return "MASK";
    case GltfAlphaMode::Blend:
        return "BLEND";
    case GltfAlphaMode::Opaque:
    default:
        return "OPAQUE";
    }
}

GltfTextureColorSpace gltf_texture_color_space_for_base_color() noexcept {
    return GltfTextureColorSpace::Srgb;
}

GltfTextureColorSpace
gltf_texture_color_space_for_material_slot(const GltfTextureRef& texture,
                                           GltfTextureColorSpace default_space) noexcept {
    return texture.has_value() ? default_space : GltfTextureColorSpace::Linear;
}

} // namespace cubey::asset
