#version 450
#extension GL_GOOGLE_include_directive : require

#include "cubey/color_space.glsl"

layout(location = 0) in vec2 frag_local;
layout(location = 1) in float frag_visibility;
layout(location = 0) out vec4 out_color;

void main() {
    if (frag_visibility <= 0.0) {
        discard;
    }
    float lateral_edge = 1.0 - smoothstep(0.72, 1.0, abs(frag_local.y));
    float headness = clamp(frag_local.x * 0.5 + 0.5, 0.0, 1.0);
    // Flow Inspection is an explicit reading surface. The field remains
    // sparse, so a strong but tapered directional glyph is clearer here than
    // another low-contrast water shimmer.
    float alpha = lateral_edge * frag_visibility * mix(0.45, 0.85, headness);
    vec3 tail = cubey_srgb_to_linear(vec3(0.96, 0.50, 0.12));
    vec3 head = cubey_srgb_to_linear(vec3(1.0, 0.94, 0.74));
    vec3 color = mix(tail, head, headness);
    // Premultiplied source-over: the project-local graphics pass uses ONE /
    // ONE_MINUS_SRC_ALPHA just like the water surface below it.
    out_color = vec4(color * alpha, alpha);
}
