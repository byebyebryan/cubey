#version 450

layout(location = 0) in vec3 in_normal;
layout(location = 1) in vec3 in_color;

layout(set = 0, binding = 0) uniform PlanetCelestialBodyFrame {
    mat4 view_projection;
    vec4 center_radius;
    vec4 light_direction_intensity;
    vec4 color_phase;
} body;

layout(location = 0) out vec4 out_color;

void main() {
    if (body.center_radius.w <= 0.0) {
        discard;
    }

    const vec3 normal = normalize(in_normal);
    const vec3 light_direction = normalize(body.light_direction_intensity.xyz);
    const float direct = max(dot(normal, light_direction), 0.0);
    const float ambient = 0.045;
    const float lit = ambient + direct * body.light_direction_intensity.w * 0.62;
    out_color = vec4(in_color * lit, 1.0);
}
