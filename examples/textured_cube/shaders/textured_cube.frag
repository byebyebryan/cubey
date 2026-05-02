#version 450

layout(set = 0, binding = 0) uniform sampler2D cube_texture;

layout(location = 0) in vec3 frag_color;
layout(location = 1) in vec2 frag_uv;
layout(location = 2) in vec3 frag_normal;
layout(location = 0) out vec4 out_color;

void main() {
    vec4 sampled = texture(cube_texture, frag_uv);
    vec3 normal = normalize(frag_normal);
    vec3 light_dir = normalize(vec3(0.35, -0.55, 0.76));
    float diffuse = max(dot(normal, light_dir), 0.0);
    float light = 0.24 + 0.76 * diffuse;
    out_color = vec4(frag_color * sampled.rgb * light, sampled.a);
}
