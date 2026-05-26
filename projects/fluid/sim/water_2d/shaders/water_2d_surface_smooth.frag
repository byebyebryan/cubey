#version 450
#extension GL_GOOGLE_include_directive : require

#include "water_2d_contract.glsl"

WATER2D_RENDER_PARAMS;

layout(set = 0, binding = 0) uniform sampler2D source_density;

layout(location = 0) in vec2 frag_position;
layout(location = 0) out float out_density;

const int kMaxSmoothRadius = 24;

void main() {
    vec2 uv = frag_position * 0.5 + 0.5;
    float radius_px = clamp(params.foam_options.z, 0.0, float(kMaxSmoothRadius));
    int radius = int(ceil(radius_px));
    if (radius <= 0) {
        out_density = texture(source_density, uv).r;
        return;
    }

    vec2 texel_size = 1.0 / vec2(textureSize(source_density, 0));
    vec2 axis = params.foam_options.w < 0.5 ? vec2(1.0, 0.0) : vec2(0.0, 1.0);
    float sigma = max(1.0, radius_px * 0.45);
    float weight_sum = 0.0;
    float density_sum = 0.0;
    for (int offset = -kMaxSmoothRadius; offset <= kMaxSmoothRadius; ++offset) {
        if (abs(offset) > radius) {
            continue;
        }
        float x = float(offset);
        float weight = exp(-0.5 * (x * x) / (sigma * sigma));
        density_sum += texture(source_density, uv + axis * texel_size * x).r * weight;
        weight_sum += weight;
    }

    out_density = weight_sum > 0.0 ? density_sum / weight_sum : texture(source_density, uv).r;
}
