#version 450
#extension GL_GOOGLE_include_directive : require

#include "cubey/color_space.glsl"

layout(push_constant) uniform RenderParams {
    vec4 grid_debug;
} params;

struct Cell {
    vec4 dye;
    vec4 velocity;
};

layout(set = 0, binding = 0, std430) readonly buffer RenderField {
    Cell cells[];
} render_field;

layout(set = 0, binding = 1, std430) readonly buffer DivergenceField {
    float values[];
} divergence_field;

layout(set = 0, binding = 2, std430) readonly buffer PressureAField {
    float values[];
} pressure_a_field;

layout(set = 0, binding = 3, std430) readonly buffer PressureBField {
    float values[];
} pressure_b_field;

layout(location = 0) in vec2 frag_position;
layout(location = 0) out vec4 out_color;

uint cell_index(ivec2 coord, uint width, uint height) {
    ivec2 clamped = clamp(coord, ivec2(0), ivec2(int(width) - 1, int(height) - 1));
    return (uint(clamped.y) * width) + uint(clamped.x);
}

Cell sample_field(vec2 uv, uint width, uint height) {
    vec2 position = uv * vec2(width, height) - vec2(0.5);
    ivec2 base = ivec2(floor(position));
    vec2 fraction = fract(position);

    Cell a = render_field.cells[cell_index(base, width, height)];
    Cell b = render_field.cells[cell_index(base + ivec2(1, 0), width, height)];
    Cell c = render_field.cells[cell_index(base + ivec2(0, 1), width, height)];
    Cell d = render_field.cells[cell_index(base + ivec2(1, 1), width, height)];

    Cell result;
    result.dye = mix(mix(a.dye, b.dye, fraction.x), mix(c.dye, d.dye, fraction.x), fraction.y);
    result.velocity = mix(mix(a.velocity, b.velocity, fraction.x),
                          mix(c.velocity, d.velocity, fraction.x), fraction.y);
    return result;
}

float sample_divergence(vec2 uv, uint width, uint height) {
    vec2 position = uv * vec2(width, height) - vec2(0.5);
    ivec2 base = ivec2(floor(position));
    vec2 fraction = fract(position);

    uint index_a = cell_index(base, width, height);
    uint index_b = cell_index(base + ivec2(1, 0), width, height);
    uint index_c = cell_index(base + ivec2(0, 1), width, height);
    uint index_d = cell_index(base + ivec2(1, 1), width, height);

    float a = divergence_field.values[index_a];
    float b = divergence_field.values[index_b];
    float c = divergence_field.values[index_c];
    float d = divergence_field.values[index_d];
    return mix(mix(a, b, fraction.x), mix(c, d, fraction.x), fraction.y);
}

float sample_pressure(vec2 uv, uint width, uint height) {
    vec2 position = uv * vec2(width, height) - vec2(0.5);
    ivec2 base = ivec2(floor(position));
    vec2 fraction = fract(position);
    bool use_pressure_b = params.grid_debug.w > 0.5;

    uint index_a = cell_index(base, width, height);
    uint index_b = cell_index(base + ivec2(1, 0), width, height);
    uint index_c = cell_index(base + ivec2(0, 1), width, height);
    uint index_d = cell_index(base + ivec2(1, 1), width, height);

    float a = use_pressure_b ? pressure_b_field.values[index_a] : pressure_a_field.values[index_a];
    float b = use_pressure_b ? pressure_b_field.values[index_b] : pressure_a_field.values[index_b];
    float c = use_pressure_b ? pressure_b_field.values[index_c] : pressure_a_field.values[index_c];
    float d = use_pressure_b ? pressure_b_field.values[index_d] : pressure_a_field.values[index_d];
    return mix(mix(a, b, fraction.x), mix(c, d, fraction.x), fraction.y);
}

vec3 signed_scalar_color(float value, float scale) {
    float positive = clamp(value * scale, 0.0, 1.0);
    float negative = clamp(-value * scale, 0.0, 1.0);
    return vec3(0.08, 0.09, 0.11) + vec3(0.88, 0.22, 0.08) * positive +
           vec3(0.10, 0.42, 0.92) * negative;
}

void main() {
    vec2 uv = frag_position * 0.5 + 0.5;
    uint width = uint(params.grid_debug.x);
    uint height = uint(params.grid_debug.y);
    int debug_mode = int(params.grid_debug.z + 0.5);
    Cell cell = sample_field(uv, width, height);
    float speed = clamp(length(cell.velocity.xy) * 0.45, 0.0, 1.0);
    if (debug_mode == 1) {
        vec2 direction = speed > 0.001 ? normalize(cell.velocity.xy) : vec2(0.0);
        out_color = vec4(cubey_srgb_to_linear(vec3(direction * 0.35 + 0.5, speed)), 1.0);
        return;
    }
    if (debug_mode == 2) {
        out_color =
            vec4(cubey_srgb_to_linear(signed_scalar_color(sample_divergence(uv, width, height),
                                                          24.0)),
                 1.0);
        return;
    }
    if (debug_mode == 3) {
        out_color = vec4(cubey_srgb_to_linear(signed_scalar_color(sample_pressure(uv, width,
                                                                                  height),
                                                                  5.0)),
                         1.0);
        return;
    }
    if (debug_mode == 4) {
        out_color = vec4(cubey_srgb_to_linear(vec3(speed)), 1.0);
        return;
    }

    vec3 dye = clamp(cell.dye.rgb, vec3(0.0), vec3(1.0));
    vec3 velocity_tint =
        cubey_srgb_to_linear(vec3(0.04, 0.10, 0.16) + vec3(0.05, 0.12, 0.20) * speed);
    out_color = vec4(dye + velocity_tint, 1.0);
}
