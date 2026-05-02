#version 450

layout(set = 0, binding = 0, std140) uniform SceneUniforms {
    mat4 mvp;
    mat4 model;
    vec4 light_direction;
    vec4 light_color;
    vec4 ambient_color;
} scene;

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_color;
layout(location = 2) in vec3 in_normal;
layout(location = 3) in vec2 in_uv;

layout(location = 0) out vec3 frag_color;
layout(location = 1) out vec2 frag_uv;
layout(location = 2) out vec3 frag_normal;

void main() {
    gl_Position = scene.mvp * vec4(in_position, 1.0);
    frag_color = in_color;
    frag_uv = in_uv;
    frag_normal = normalize(mat3(scene.model) * in_normal);
}
