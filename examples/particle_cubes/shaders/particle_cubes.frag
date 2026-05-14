#version 450

layout(location = 0) in vec3 frag_color;
layout(location = 1) in vec3 frag_normal;

layout(location = 0) out vec4 out_color;

const vec3 kAmbientRadiance = vec3(0.045);
const vec3 kKeyLightRadiance = vec3(0.72);

void main() {
    vec3 normal = normalize(frag_normal);
    vec3 light_dir = normalize(vec3(0.35, 0.68, 0.64));
    float diffuse = max(dot(normal, light_dir), 0.0);
    vec3 lit = frag_color * (kAmbientRadiance + kKeyLightRadiance * diffuse);
    out_color = vec4(lit, 1.0);
}
