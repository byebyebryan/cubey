#pragma once

#include <cubey/render/mesh.h>

#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace cubey::render {

using PrimitiveVec2 = std::array<float, 2>;
using PrimitiveVec3 = std::array<float, 3>;

struct VertexPositionColor {
    PrimitiveVec3 position{};
    PrimitiveVec3 color{};
};

struct VertexPositionColorNormal {
    PrimitiveVec3 position{};
    PrimitiveVec3 color{};
    PrimitiveVec3 normal{};
};

struct VertexPositionColorNormalUv {
    PrimitiveVec3 position{};
    PrimitiveVec3 color{};
    PrimitiveVec3 normal{};
    PrimitiveVec2 uv{};
};

struct VertexInputLayout {
    std::vector<VkVertexInputBindingDescription> vertex_bindings{};
    std::vector<VkVertexInputAttributeDescription> attributes{};

    [[nodiscard]] std::span<const VkVertexInputBindingDescription> bindings() const {
        return vertex_bindings;
    }

    [[nodiscard]] std::span<const VkVertexInputAttributeDescription>
    attribute_descriptions() const {
        return attributes;
    }
};

template <typename Vertex> struct PrimitiveMeshData {
    std::vector<Vertex> vertices{};
    std::vector<std::uint16_t> indices{};

    [[nodiscard]] MeshConfig mesh_config() const {
        return indexed_mesh_config(std::span<const Vertex>(vertices.data(), vertices.size()),
                                   std::span<const std::uint16_t>(indices.data(), indices.size()));
    }
};

inline constexpr std::array<PrimitiveVec3, 6> kDefaultCubeFaceColors{
    PrimitiveVec3{0.95F, 0.25F, 0.18F}, PrimitiveVec3{0.18F, 0.56F, 0.95F},
    PrimitiveVec3{0.22F, 0.78F, 0.42F}, PrimitiveVec3{0.96F, 0.76F, 0.18F},
    PrimitiveVec3{0.65F, 0.34F, 0.95F}, PrimitiveVec3{0.18F, 0.82F, 0.82F},
};

struct CubeMeshConfig {
    float half_extent = 1.0F;
    // Authored display/sRGB colors; primitive builders linearize before storing vertices.
    std::array<PrimitiveVec3, 6> face_colors = kDefaultCubeFaceColors;
};

struct PlaneMeshConfig {
    PrimitiveVec3 center{0.0F, 0.0F, 0.0F};
    float half_extent_x = 1.0F;
    float half_extent_z = 1.0F;
    // Authored display/sRGB color; primitive builders linearize before storing vertices.
    PrimitiveVec3 color{0.58F, 0.58F, 0.52F};
};

struct SphereMeshConfig {
    float radius = 1.0F;
    std::uint32_t latitude_segments = 16;
    std::uint32_t longitude_segments = 32;
    // Authored display/sRGB color; primitive builders linearize before storing vertices.
    PrimitiveVec3 color{0.86F, 0.86F, 0.86F};
};

[[nodiscard]] VertexInputLayout vertex_position_color_input_layout();
[[nodiscard]] VertexInputLayout vertex_position_color_normal_input_layout();
[[nodiscard]] VertexInputLayout vertex_position_color_normal_uv_input_layout();
[[nodiscard]] VertexInputLayout vertex_position_only_input_layout(std::uint32_t vertex_stride);
[[nodiscard]] VkVertexInputBindingDescription
vertex_input_binding(std::uint32_t binding, std::uint32_t stride, VkVertexInputRate input_rate);
[[nodiscard]] VkVertexInputAttributeDescription vertex_input_attribute(std::uint32_t location,
                                                                       std::uint32_t binding,
                                                                       VkFormat format,
                                                                       std::uint32_t offset);

[[nodiscard]] PrimitiveMeshData<VertexPositionColor>
make_cube_position_color_mesh(CubeMeshConfig config = {});
[[nodiscard]] PrimitiveMeshData<VertexPositionColorNormal>
make_cube_position_color_normal_mesh(CubeMeshConfig config = {});
[[nodiscard]] PrimitiveMeshData<VertexPositionColorNormalUv>
make_cube_position_color_normal_uv_mesh(CubeMeshConfig config = {});
[[nodiscard]] PrimitiveMeshData<VertexPositionColorNormal>
make_xz_plane_position_color_normal_mesh(PlaneMeshConfig config);
[[nodiscard]] PrimitiveMeshData<VertexPositionColorNormalUv>
make_uv_sphere_position_color_normal_uv_mesh(SphereMeshConfig config = {});

} // namespace cubey::render
