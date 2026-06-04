#version 450

layout(location = 0) in vec3 in_color;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec2 in_uv;

layout(push_constant) uniform PlanetPushConstants {
    mat4 view_projection;
    vec4 light_direction_debug;
    vec4 options;
} pc;

layout(location = 0) out vec4 out_color;

void main() {
    vec3 normal = normalize(in_normal);
    vec3 light_dir = normalize(pc.light_direction_debug.xyz);
    float ndotl = max(dot(normal, light_dir), 0.0);
    float wrap = max(dot(normal, light_dir) * 0.5 + 0.5, 0.0);
    vec3 sky = vec3(0.06, 0.12, 0.22) * pow(wrap, 1.8);
    vec3 color = in_color * (0.14 + 0.86 * ndotl) + sky;
    out_color = vec4(color, 1.0);
}
