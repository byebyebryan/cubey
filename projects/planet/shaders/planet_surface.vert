#version 450

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_color;
layout(location = 2) in vec3 in_normal;
layout(location = 3) in vec2 in_uv;

layout(push_constant) uniform PlanetPushConstants {
    mat4 view_projection;
    vec4 light_direction_debug;
    vec4 options;
} pc;

layout(location = 0) out vec3 out_color;
layout(location = 1) out vec3 out_normal;
layout(location = 2) out vec2 out_uv;

void main() {
    out_color = in_color;
    out_normal = normalize(in_normal);
    out_uv = in_uv;
    gl_Position = pc.view_projection * vec4(in_position, 1.0);
}
