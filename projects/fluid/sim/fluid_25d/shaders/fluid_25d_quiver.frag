#version 450
#extension GL_GOOGLE_include_directive : require

#include "cubey/color_space.glsl"

layout(location = 0) in vec2 frag_local;
layout(location = 1) flat in uint frag_part;
layout(location = 2) in float frag_speed_fraction;
layout(location = 3) in float frag_opacity;
layout(location = 0) out vec4 out_color;

void main() {
    if (frag_opacity <= 0.0) {
        discard;
    }
    // Both portions use derivative-scaled coverage so the thin shaft and the
    // distinct triangular head remain stable at overview resolution.
    float lateral_limit = frag_part == 0u
                              ? 0.10
                              : 0.27 * clamp((0.70 - frag_local.x) / (0.70 - 0.04), 0.0, 1.0);
    float normalized_edge = abs(frag_local.y) / max(lateral_limit, 1.0e-4);
    float edge_aa = max(fwidth(normalized_edge), 0.015);
    float coverage = 1.0 - smoothstep(1.0 - edge_aa, 1.0 + edge_aa, normalized_edge);
    float speed = clamp(frag_speed_fraction, 0.0, 1.0);
    vec3 slow = cubey_srgb_to_linear(vec3(0.38, 0.73, 1.0));
    vec3 fast = cubey_srgb_to_linear(vec3(1.0, 0.95, 0.62));
    vec3 color = mix(slow, fast, speed);
    float alpha = coverage * frag_opacity * mix(0.58, 0.90, speed);
    out_color = vec4(color * alpha, alpha);
}
