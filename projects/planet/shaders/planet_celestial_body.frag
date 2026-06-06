#version 450

layout(location = 0) in vec3 in_normal;
layout(location = 1) in vec3 in_color;
layout(location = 2) in vec3 in_render_position;

layout(set = 0, binding = 0) uniform PlanetCelestialBodyFrame {
    mat4 view_projection;
    vec4 center_radius;
    vec4 camera_position_options;
    vec4 light_direction_intensity;
    vec4 color_phase;
    vec4 visibility_atmosphere;
} body;

layout(location = 0) out vec4 out_color;

float moon_surface_detail(vec3 normal) {
    const vec3 n = normalize(normal);
    const float maria =
        sin(dot(n, normalize(vec3(0.73, -0.18, 0.66))) * 18.0) *
        sin(dot(n, normalize(vec3(-0.22, 0.91, 0.35))) * 13.0);
    const float highlands =
        sin(dot(n, normalize(vec3(0.41, 0.57, -0.71))) * 44.0) * 0.5 + 0.5;
    const float crater_seed =
        sin(dot(n, normalize(vec3(-0.64, 0.28, 0.71))) * 72.0) *
        sin(dot(n, normalize(vec3(0.33, -0.78, 0.53))) * 69.0);
    const float crater = smoothstep(0.82, 0.98, crater_seed) * 0.16;
    return clamp((maria * 0.16) + (highlands * 0.10) - crater, -0.22, 0.22);
}

void main() {
    if (body.center_radius.w <= 0.0) {
        discard;
    }

    const vec3 normal = normalize(in_normal);
    const vec3 light_direction = normalize(body.light_direction_intensity.xyz);
    const vec3 view_direction = normalize(body.camera_position_options.xyz - in_render_position);
    const float direct = smoothstep(-0.035, 0.055, dot(normal, light_direction));
    const float limb = smoothstep(0.02, 0.34, dot(normal, view_direction));
    const float eclipse_shadow = clamp(body.visibility_atmosphere.y, 0.0, 1.0);
    const float limb_strength = clamp(body.visibility_atmosphere.z, 0.0, 1.0);
    const float detail_strength = clamp(body.camera_position_options.w, 0.0, 1.0);
    const vec3 albedo = in_color * (1.0 + moon_surface_detail(normal) * detail_strength);
    const float ambient = 0.040;
    const float lit =
        (ambient + direct * body.light_direction_intensity.w * 0.66) *
        mix(1.0, 0.12, eclipse_shadow);
    const float limb_shade = mix(1.0, mix(0.72, 1.0, limb), limb_strength);
    const float washout = clamp(body.visibility_atmosphere.x, 0.0, 1.0);
    vec3 shaded = albedo * lit * limb_shade;
    const vec3 sky_wash = vec3(0.42, 0.54, 0.72) *
                          (0.38 + body.light_direction_intensity.w * 0.10);
    const vec3 low_contrast = mix(sky_wash, shaded, 0.18);
    out_color = vec4(mix(shaded, low_contrast, washout), 1.0);
}
