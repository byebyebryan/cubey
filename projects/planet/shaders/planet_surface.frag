#version 450

layout(location = 0) in vec3 in_color;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec2 in_uv;
layout(location = 3) in vec3 in_render_position;

layout(set = 0, binding = 0) uniform PlanetSurfaceFrame {
    mat4 view_projection;
    vec4 light_direction_debug;
    vec4 render_origin_radius;
    vec4 surface_options;
    vec4 terrain_options;
    vec4 camera_horizon;
    vec4 atmosphere_options;
    vec4 haze_color_direct;
} surface_frame;

layout(location = 0) out vec4 out_color;

void main() {
    vec3 normal = normalize(in_normal);
    vec3 light_dir = normalize(surface_frame.light_direction_debug.xyz);
    float ndotl = max(dot(normal, light_dir), 0.0);
    float wrap = max(dot(normal, light_dir) * 0.5 + 0.5, 0.0);
    vec3 haze_color = surface_frame.haze_color_direct.rgb;
    float direct_intensity = max(surface_frame.haze_color_direct.w, 0.0);
    float ambient_intensity = max(surface_frame.atmosphere_options.x, 0.0);
    vec3 sky = haze_color * pow(wrap, 1.8) * 0.18;
    vec3 color = in_color * (haze_color * ambient_intensity + direct_intensity * ndotl) + sky;
    float edge = min(min(in_uv.x, in_uv.y), min(1.0 - in_uv.x, 1.0 - in_uv.y));
    float wire = 1.0 - smoothstep(0.0, 0.015, edge);
    float wire_enabled = fract(surface_frame.surface_options.x) > 0.1 ? 1.0 : 0.0;
    color = mix(color, vec3(0.92, 0.96, 1.0), wire * wire_enabled);
    float view_distance = length(in_render_position - surface_frame.camera_horizon.xyz);
    float horizon_distance = max(surface_frame.camera_horizon.w, 1.0);
    float haze_strength = clamp(surface_frame.atmosphere_options.y, 0.0, 1.0);
    float haze_start = clamp(surface_frame.atmosphere_options.z, 0.0, 1.0);
    float haze_end = clamp(max(surface_frame.atmosphere_options.w, haze_start + 0.001), 0.0, 1.5);
    float haze = smoothstep(horizon_distance * haze_start, horizon_distance * haze_end,
                            view_distance) * haze_strength;
    float final_view = floor(surface_frame.surface_options.x) < 0.5 ? 1.0 : 0.0;
    color = mix(color, haze_color, haze * final_view);
    out_color = vec4(color, 1.0);
}
