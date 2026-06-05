#version 450

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_color;
layout(location = 2) in vec3 in_normal;
layout(location = 3) in vec2 in_uv;

layout(set = 0, binding = 0) uniform PlanetCelestialBodyFrame {
    mat4 view_projection;
    vec4 center_radius;
    vec4 light_direction_intensity;
    vec4 color_phase;
    vec4 visibility_atmosphere;
} body;

layout(location = 0) out vec3 out_normal;
layout(location = 1) out vec3 out_color;

void main() {
    const float radius = max(body.center_radius.w, 0.0);
    const vec3 render_position = body.center_radius.xyz + in_position * radius;
    out_normal = normalize(in_normal);
    out_color = body.color_phase.rgb;
    gl_Position = body.view_projection * vec4(render_position, 1.0);
}
