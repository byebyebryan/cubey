#version 450
#extension GL_GOOGLE_include_directive : require

#include "water_3d_surface_common.glsl"

WATER3D_SURFACE_PARAMS;

layout(location = 0) in vec2 frag_local;
layout(location = 1) in float frag_kind;
layout(location = 2) in float frag_age;
layout(location = 3) in float frag_energy;
layout(location = 4) in float frag_linear_depth;
layout(location = 5) in float frag_radius_px;
layout(location = 6) in float frag_seed;
layout(location = 0) out vec4 out_color;

float hash12(vec2 value) {
    return fract(sin(dot(value, vec2(127.1, 311.7))) * 43758.5453);
}

void main() {
    float radius = length(frag_local);
    if (radius > 1.0) {
        discard;
    }

    float blur_px = clamp(surface_params.filter_options.w, 0.0, 12.0);
    float softness = clamp(blur_px / 8.0, 0.0, 1.0);
    float anti_alias = fwidth(radius) * 1.5;
    float edge_width = min(0.90, max(anti_alias, blur_px / max(1.0, frag_radius_px)));
    float spray = step(0.5, frag_kind) * (1.0 - step(1.5, frag_kind));
    float bubble = step(1.5, frag_kind);
    float angle = atan(frag_local.y, frag_local.x);
    float lobe = sin(angle * 5.0 + frag_seed * 19.1) * 0.09 +
                 sin(angle * 9.0 - frag_seed * 31.7) * 0.045;
    float fleck_radius = radius / max(0.35, 1.0 + lobe);
    float body_radius = mix(0.58, 0.44, spray);
    body_radius = mix(body_radius, 0.38, bubble);
    float hard_body =
        1.0 - smoothstep(max(0.0, body_radius - anti_alias), body_radius + anti_alias,
                         fleck_radius);
    float soft_body =
        1.0 - smoothstep(max(0.0, body_radius - edge_width), body_radius + edge_width,
                         fleck_radius);
    float body = mix(hard_body, soft_body, softness);
    float core_radius = mix(0.68, 0.50, step(0.5, frag_kind));
    float core_width = max(anti_alias, edge_width * 0.55);
    float core = 1.0 - smoothstep(max(0.0, core_radius - core_width),
                                  min(1.0, core_radius + core_width), fleck_radius);
    float cell_noise = hash12(floor((frag_local + vec2(1.0)) * vec2(7.0, 5.0)) +
                              vec2(frag_seed * 17.0, frag_seed * 29.0));
    float chip = mix(step(0.22, cell_noise), 1.0, softness);
    float breakup = 0.82 + 0.18 * sin(dot(frag_local, vec2(19.7, -31.3)) + frag_energy * 5.1);
    float life = smoothstep(0.0, 0.08, frag_age) * (1.0 - smoothstep(0.58, 1.0, frag_age));
    float alpha = mix(0.22, 0.30, spray) * mix(body, core, spray * 0.35) * chip * life *
                  breakup;
    alpha *= mix(1.15, 0.72, softness);
    alpha *= mix(0.65, 1.05, clamp(frag_energy, 0.0, 1.0));
    alpha *= mix(1.0, 0.42, bubble);

    float foam_weight = mix(1.0, 0.58, spray);
    foam_weight = mix(foam_weight, 0.28, bubble);
    out_color = vec4(alpha * foam_weight, alpha * frag_kind, alpha * frag_linear_depth, alpha);
}
