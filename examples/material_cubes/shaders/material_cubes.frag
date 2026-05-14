#version 450

layout(set = 0, binding = 0, std140) uniform MaterialUniforms {
    vec4 base_color;
} material;

layout(location = 0) in vec3 frag_vertex_color;
layout(location = 1) in vec3 frag_normal;
layout(location = 0) out vec4 out_color;

const float kAmbientRadiance = 0.045;
const float kKeyLightRadiance = 0.72;

void main() {
    vec3 normal = normalize(frag_normal);
    vec3 light_dir = normalize(vec3(0.3, 0.7, 0.58));
    float diffuse = max(dot(normal, light_dir), 0.0);
    vec3 base = material.base_color.rgb * mix(vec3(1.0), frag_vertex_color, 0.18);
    out_color = vec4(base * (kAmbientRadiance + kKeyLightRadiance * diffuse),
                     material.base_color.a);
}
