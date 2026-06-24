#version 450

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_color;
layout(location = 2) in vec3 in_normal;
layout(location = 3) in vec2 in_uv;

layout(set = 0, binding = 0) uniform PlanetCelestialBodyFrame {
    mat4 view_projection;
    vec4 center_radius;
    vec4 camera_position_options;
    vec4 light_direction_intensity;
    vec4 color_phase;
    vec4 visibility_atmosphere;
    vec4 surface_basis_right;
    vec4 surface_basis_up;
    vec4 surface_basis_forward_options;
} body;

layout(location = 0) out vec3 out_normal;
layout(location = 1) out vec3 out_color;
layout(location = 2) out vec3 out_render_position;

void main() {
    const float radius = max(body.center_radius.w, 0.0);
    const vec3 render_position = body.center_radius.xyz + in_position * radius;
    out_normal = normalize(in_normal);
    out_color = body.color_phase.rgb;
    out_render_position = render_position;
    gl_Position = body.view_projection * vec4(render_position, 1.0);
}
