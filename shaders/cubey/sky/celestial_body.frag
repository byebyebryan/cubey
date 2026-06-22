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
    vec4 surface_basis_right;
    vec4 surface_basis_up;
    vec4 surface_basis_forward_options;
} body;

layout(set = 0, binding = 1) uniform sampler2D lunar_atlas;

layout(location = 0) out vec4 out_color;

struct LunarSurfaceSample {
    float albedo;
    vec3 normal;
};

LunarSurfaceSample lunar_surface_sample(vec3 normal) {
    const vec3 basis_right = normalize(body.surface_basis_right.xyz);
    const vec3 basis_up = normalize(body.surface_basis_up.xyz);
    const vec3 basis_forward = normalize(body.surface_basis_forward_options.xyz);
    const vec2 disk_position = vec2(dot(normal, basis_right), dot(normal, basis_up));
    const vec2 uv = clamp(disk_position * 0.5 + 0.5, vec2(0.0), vec2(1.0));
    const vec4 atlas = texture(lunar_atlas, uv);
    vec2 detail_xy = atlas.gb * 2.0 - 1.0;
    detail_xy *= smoothstep(0.0, 0.18, max(dot(normal, basis_forward), 0.0));
    const vec3 detail_normal =
        normalize(normal + basis_right * detail_xy.x * 0.30 + basis_up * detail_xy.y * 0.30);
    const float albedo = clamp((atlas.r - 0.44) * 2.05 + 0.44, 0.16, 0.84);
    return LunarSurfaceSample(albedo, detail_normal);
}

void main() {
    if (body.center_radius.w <= 0.0) {
        discard;
    }

    const vec3 normal = normalize(in_normal);
    const LunarSurfaceSample surface = lunar_surface_sample(normal);
    const vec3 light_direction = normalize(body.light_direction_intensity.xyz);
    const vec3 view_direction = normalize(body.camera_position_options.xyz - in_render_position);
    const float ndotl = dot(surface.normal, light_direction);
    const float ndotv = abs(dot(normal, view_direction));
    const float sky_visibility = clamp(body.visibility_atmosphere.x, 0.0, 1.0);
    const float body_transmittance = mix(1.0, 0.28, sky_visibility);
    const float terminator_width = max(fwidth(ndotl) * 2.5, mix(0.075, 0.135, sky_visibility));
    const float direct = smoothstep(-terminator_width, terminator_width, ndotl);
    const float limb = smoothstep(0.02, 0.34, ndotv);
    const float eclipse_shadow = clamp(body.visibility_atmosphere.y, 0.0, 1.0);
    const float limb_strength = clamp(body.visibility_atmosphere.z, 0.0, 1.0);
    const float detail_strength = clamp(body.camera_position_options.w, 0.0, 1.0);
    const float texture_strength = clamp(body.surface_basis_forward_options.w, 0.0, 1.0);
    const vec3 albedo = in_color * mix(vec3(1.0), vec3(surface.albedo * 1.65),
                                       texture_strength * detail_strength);
    const float ambient = 0.030 * (1.0 - sky_visibility);
    const float lit =
        (ambient + direct * body.light_direction_intensity.w * 0.66) *
        mix(1.0, 0.12, eclipse_shadow);
    const float limb_shade = mix(1.0, mix(0.72, 1.0, limb), limb_strength);
    vec3 shaded = albedo * lit * limb_shade * mix(1.25, 1.05, sky_visibility);
    const float unlit_alpha = 0.035 * (1.0 - sky_visibility);
    const float lit_alpha = 0.96 * body_transmittance;
    const float limb_width = max(fwidth(ndotv) * 3.0, mix(0.055, 0.105, sky_visibility));
    const float limb_alpha = smoothstep(0.0, limb_width, ndotv);
    const float alpha = clamp(mix(unlit_alpha, lit_alpha, direct) * limb_alpha, 0.0, 1.0);
    out_color = vec4(shaded * alpha, alpha);
}
