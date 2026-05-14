#version 450
#extension GL_GOOGLE_include_directive : require

#include "cubey/color_space.glsl"

layout(push_constant) uniform FractalParams {
    vec2 center;
    float scale;
    float aspect;
    int max_iterations;
} params;

layout(location = 0) in vec2 frag_position;
layout(location = 0) out vec4 out_color;

vec3 palette(float t) {
    return 0.5 + 0.5 * cos(6.28318 * (vec3(0.00, 0.15, 0.35) + t));
}

void main() {
    vec2 c = params.center + vec2(frag_position.x * params.aspect, frag_position.y) * params.scale;
    vec2 z = vec2(0.0);

    int iteration = 0;
    for (int i = 0; i < params.max_iterations; ++i) {
        z = vec2(z.x * z.x - z.y * z.y, 2.0 * z.x * z.y) + c;
        if (dot(z, z) > 4.0) {
            iteration = i;
            break;
        }
        iteration = i + 1;
    }

    if (iteration >= params.max_iterations) {
        out_color = vec4(cubey_srgb_to_linear(vec3(0.015, 0.018, 0.026)), 1.0);
        return;
    }

    float t = float(iteration) / float(params.max_iterations);
    vec3 color = palette(t) * (0.35 + 0.65 * smoothstep(0.0, 1.0, t));
    out_color = vec4(cubey_srgb_to_linear(color), 1.0);
}
