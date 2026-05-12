#include <cubey/render/primitive_mesh.h>

#include <array>
#include <cstddef>
#include <stdexcept>

namespace cubey::render {
namespace {

struct CubeFaceVertex {
    PrimitiveVec3 position{};
    PrimitiveVec3 normal{};
    PrimitiveVec2 uv{};
    std::size_t face = 0;
};

[[nodiscard]] std::uint32_t offset_of(std::size_t offset) {
    return static_cast<std::uint32_t>(offset);
}

template <typename Vertex>
[[nodiscard]] VkVertexInputBindingDescription vertex_binding_description() {
    return {
        .binding = 0,
        .stride = static_cast<std::uint32_t>(sizeof(Vertex)),
        .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
    };
}

[[nodiscard]] VkVertexInputAttributeDescription vertex_attribute(std::uint32_t location,
                                                                 VkFormat format,
                                                                 std::uint32_t offset) {
    return {
        .location = location,
        .binding = 0,
        .format = format,
        .offset = offset,
    };
}

[[nodiscard]] std::array<CubeFaceVertex, 24> cube_face_vertices(float half_extent) {
    const float h = half_extent;
    return {{
        {{-h, -h, h}, {0.0F, 0.0F, 1.0F}, {0.0F, 0.0F}, 0},
        {{h, -h, h}, {0.0F, 0.0F, 1.0F}, {1.0F, 0.0F}, 0},
        {{h, h, h}, {0.0F, 0.0F, 1.0F}, {1.0F, 1.0F}, 0},
        {{-h, h, h}, {0.0F, 0.0F, 1.0F}, {0.0F, 1.0F}, 0},
        {{h, -h, -h}, {0.0F, 0.0F, -1.0F}, {0.0F, 0.0F}, 1},
        {{-h, -h, -h}, {0.0F, 0.0F, -1.0F}, {1.0F, 0.0F}, 1},
        {{-h, h, -h}, {0.0F, 0.0F, -1.0F}, {1.0F, 1.0F}, 1},
        {{h, h, -h}, {0.0F, 0.0F, -1.0F}, {0.0F, 1.0F}, 1},
        {{-h, -h, -h}, {-1.0F, 0.0F, 0.0F}, {0.0F, 0.0F}, 2},
        {{-h, -h, h}, {-1.0F, 0.0F, 0.0F}, {1.0F, 0.0F}, 2},
        {{-h, h, h}, {-1.0F, 0.0F, 0.0F}, {1.0F, 1.0F}, 2},
        {{-h, h, -h}, {-1.0F, 0.0F, 0.0F}, {0.0F, 1.0F}, 2},
        {{h, -h, h}, {1.0F, 0.0F, 0.0F}, {0.0F, 0.0F}, 3},
        {{h, -h, -h}, {1.0F, 0.0F, 0.0F}, {1.0F, 0.0F}, 3},
        {{h, h, -h}, {1.0F, 0.0F, 0.0F}, {1.0F, 1.0F}, 3},
        {{h, h, h}, {1.0F, 0.0F, 0.0F}, {0.0F, 1.0F}, 3},
        {{-h, h, h}, {0.0F, 1.0F, 0.0F}, {0.0F, 0.0F}, 4},
        {{h, h, h}, {0.0F, 1.0F, 0.0F}, {1.0F, 0.0F}, 4},
        {{h, h, -h}, {0.0F, 1.0F, 0.0F}, {1.0F, 1.0F}, 4},
        {{-h, h, -h}, {0.0F, 1.0F, 0.0F}, {0.0F, 1.0F}, 4},
        {{-h, -h, -h}, {0.0F, -1.0F, 0.0F}, {0.0F, 0.0F}, 5},
        {{h, -h, -h}, {0.0F, -1.0F, 0.0F}, {1.0F, 0.0F}, 5},
        {{h, -h, h}, {0.0F, -1.0F, 0.0F}, {1.0F, 1.0F}, 5},
        {{-h, -h, h}, {0.0F, -1.0F, 0.0F}, {0.0F, 1.0F}, 5},
    }};
}

[[nodiscard]] std::vector<std::uint16_t> cube_indices() {
    return {
        0,  1,  2,  0,  2,  3,  4,  5,  6,  4,  6,  7,  8,  9,  10, 8,  10, 11,
        12, 13, 14, 12, 14, 15, 16, 17, 18, 16, 18, 19, 20, 21, 22, 20, 22, 23,
    };
}

void validate_cube_config(const CubeMeshConfig& config) {
    if (config.half_extent <= 0.0F) {
        throw std::runtime_error("cube mesh half extent must be positive");
    }
}

void validate_plane_config(const PlaneMeshConfig& config) {
    if (config.half_extent_x <= 0.0F || config.half_extent_z <= 0.0F) {
        throw std::runtime_error("plane mesh half extents must be positive");
    }
}

} // namespace

VertexInputLayout vertex_position_color_input_layout() {
    return {
        .binding = vertex_binding_description<VertexPositionColor>(),
        .attributes =
            {
                vertex_attribute(0, VK_FORMAT_R32G32B32_SFLOAT,
                                 offset_of(offsetof(VertexPositionColor, position))),
                vertex_attribute(1, VK_FORMAT_R32G32B32_SFLOAT,
                                 offset_of(offsetof(VertexPositionColor, color))),
            },
    };
}

VertexInputLayout vertex_position_color_normal_input_layout() {
    return {
        .binding = vertex_binding_description<VertexPositionColorNormal>(),
        .attributes =
            {
                vertex_attribute(0, VK_FORMAT_R32G32B32_SFLOAT,
                                 offset_of(offsetof(VertexPositionColorNormal, position))),
                vertex_attribute(1, VK_FORMAT_R32G32B32_SFLOAT,
                                 offset_of(offsetof(VertexPositionColorNormal, color))),
                vertex_attribute(2, VK_FORMAT_R32G32B32_SFLOAT,
                                 offset_of(offsetof(VertexPositionColorNormal, normal))),
            },
    };
}

VertexInputLayout vertex_position_color_normal_uv_input_layout() {
    return {
        .binding = vertex_binding_description<VertexPositionColorNormalUv>(),
        .attributes =
            {
                vertex_attribute(0, VK_FORMAT_R32G32B32_SFLOAT,
                                 offset_of(offsetof(VertexPositionColorNormalUv, position))),
                vertex_attribute(1, VK_FORMAT_R32G32B32_SFLOAT,
                                 offset_of(offsetof(VertexPositionColorNormalUv, color))),
                vertex_attribute(2, VK_FORMAT_R32G32B32_SFLOAT,
                                 offset_of(offsetof(VertexPositionColorNormalUv, normal))),
                vertex_attribute(3, VK_FORMAT_R32G32_SFLOAT,
                                 offset_of(offsetof(VertexPositionColorNormalUv, uv))),
            },
    };
}

PrimitiveMeshData<VertexPositionColor> make_cube_position_color_mesh(CubeMeshConfig config) {
    validate_cube_config(config);

    PrimitiveMeshData<VertexPositionColor> mesh;
    mesh.vertices.reserve(24);
    for (const CubeFaceVertex& vertex : cube_face_vertices(config.half_extent)) {
        mesh.vertices.push_back({
            .position = vertex.position,
            .color = config.face_colors[vertex.face],
        });
    }
    mesh.indices = cube_indices();
    return mesh;
}

PrimitiveMeshData<VertexPositionColorNormal>
make_cube_position_color_normal_mesh(CubeMeshConfig config) {
    validate_cube_config(config);

    PrimitiveMeshData<VertexPositionColorNormal> mesh;
    mesh.vertices.reserve(24);
    for (const CubeFaceVertex& vertex : cube_face_vertices(config.half_extent)) {
        mesh.vertices.push_back({
            .position = vertex.position,
            .color = config.face_colors[vertex.face],
            .normal = vertex.normal,
        });
    }
    mesh.indices = cube_indices();
    return mesh;
}

PrimitiveMeshData<VertexPositionColorNormalUv>
make_cube_position_color_normal_uv_mesh(CubeMeshConfig config) {
    validate_cube_config(config);

    PrimitiveMeshData<VertexPositionColorNormalUv> mesh;
    mesh.vertices.reserve(24);
    for (const CubeFaceVertex& vertex : cube_face_vertices(config.half_extent)) {
        mesh.vertices.push_back({
            .position = vertex.position,
            .color = config.face_colors[vertex.face],
            .normal = vertex.normal,
            .uv = vertex.uv,
        });
    }
    mesh.indices = cube_indices();
    return mesh;
}

PrimitiveMeshData<VertexPositionColorNormal>
make_xz_plane_position_color_normal_mesh(PlaneMeshConfig config) {
    validate_plane_config(config);

    const float min_x = config.center[0] - config.half_extent_x;
    const float max_x = config.center[0] + config.half_extent_x;
    const float y = config.center[1];
    const float min_z = config.center[2] - config.half_extent_z;
    const float max_z = config.center[2] + config.half_extent_z;
    constexpr PrimitiveVec3 kUpNormal{0.0F, 1.0F, 0.0F};

    return {
        .vertices =
            {
                {.position = {min_x, y, min_z}, .color = config.color, .normal = kUpNormal},
                {.position = {max_x, y, min_z}, .color = config.color, .normal = kUpNormal},
                {.position = {max_x, y, max_z}, .color = config.color, .normal = kUpNormal},
                {.position = {min_x, y, max_z}, .color = config.color, .normal = kUpNormal},
            },
        .indices = {0, 1, 2, 0, 2, 3},
    };
}

} // namespace cubey::render
