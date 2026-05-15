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
} scene;

layout(push_constant) uniform PbrPushConstants {
    mat4 model;
} push_constants;

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec4 in_tangent;
layout(location = 3) in vec2 in_uv0;
layout(location = 4) in vec2 in_uv1;
layout(location = 5) in vec4 in_color0;

layout(location = 0) out vec3 frag_world_position;
layout(location = 1) out vec3 frag_normal;
layout(location = 2) out vec3 frag_tangent;
layout(location = 3) out vec3 frag_bitangent;
layout(location = 4) out vec2 frag_uv0;
layout(location = 5) out vec2 frag_uv1;
layout(location = 6) out vec4 frag_color0;
layout(location = 7) out vec4 frag_shadow_position;

vec3 safeNormalize(vec3 value, vec3 fallback) {
    const float length_squared = dot(value, value);
    return length_squared > 1.0e-10 ? value * inversesqrt(length_squared) : fallback;
}

vec3 orthogonalizeTangent(vec3 tangent, vec3 normal) {
    return safeNormalize(tangent - normal * dot(normal, tangent), vec3(1.0, 0.0, 0.0));
}

void main() {
    vec4 world_position = push_constants.model * vec4(in_position, 1.0);
    mat3 model = mat3(push_constants.model);
    mat3 normal_matrix = transpose(inverse(mat3(push_constants.model)));
    vec3 normal = safeNormalize(normal_matrix * in_normal, vec3(0.0, 1.0, 0.0));
    vec3 tangent = orthogonalizeTangent(mat3(model) * in_tangent.xyz, normal);
    vec3 bitangent = safeNormalize(cross(normal, tangent) * in_tangent.w, vec3(0.0, 0.0, 1.0));

    gl_Position = scene.view_projection * world_position;
    frag_world_position = world_position.xyz;
    frag_normal = normal;
    frag_tangent = tangent;
    frag_bitangent = bitangent;
    frag_uv0 = in_uv0;
    frag_uv1 = in_uv1;
    frag_color0 = in_color0;
    frag_shadow_position = scene.light_view_projection * world_position;
}
