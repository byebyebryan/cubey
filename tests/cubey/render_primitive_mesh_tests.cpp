#include <cubey/render/primitive_mesh.h>

#include <vulkan/vulkan.h>

#include <cstddef>
#include <cstdint>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void require_vec3(std::array<float, 3> value, std::array<float, 3> expected,
                  const char* message) {
    require(value == expected, message);
}

void require_vec2(std::array<float, 2> value, std::array<float, 2> expected,
                  const char* message) {
    require(value == expected, message);
}

} // namespace

void test_primitive_vertex_layouts_match_shader_contracts() {
    const cubey::render::VertexInputLayout position_color =
        cubey::render::vertex_position_color_input_layout();
    require(position_color.binding.stride == sizeof(cubey::render::VertexPositionColor),
            "position-color binding stride should match vertex size");
    require(position_color.binding.inputRate == VK_VERTEX_INPUT_RATE_VERTEX,
            "position-color binding should be per-vertex");
    require(position_color.attributes.size() == 2,
            "position-color layout should expose position and color");
    require(position_color.attributes[0].location == 0,
            "position should use shader location 0");
    require(position_color.attributes[0].format == VK_FORMAT_R32G32B32_SFLOAT,
            "position should use vec3 float format");
    require(position_color.attributes[0].offset ==
                offsetof(cubey::render::VertexPositionColor, position),
            "position offset should match vertex field");
    require(position_color.attributes[1].location == 1, "color should use shader location 1");
    require(position_color.attributes[1].format == VK_FORMAT_R32G32B32_SFLOAT,
            "color should use vec3 float format");
    require(position_color.attributes[1].offset == offsetof(cubey::render::VertexPositionColor,
                                                            color),
            "color offset should match vertex field");

    const cubey::render::VertexInputLayout position_color_normal =
        cubey::render::vertex_position_color_normal_input_layout();
    require(position_color_normal.binding.stride ==
                sizeof(cubey::render::VertexPositionColorNormal),
            "position-color-normal binding stride should match vertex size");
    require(position_color_normal.attributes.size() == 3,
            "position-color-normal layout should expose position, color, normal");
    require(position_color_normal.attributes[2].location == 2,
            "normal should use shader location 2");
    require(position_color_normal.attributes[2].offset ==
                offsetof(cubey::render::VertexPositionColorNormal, normal),
            "normal offset should match vertex field");

    const cubey::render::VertexInputLayout position_color_normal_uv =
        cubey::render::vertex_position_color_normal_uv_input_layout();
    require(position_color_normal_uv.binding.stride ==
                sizeof(cubey::render::VertexPositionColorNormalUv),
            "position-color-normal-uv binding stride should match vertex size");
    require(position_color_normal_uv.attributes.size() == 4,
            "position-color-normal-uv layout should expose position, color, normal, uv");
    require(position_color_normal_uv.attributes[3].location == 3,
            "uv should use shader location 3");
    require(position_color_normal_uv.attributes[3].format == VK_FORMAT_R32G32_SFLOAT,
            "uv should use vec2 float format");
    require(position_color_normal_uv.attributes[3].offset ==
                offsetof(cubey::render::VertexPositionColorNormalUv, uv),
            "uv offset should match vertex field");

    const cubey::render::VertexInputLayout position_only =
        cubey::render::vertex_position_only_input_layout(
            sizeof(cubey::render::VertexPositionColorNormal));
    require(position_only.binding.stride == sizeof(cubey::render::VertexPositionColorNormal),
            "position-only binding stride should use caller-provided vertex size");
    require(position_only.binding.inputRate == VK_VERTEX_INPUT_RATE_VERTEX,
            "position-only binding should be per-vertex");
    require(position_only.attributes.size() == 1,
            "position-only layout should expose only position");
    require(position_only.attributes[0].location == 0,
            "position-only attribute should use shader location 0");
    require(position_only.attributes[0].format == VK_FORMAT_R32G32B32_SFLOAT,
            "position-only attribute should use vec3 float format");
    require(position_only.attributes[0].offset == 0,
            "position-only attribute should assume position is the first vertex field");
}

