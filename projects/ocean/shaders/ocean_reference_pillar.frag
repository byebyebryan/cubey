#version 450

layout(push_constant) uniform ReferencePillarParams {
    mat4 view_projection;
    vec4 light_direction;
} pillar;

layout(location = 0) in vec3 frag_color;
layout(location = 1) in vec3 frag_normal;

layout(location = 0) out vec4 out_color;

void main() {
    vec3 normal = normalize(frag_normal);
    vec3 light_dir = normalize(pillar.light_direction.xyz);
    float light_energy = clamp(pillar.light_direction.w, 0.0, 1.0);
    float ndotl = max(dot(normal, light_dir), 0.0);
    float ambient = mix(0.36, 0.28, light_energy);
    float diffuse = ndotl * mix(0.42, 1.05, light_energy);
    float shade = ambient + diffuse;
    vec3 color = frag_color * shade;
    out_color = vec4(color, 1.0);
}
