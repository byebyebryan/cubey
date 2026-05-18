#include <cubey/render/color_space.h>
#include <cubey/render/instance_buffer.h>
#include <cubey/render/primitive_mesh.h>

#include <vulkan/vulkan.h>

#include <cstddef>
#include <cstdint>
#include <cmath>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void require_vec3(std::array<float, 3> value, std::array<float, 3> expected, const char* message) {
    require(value == expected, message);
}

void require_vec2(std::array<float, 2> value, std::array<float, 2> expected, const char* message) {
    require(value == expected, message);
}

void require_close(float actual, float expected, const char* message) {
    if (std::fabs(actual - expected) > 0.0001F) {
        throw std::runtime_error(message);
    }
}

void require_vec3_close(std::array<float, 3> value, std::array<float, 3> expected,
                        const char* message) {
    require_close(value[0], expected[0], message);
    require_close(value[1], expected[1], message);
    require_close(value[2], expected[2], message);
}

} // namespace

void test_primitive_vertex_layouts_match_shader_contracts() {
    const cubey::render::VertexInputLayout position_color =
        cubey::render::vertex_position_color_input_layout();
    require(position_color.bindings().size() == 1,
            "position-color layout should expose one vertex binding");
    require(position_color.bindings()[0].stride == sizeof(cubey::render::VertexPositionColor),
            "position-color binding stride should match vertex size");
    require(position_color.bindings()[0].inputRate == VK_VERTEX_INPUT_RATE_VERTEX,
            "position-color binding should be per-vertex");
    require(position_color.attributes.size() == 2,
            "position-color layout should expose position and color");
    require(position_color.attributes[0].location == 0, "position should use shader location 0");
    require(position_color.attributes[0].format == VK_FORMAT_R32G32B32_SFLOAT,
            "position should use vec3 float format");
    require(position_color.attributes[0].offset ==
                offsetof(cubey::render::VertexPositionColor, position),
            "position offset should match vertex field");
    require(position_color.attributes[1].location == 1, "color should use shader location 1");
    require(position_color.attributes[1].format == VK_FORMAT_R32G32B32_SFLOAT,
            "color should use vec3 float format");
    require(position_color.attributes[1].offset ==
                offsetof(cubey::render::VertexPositionColor, color),
            "color offset should match vertex field");

    const cubey::render::VertexInputLayout position_color_normal =
        cubey::render::vertex_position_color_normal_input_layout();
    require(position_color_normal.bindings().size() == 1,
            "position-color-normal layout should expose one vertex binding");
    require(position_color_normal.bindings()[0].stride ==
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
    require(position_color_normal_uv.bindings().size() == 1,
            "position-color-normal-uv layout should expose one vertex binding");
    require(position_color_normal_uv.bindings()[0].stride ==
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
    require(position_only.bindings().size() == 1,
            "position-only layout should expose one vertex binding");
    require(position_only.bindings()[0].stride == sizeof(cubey::render::VertexPositionColorNormal),
            "position-only binding stride should use caller-provided vertex size");
    require(position_only.bindings()[0].inputRate == VK_VERTEX_INPUT_RATE_VERTEX,
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

void test_instance_buffer_helpers_describe_instance_vertex_data() {
    struct InstanceData {
        std::array<float, 4> color{};
    };

    const VkVertexInputBindingDescription binding =
        cubey::render::instance_input_binding<InstanceData>(1);
    require(binding.binding == 1, "instance input binding should preserve binding index");
    require(binding.stride == sizeof(InstanceData),
            "instance input binding should use instance data stride");
    require(binding.inputRate == VK_VERTEX_INPUT_RATE_INSTANCE,
            "instance input binding should be per-instance");

    const VkVertexInputAttributeDescription attribute = cubey::render::vertex_input_attribute(
        4, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(InstanceData, color));
    require(attribute.location == 4, "vertex input attribute should preserve location");
    require(attribute.binding == 1, "vertex input attribute should preserve binding");
    require(cubey::render::instance_buffer_byte_size(3, sizeof(InstanceData)) ==
                sizeof(InstanceData) * 3,
            "instance buffer byte size should scale by instance count");

    bool threw = false;
    try {
        static_cast<void>(cubey::render::instance_buffer_byte_size(0, sizeof(InstanceData)));
    } catch (const std::runtime_error&) {
        threw = true;
    }
    require(threw, "instance buffer byte size should reject empty instance data");
}

void test_color_space_converts_srgb_authored_values_to_linear() {
    require_close(cubey::render::srgb_channel_to_linear(0.0F), 0.0F,
                  "black should stay black");
    require_close(cubey::render::srgb_channel_to_linear(1.0F), 1.0F,
                  "white should stay white");
    require_close(cubey::render::srgb_channel_to_linear(0.5F), 0.214041F,
                  "middle gray should be converted from sRGB to linear");
    require_close(cubey::render::srgb_channel_to_linear(0.04045F), 0.0031308F,
                  "sRGB knee should use the linear segment");

    const std::array<float, 3> linear =
        cubey::render::srgb_to_linear_rgb({0.5F, 0.25F, 1.0F});
    require_vec3_close(linear, {0.214041F, 0.050876F, 1.0F},
                       "sRGB RGB helper should convert each color channel");

    const cubey::math::Vec4 rgba =
        cubey::render::srgb_to_linear_rgba({0.5F, 0.25F, 1.0F, 0.75F});
    require_close(rgba.r, 0.214041F, "sRGB RGBA helper should convert red");
    require_close(rgba.g, 0.050876F, "sRGB RGBA helper should convert green");
    require_close(rgba.b, 1.0F, "sRGB RGBA helper should convert blue");
    require_close(rgba.a, 0.75F, "sRGB RGBA helper should preserve alpha");
}

void test_color_space_converts_hsv_and_hsl_authored_values() {
    require_close(cubey::render::wrap_unit(1.25F), 0.25F,
                  "unit wrapping should wrap positive hue values");
    require_close(cubey::render::wrap_unit(-0.25F), 0.75F,
                  "unit wrapping should wrap negative hue values");

    require_vec3_close(cubey::render::hsv_to_srgb({.hue = 0.0F, .saturation = 1.0F,
                                                   .value = 1.0F}),
                       {1.0F, 0.0F, 0.0F}, "HSV hue 0 should produce red");
    require_vec3_close(cubey::render::hsv_to_srgb({.hue = 1.0F / 3.0F, .saturation = 1.0F,
                                                   .value = 1.0F}),
                       {0.0F, 1.0F, 0.0F}, "HSV hue one-third should produce green");
    require_vec3_close(cubey::render::hsv_to_srgb({.hue = 2.0F / 3.0F, .saturation = 1.0F,
                                                   .value = 1.0F}),
                       {0.0F, 0.0F, 1.0F}, "HSV hue two-thirds should produce blue");
    require_vec3_close(cubey::render::hsv_to_srgb({.hue = 1.25F, .saturation = 0.5F,
                                                   .value = 0.8F}),
                       {0.6F, 0.8F, 0.4F}, "HSV helper should wrap hue and preserve value");
    require_vec3_close(cubey::render::hsv_to_srgb({.hue = 0.8F, .saturation = 0.0F,
                                                   .value = 0.35F}),
                       {0.35F, 0.35F, 0.35F}, "HSV zero saturation should be grayscale");

    require_vec3_close(cubey::render::hsl_to_srgb({.hue = 0.0F, .saturation = 1.0F,
                                                   .lightness = 0.5F}),
                       {1.0F, 0.0F, 0.0F}, "HSL hue 0 should produce red at mid lightness");
    require_vec3_close(cubey::render::hsl_to_srgb({.hue = 1.0F / 3.0F, .saturation = 1.0F,
                                                   .lightness = 0.5F}),
                       {0.0F, 1.0F, 0.0F}, "HSL hue one-third should produce green");
    require_vec3_close(cubey::render::hsl_to_srgb({.hue = 2.0F / 3.0F, .saturation = 1.0F,
                                                   .lightness = 0.5F}),
                       {0.0F, 0.0F, 1.0F}, "HSL hue two-thirds should produce blue");
    require_vec3_close(cubey::render::hsl_to_srgb({.hue = 0.5F, .saturation = 0.5F,
                                                   .lightness = 0.4F}),
                       {0.2F, 0.6F, 0.6F}, "HSL helper should preserve authored lightness");

    require_vec3_close(cubey::render::hsv_to_linear_rgb({.hue = 0.0F, .saturation = 0.0F,
                                                         .value = 0.5F}),
                       {0.214041F, 0.214041F, 0.214041F},
                       "HSV linear helper should convert generated sRGB values to linear");
    require_vec3_close(cubey::render::hsl_to_linear_rgb({.hue = 0.0F, .saturation = 0.0F,
                                                         .lightness = 0.5F}),
                       {0.214041F, 0.214041F, 0.214041F},
                       "HSL linear helper should convert generated sRGB values to linear");
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
    require_vec3_close(cube.vertices[0].color,
                       cubey::render::srgb_to_linear_rgb({0.1F, 0.2F, 0.3F}),
                       "cube should linearize authored face color");
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
    require_vec3(plane.vertices[0].normal, {0.0F, 1.0F, 0.0F}, "xz plane should use +Y normal");
    require_vec3_close(plane.vertices[0].color,
                       cubey::render::srgb_to_linear_rgb({0.6F, 0.7F, 0.8F}),
                       "plane should linearize authored color");
    require(plane.indices[0] == 0 && plane.indices[1] == 1 && plane.indices[2] == 2,
            "plane should use the expected first triangle");
}

void test_primitive_uv_sphere_mesh_uses_smooth_normals_and_uv_grid() {
    cubey::render::SphereMeshConfig config;
    config.radius = 2.0F;
    config.latitude_segments = 4;
    config.longitude_segments = 8;
    config.color = {0.7F, 0.8F, 0.9F};

    const cubey::render::PrimitiveMeshData<cubey::render::VertexPositionColorNormalUv> sphere =
        cubey::render::make_uv_sphere_position_color_normal_uv_mesh(config);

    require(sphere.vertices.size() == 45,
            "sphere should include one UV seam vertex per latitude row");
    require(sphere.indices.size() == 192, "sphere should emit two triangles per grid cell");
    require_vec3_close(sphere.vertices.front().position, {0.0F, 2.0F, 0.0F},
                       "sphere first vertex should sit on the positive Y pole");
    require_vec3_close(sphere.vertices.front().normal, {0.0F, 1.0F, 0.0F},
                       "sphere pole normal should be unit length and radial");
    require_vec2(sphere.vertices.front().uv, {0.0F, 0.0F},
                 "sphere first vertex should start the UV domain");
    require_vec2(sphere.vertices[8].uv, {1.0F, 0.0F},
                 "sphere seam vertex should close each latitude row at u=1");
    for (const auto& vertex : sphere.vertices) {
        require_vec3_close(vertex.color, cubey::render::srgb_to_linear_rgb(config.color),
                           "sphere should linearize authored vertex color");
        const float normal_length =
            std::sqrt(vertex.normal[0] * vertex.normal[0] + vertex.normal[1] * vertex.normal[1] +
                      vertex.normal[2] * vertex.normal[2]);
        require_close(normal_length, 1.0F, "sphere normals should be normalized");
    }
    for (const std::uint16_t index : sphere.indices) {
        require(index < sphere.vertices.size(), "sphere indices should stay in vertex range");
    }
}
