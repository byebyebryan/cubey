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
    const float ndotl = dot(normal, light_direction);
    const float ndotv = abs(dot(normal, view_direction));
    const float washout = clamp(body.visibility_atmosphere.x, 0.0, 1.0);
    const float terminator_width = max(fwidth(ndotl) * 2.5, mix(0.075, 0.135, washout));
    const float direct = smoothstep(-terminator_width, terminator_width, ndotl);
    const float limb = smoothstep(0.02, 0.34, ndotv);
    const float eclipse_shadow = clamp(body.visibility_atmosphere.y, 0.0, 1.0);
    const float limb_strength = clamp(body.visibility_atmosphere.z, 0.0, 1.0);
    const float detail_strength = clamp(body.camera_position_options.w, 0.0, 1.0);
    const vec3 albedo = in_color * (1.0 + moon_surface_detail(normal) * detail_strength);
    const float ambient = mix(0.040, 0.004, washout);
    const float lit =
        (ambient + direct * body.light_direction_intensity.w * 0.66) *
        mix(1.0, 0.12, eclipse_shadow);
    const float limb_shade = mix(1.0, mix(0.72, 1.0, limb), limb_strength);
    vec3 shaded = albedo * lit * limb_shade * mix(1.35, 1.75, washout);
    const float unlit_alpha = mix(0.040, 0.0, washout);
    const float lit_alpha = mix(0.96, 0.30, washout);
    const float limb_width = max(fwidth(ndotv) * 3.0, mix(0.055, 0.105, washout));
    const float limb_alpha = smoothstep(0.0, limb_width, ndotv);
    const float alpha = clamp(mix(unlit_alpha, lit_alpha, direct) * limb_alpha, 0.0, 1.0);
    out_color = vec4(shaded * alpha, alpha);
}
