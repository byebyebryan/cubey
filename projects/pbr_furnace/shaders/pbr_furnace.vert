#version 450

layout(set = 0, binding = 0) uniform PbrSceneUniforms {
    mat4 view_projection;
    mat4 light_view_projection;
    vec4 camera_position;
    vec4 light_direction;
    vec4 light_color_intensity;
    vec4 ambient_color_intensity;
    vec4 environment_intensity_mip_count;
    vec4 display_transform;
    vec4 debug_options;
    vec4 diffuse_irradiance_sh[9];
    vec4 environment_options;
} scene;

layout(push_constant) uniform PbrPushConstants {
    mat4 model;
} push_constants;

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec4 in_tangent;
layout(location = 3) in vec2 in_uv0;

layout(location = 0) out vec3 frag_world_position;
layout(location = 1) out vec3 frag_normal;
layout(location = 2) out vec3 frag_tangent;
layout(location = 3) out vec3 frag_bitangent;
layout(location = 4) out vec2 frag_uv0;

void main() {
    vec4 world_position = push_constants.model * vec4(in_position, 1.0);
    mat3 normal_matrix = transpose(inverse(mat3(push_constants.model)));
    vec3 normal = normalize(normal_matrix * in_normal);
    vec3 tangent = normalize(normal_matrix * in_tangent.xyz);
    vec3 bitangent = normalize(cross(normal, tangent) * in_tangent.w);

    gl_Position = scene.view_projection * world_position;
    frag_world_position = world_position.xyz;
    frag_normal = normal;
    frag_tangent = tangent;
    frag_bitangent = bitangent;
    frag_uv0 = in_uv0;
}
