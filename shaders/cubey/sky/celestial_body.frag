#version 450
#extension GL_GOOGLE_include_directive : require

#include "cubey/color_space.glsl"

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

layout(set = 0, binding = 1) uniform sampler2D lunar_surface_map;

layout(location = 0) out vec4 out_color;

struct LunarSurfaceSample {
    float albedo;
    vec3 normal;
};

const float kPi = 3.14159265358979323846;
const float kInvTwoPi = 1.0 / (kPi * 2.0);
const float kInvPi = 1.0 / kPi;
const int kCelestialBodyShadingLit = 0;
const int kCelestialBodyShadingSurfaceDebug = 1;

LunarSurfaceSample lunar_surface_sample(vec3 normal, vec3 view_direction, bool force_base_lod) {
    const vec3 basis_right = normalize(body.surface_basis_right.xyz);
    const vec3 basis_up = normalize(body.surface_basis_up.xyz);
    const vec3 basis_forward = normalize(body.surface_basis_forward_options.xyz);

    const vec3 sample_normal = -normal;
    const vec3 local = normalize(vec3(dot(sample_normal, basis_forward),
                                      -dot(sample_normal, basis_right),
                                      dot(sample_normal, basis_up)));
    const vec2 uv = vec2(fract(atan(local.y, local.x) * kInvTwoPi + 0.5),
                         clamp(0.5 - asin(clamp(local.z, -1.0, 1.0)) * kInvPi, 0.0, 1.0));
    const vec4 surface =
        force_base_lod ? textureLod(lunar_surface_map, uv, 0.0) : texture(lunar_surface_map, uv);
    vec2 detail_xy = surface.gb * 2.0 - 1.0;
    detail_xy *= smoothstep(0.0, 0.18, max(dot(normal, view_direction), 0.0));
    const vec3 detail_normal =
        normalize(normal + basis_right * detail_xy.x * 0.34 + basis_up * detail_xy.y * 0.34);
    const float albedo = clamp((surface.r - 0.43) * 1.55 + 0.43, 0.16, 0.86);
    return LunarSurfaceSample(albedo, detail_normal);
}

vec3 lunar_surface_debug_color(LunarSurfaceSample surface, vec3 normal, vec3 view_direction) {
    const float albedo = clamp((surface.albedo - 0.12) / 0.78, 0.0, 1.0);
    const float maria_preserving_albedo = pow(clamp((albedo - 0.20) * 1.22 + 0.26, 0.0, 1.0),
                                              1.04);
    const vec3 debug_light = normalize(vec3(-0.45, 0.55, 0.70));
    const vec3 tangent_relief = surface.normal - normal * dot(surface.normal, normal);
    const float relief = dot(tangent_relief, debug_light) * 1.45;
    const float limb = smoothstep(0.01, 0.16, abs(dot(normal, view_direction)));
    const float value = clamp(mix(0.27, 0.62, maria_preserving_albedo) + relief, 0.18, 0.72);
    return cubey_srgb_to_linear(vec3(value * limb));
}

void main() {
    if (body.center_radius.w <= 0.0) {
        discard;
    }

    const vec3 normal = normalize(in_normal);
    const vec3 view_direction = normalize(body.camera_position_options.xyz - in_render_position);
    const int shading_mode = int(body.visibility_atmosphere.w + 0.5);
    const LunarSurfaceSample surface =
        lunar_surface_sample(normal, view_direction, shading_mode == kCelestialBodyShadingSurfaceDebug);
    const vec3 light_direction = normalize(body.light_direction_intensity.xyz);
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
    if (shading_mode == kCelestialBodyShadingSurfaceDebug) {
        out_color = vec4(lunar_surface_debug_color(surface, normal, view_direction), 1.0);
        return;
    }

    const vec3 moon_tint = mix(vec3(0.82, 0.80, 0.74), in_color, 0.38);
    const float surface_tone = clamp(surface.albedo * 1.60 + 0.035, 0.24, 1.0);
    const vec3 albedo = moon_tint * mix(vec3(0.94), vec3(surface_tone),
                                        texture_strength * detail_strength);
    const float ambient = mix(0.016, 0.008, sky_visibility);
    const float lit =
        (ambient + direct * body.light_direction_intensity.w * mix(0.72, 0.55, sky_visibility)) *
        mix(1.0, 0.12, eclipse_shadow);
    const float limb_shade = mix(1.0, mix(0.72, 1.0, limb), limb_strength);
    vec3 shaded = albedo * lit * limb_shade * mix(1.18, 1.03, sky_visibility);
    const float unlit_alpha = 0.012 * (1.0 - sky_visibility);
    const float lit_alpha = mix(0.94, 0.88, sky_visibility) * body_transmittance;
    const float limb_width = max(fwidth(ndotv) * 3.0, mix(0.055, 0.105, sky_visibility));
    const float limb_alpha = smoothstep(0.0, limb_width, ndotv);
    const float alpha = clamp(mix(unlit_alpha, lit_alpha, direct) * limb_alpha, 0.0, 1.0);
    out_color = vec4(shaded * alpha, alpha);
}