void test_primitive_cube_position_color_mesh_uses_face_colors_and_indices() {
    cubey::render::CubeMeshConfig config;
    config.face_colors[0] = {0.1F, 0.2F, 0.3F};
    const cubey::render::PrimitiveMeshData<cubey::render::VertexPositionColor> cube =
        cubey::render::make_cube_position_color_mesh(config);

    require(cube.vertices.size() == 24, "cube should have independent vertices per face");
    require(cube.indices.size() == 36, "cube should have two triangles per face");
    require_vec3(cube.vertices[0].position, {-1.0F, -1.0F, 1.0F},
                 "cube should preserve front-bottom-left position");
    require_vec3(cube.vertices[0].color, {0.1F, 0.2F, 0.3F},
                 "cube should apply configured face color");
    for (const std::uint16_t index : cube.indices) {
        require(index < cube.vertices.size(), "cube indices should stay in vertex range");
    }

    const cubey::render::MeshConfig mesh_config = cube.mesh_config();
    require(mesh_config.vertex_data == cube.vertices.data(),
            "primitive mesh config should point at vertex storage");
    require(mesh_config.vertex_bytes ==
                sizeof(cubey::render::VertexPositionColor) * cube.vertices.size(),
            "primitive mesh config should compute vertex bytes");
    require(mesh_config.index_data == cube.indices.data(),
            "primitive mesh config should point at index storage");
    require(mesh_config.index_type == VK_INDEX_TYPE_UINT16,
            "primitive mesh config should use uint16 indices");
    require(mesh_config.index_count == 36, "primitive mesh config should report index count");
}

void test_primitive_cube_normal_uv_mesh_preserves_normals_and_face_uvs() {
    const cubey::render::PrimitiveMeshData<cubey::render::VertexPositionColorNormalUv> cube =
        cubey::render::make_cube_position_color_normal_uv_mesh();

    require(cube.vertices.size() == 24, "textured cube should have independent face vertices");
    require_vec3(cube.vertices[0].normal, {0.0F, 0.0F, 1.0F},
                 "front cube face should use +Z normal");
    require_vec3(cube.vertices[4].normal, {0.0F, 0.0F, -1.0F},
                 "back cube face should use -Z normal");
    require_vec2(cube.vertices[0].uv, {0.0F, 0.0F}, "first face uv should start at 0,0");
    require_vec2(cube.vertices[2].uv, {1.0F, 1.0F}, "first face uv should include 1,1");
}

void test_primitive_xz_plane_mesh_uses_center_half_extents_and_up_normal() {
    cubey::render::PlaneMeshConfig config;
    config.center = {1.0F, -2.0F, 3.0F};
    config.half_extent_x = 4.0F;
    config.half_extent_z = 5.0F;
    config.color = {0.6F, 0.7F, 0.8F};

    const cubey::render::PrimitiveMeshData<cubey::render::VertexPositionColorNormal> plane =
        cubey::render::make_xz_plane_position_color_normal_mesh(config);

    require(plane.vertices.size() == 4, "plane should have four vertices");
    require(plane.indices.size() == 6, "plane should have two triangles");
    require_vec3(plane.vertices[0].position, {-3.0F, -2.0F, -2.0F},
                 "plane first vertex should use center and half extents");
    require_vec3(plane.vertices[2].position, {5.0F, -2.0F, 8.0F},
                 "plane opposite vertex should use center and half extents");
    require_vec3(plane.vertices[0].normal, {0.0F, 1.0F, 0.0F},
                 "xz plane should use +Y normal");
    require_vec3(plane.vertices[0].color, {0.6F, 0.7F, 0.8F},
                 "plane should apply configured color");
    require(plane.indices[0] == 0 && plane.indices[1] == 1 && plane.indices[2] == 2,
            "plane should use the expected first triangle");
}
