#version 450

layout(set = 0, binding = 0, std140) uniform SceneUniforms {
    mat4 mvp;
    mat4 model;
    vec4 light_direction;
    vec4 light_color;
    vec4 ambient_color;
} scene;

layout(set = 0, binding = 1) uniform sampler2D cube_texture;

layout(location = 0) in vec3 frag_color;
layout(location = 1) in vec2 frag_uv;
layout(location = 2) in vec3 frag_normal;
layout(location = 0) out vec4 out_color;

void main() {
    vec4 sampled = texture(cube_texture, frag_uv);
    vec3 normal = normalize(frag_normal);
    vec3 light_dir = normalize(scene.light_direction.xyz);
    float diffuse = max(dot(normal, light_dir), 0.0);
    vec3 light = scene.ambient_color.rgb + scene.light_color.rgb * diffuse;
    out_color = vec4(frag_color * sampled.rgb * light, sampled.a);
}
