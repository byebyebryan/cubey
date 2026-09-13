#include <cubey/asset/gltf_asset.h>

#include "gltf_asset_internal.h"

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/common.hpp>
#include <glm/geometric.hpp>

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wold-style-cast"
#pragma GCC diagnostic ignored "-Wsign-conversion"
#pragma GCC diagnostic ignored "-Wshadow"
#endif
#include <cgltf.h>
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace cubey::asset::gltf_internal {

[[nodiscard]] std::vector<float> read_float_accessor_values(const cgltf_accessor* accessor,
                                                            std::size_t component_count,
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

namespace {
[[nodiscard]] std::uint32_t checked_geometry_index(std::ptrdiff_t index, const char* label) {
    if (index < 0 || static_cast<std::uint64_t>(index) >
                         static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max())) {
        throw gltf_error(std::string(label) + " index is out of range");
    }
    return static_cast<std::uint32_t>(index);
}

template <typename T>
[[nodiscard]] std::uint32_t geometry_pointer_index(const T* pointer, const T* base,
                                                   cgltf_size count, const char* label) {
    if (pointer == nullptr) {
        return kInvalidAssetIndex;
    }
    if (base == nullptr || pointer < base || pointer >= base + count) {
        throw gltf_error(std::string(label) + " pointer is outside the glTF data");
    }
    return checked_geometry_index(pointer - base, label);
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

[[nodiscard]] math::Vec2 tangent_texcoord(const GltfVertex& vertex, std::uint32_t texcoord_set) {
    return texcoord_set == 1U ? vertex.texcoord1 : vertex.texcoord0;
}

void generate_tangents(GltfMeshPrimitive& primitive, std::uint32_t texcoord_set) {
    std::vector<math::Vec3> tangent_accum(primitive.vertices.size(), math::Vec3{0.0F, 0.0F, 0.0F});
    std::vector<math::Vec3> bitangent_accum(primitive.vertices.size(),
                                            math::Vec3{0.0F, 0.0F, 0.0F});
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
        const math::Vec2 delta_uv1 =
            tangent_texcoord(v1, texcoord_set) - tangent_texcoord(v0, texcoord_set);
        const math::Vec2 delta_uv2 =
            tangent_texcoord(v2, texcoord_set) - tangent_texcoord(v0, texcoord_set);
        const float determinant = delta_uv1.x * delta_uv2.y - delta_uv1.y * delta_uv2.x;
        if (std::abs(determinant) < 1.0e-6F) {
            continue;
        }
        const math::Vec3 tangent = (edge1 * delta_uv2.y - edge2 * delta_uv1.y) / determinant;
        const math::Vec3 bitangent = (edge2 * delta_uv1.x - edge1 * delta_uv2.x) / determinant;
        tangent_accum[i0] += tangent;
        tangent_accum[i1] += tangent;
        tangent_accum[i2] += tangent;
        bitangent_accum[i0] += bitangent;
        bitangent_accum[i1] += bitangent;
        bitangent_accum[i2] += bitangent;
    }

    for (std::size_t i = 0; i < primitive.vertices.size(); ++i) {
        const math::Vec3 normal = primitive.vertices[i].normal;
        math::Vec3 tangent = tangent_accum[i] - normal * glm::dot(normal, tangent_accum[i]);
        const bool has_triangle_tangent = glm::length(tangent) >= 1.0e-6F;
        if (!has_triangle_tangent) {
            tangent = std::abs(normal.y) < 0.9F ? glm::cross(normal, math::Vec3{0.0F, 1.0F, 0.0F})
                                                : glm::cross(normal, math::Vec3{1.0F, 0.0F, 0.0F});
        }
        tangent = glm::normalize(tangent);
        float handedness = 1.0F;
        if (has_triangle_tangent && glm::length(bitangent_accum[i]) >= 1.0e-6F) {
            // glTF normal maps use +Y up while UV V starts at the top of the image. Emit the
            // handedness that makes the shader's cross(normal, tangent) basis match that
            // convention.
            handedness =
                glm::dot(glm::cross(normal, tangent), bitangent_accum[i]) < 0.0F ? 1.0F : -1.0F;
        }
        primitive.vertices[i].tangent = {tangent.x, tangent.y, tangent.z, handedness};
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

[[nodiscard]] std::uint32_t normal_texture_texcoord_set(const cgltf_primitive& primitive) {
    if (primitive.material == nullptr || primitive.material->normal_texture.texture == nullptr) {
        return 0U;
    }
    const cgltf_texture_view& view = primitive.material->normal_texture;
    cgltf_int texcoord = view.texcoord;
    if (view.has_transform != 0 && view.transform.has_texcoord != 0) {
        texcoord = view.transform.texcoord;
    }
    if (texcoord < 0 || texcoord > 1) {
        throw gltf_error("normal texture coordinate sets above TEXCOORD_1 are not supported");
    }
    return static_cast<std::uint32_t>(texcoord);
}

[[nodiscard]] std::uint32_t texture_texcoord_set(const cgltf_texture_view& view,
                                                 const char* texture_label) {
    cgltf_int texcoord = view.texcoord;
    if (view.has_transform != 0 && view.transform.has_texcoord != 0) {
        texcoord = view.transform.texcoord;
    }
    if (texcoord < 0 || texcoord > 1) {
        throw gltf_error(std::string(texture_label) + " texCoord must be TEXCOORD_0 or TEXCOORD_1");
    }
    return static_cast<std::uint32_t>(texcoord);
}

[[nodiscard]] std::uint32_t anisotropy_tangent_texcoord_set(const cgltf_primitive& primitive) {
    if (primitive.material == nullptr || primitive.material->has_anisotropy == 0) {
        return normal_texture_texcoord_set(primitive);
    }

    const cgltf_material& material = *primitive.material;
    const bool has_normal_texture = material.normal_texture.texture != nullptr;
    const bool has_anisotropy_texture = material.anisotropy.anisotropy_texture.texture != nullptr;
    const std::uint32_t normal_texcoord =
        has_normal_texture ? texture_texcoord_set(material.normal_texture, "normalTexture") : 0U;
    const std::uint32_t anisotropy_texcoord =
        has_anisotropy_texture ? texture_texcoord_set(material.anisotropy.anisotropy_texture,
                                                      "KHR_materials_anisotropy anisotropyTexture")
                               : 0U;
    if (has_normal_texture && has_anisotropy_texture && normal_texcoord != anisotropy_texcoord) {
        throw gltf_error("KHR_materials_anisotropy requires matching normalTexture and "
                         "anisotropyTexture texCoord sets when generating TANGENT");
    }
    return has_normal_texture ? normal_texcoord
                              : (has_anisotropy_texture ? anisotropy_texcoord : 0U);
}

void require_anisotropy_tangent_space(const cgltf_primitive& primitive,
                                      const cgltf_accessor* tangents,
                                      const cgltf_accessor* texcoord0,
                                      const cgltf_accessor* texcoord1, const GltfLoadConfig& config,
                                      GltfMeshPrimitive& result) {
    if (primitive.material == nullptr || primitive.material->has_anisotropy == 0 ||
        tangents != nullptr) {
        return;
    }

    if (!config.generate_missing_tangents) {
        throw gltf_error("KHR_materials_anisotropy requires a TANGENT attribute when "
                         "generate_missing_tangents is disabled");
    }
    const std::uint32_t texcoord_set = anisotropy_tangent_texcoord_set(primitive);
    const bool has_texcoords = texcoord_set == 1U ? texcoord1 != nullptr : texcoord0 != nullptr;
    if (!has_texcoords) {
        throw gltf_error(std::string("KHR_materials_anisotropy requires TEXCOORD_") +
                         std::to_string(texcoord_set) + " to generate the required tangent space");
    }
    generate_tangents(result, texcoord_set);
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
        geometry_pointer_index(primitive.material, material_base, material_count, "material");
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
    if (primitive.material != nullptr && primitive.material->has_anisotropy != 0) {
        require_anisotropy_tangent_space(primitive, tangents, texcoord0, texcoord1, config, result);
    } else {
        const std::uint32_t tangent_texcoord_set = normal_texture_texcoord_set(primitive);
        const bool has_tangent_texcoords =
            tangent_texcoord_set == 1U ? texcoord1 != nullptr : texcoord0 != nullptr;
        if (tangents == nullptr && config.generate_missing_tangents && has_tangent_texcoords) {
            generate_tangents(result, tangent_texcoord_set);
        }
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
} // namespace

void assemble_gltf_geometry_data(GltfAsset& asset, const cgltf_data& data,
                                 const GltfLoadConfig& config) {
    asset.meshes.reserve(data.meshes_count);
    for (cgltf_size i = 0; i < data.meshes_count; ++i) {
        asset.meshes.push_back(
            load_mesh(data.meshes[i], data.materials, data.materials_count, config));
    }
}

} // namespace cubey::asset::gltf_internal
