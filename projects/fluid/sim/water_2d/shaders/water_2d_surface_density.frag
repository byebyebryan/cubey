#version 450
#extension GL_GOOGLE_include_directive : require

#include "water_2d_contract.glsl"

WATER2D_RENDER_PARAMS;

layout(location = 0) in vec2 frag_local;
layout(location = 0) out float out_density;

void main() {
    float radius_squared = dot(frag_local, frag_local);
    if (radius_squared > 1.0) {
        discard;
    }

    float kernel = exp(-radius_squared * 2.4);
    float edge_gate = 1.0 - smoothstep(0.82, 1.0, sqrt(radius_squared));
    out_density = kernel * edge_gate * max(0.0, params.surface_options.w);
}
