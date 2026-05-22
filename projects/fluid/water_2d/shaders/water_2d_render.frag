#version 450
#extension GL_GOOGLE_include_directive : require

#include "cubey/color_space.glsl"

layout(push_constant) uniform RenderParams {
    vec4 grid_debug;
} params;

layout(set = 0, binding = 0, std430) readonly buffer PhiAField {
    float values[];
} phi_a;
layout(set = 0, binding = 2, std430) readonly buffer UField {
    float values[];
} u_a;
layout(set = 0, binding = 4, std430) readonly buffer VField {
    float values[];
} v_a;
layout(set = 0, binding = 6, std430) readonly buffer PressureAField {
    float values[];
} pressure_a;
layout(set = 0, binding = 7, std430) readonly buffer PressureBField {
    float values[];
} pressure_b;
layout(set = 0, binding = 8, std430) readonly buffer DivergenceField {
    float values[];
} divergence;
layout(set = 0, binding = 9, std430) readonly buffer SolidField {
    float values[];
} solid;

layout(location = 0) in vec2 frag_position;
layout(location = 0) out vec4 out_color;

uint cell_index(ivec2 coord, uint width, uint height) {
    ivec2 clamped = clamp(coord, ivec2(0), ivec2(int(width) - 1, int(height) - 1));
    return (uint(clamped.y) * width) + uint(clamped.x);
}

uint u_index(ivec2 coord, uint width, uint height) {
    ivec2 clamped = ivec2(clamp(coord.x, 0, int(width)), clamp(coord.y, 0, int(height) - 1));
    return (uint(clamped.y) * (width + 1u)) + uint(clamped.x);
}

uint v_index(ivec2 coord, uint width, uint height) {
    ivec2 clamped = ivec2(clamp(coord.x, 0, int(width) - 1), clamp(coord.y, 0, int(height)));
    return (uint(clamped.y) * width) + uint(clamped.x);
}

float sample_cell_field(vec2 uv, uint width, uint height, int field_id) {
    vec2 position = uv * vec2(width, height) - vec2(0.5);
    ivec2 base = ivec2(floor(position));
    vec2 f = fract(position);
    uint ia = cell_index(base, width, height);
    uint ib = cell_index(base + ivec2(1, 0), width, height);
    uint ic = cell_index(base + ivec2(0, 1), width, height);
    uint id = cell_index(base + ivec2(1, 1), width, height);

    float a = 0.0;
    float b = 0.0;
    float c = 0.0;
    float d = 0.0;
    if (field_id == 0) {
        a = phi_a.values[ia];
        b = phi_a.values[ib];
        c = phi_a.values[ic];
        d = phi_a.values[id];
    } else if (field_id == 1) {
        bool use_pressure_b = params.grid_debug.w > 0.5;
        a = use_pressure_b ? pressure_b.values[ia] : pressure_a.values[ia];
        b = use_pressure_b ? pressure_b.values[ib] : pressure_a.values[ib];
        c = use_pressure_b ? pressure_b.values[ic] : pressure_a.values[ic];
        d = use_pressure_b ? pressure_b.values[id] : pressure_a.values[id];
    } else if (field_id == 2) {
        a = divergence.values[ia];
        b = divergence.values[ib];
        c = divergence.values[ic];
        d = divergence.values[id];
    } else {
        a = solid.values[ia];
        b = solid.values[ib];
        c = solid.values[ic];
        d = solid.values[id];
    }
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

float sample_u(vec2 uv, uint width, uint height) {
    vec2 position = vec2(uv.x * float(width), uv.y * float(height) - 0.5);
    ivec2 base = ivec2(floor(position));
    vec2 f = fract(position);
    float a = u_a.values[u_index(base, width, height)];
    float b = u_a.values[u_index(base + ivec2(1, 0), width, height)];
    float c = u_a.values[u_index(base + ivec2(0, 1), width, height)];
    float d = u_a.values[u_index(base + ivec2(1, 1), width, height)];
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

float sample_v(vec2 uv, uint width, uint height) {
    vec2 position = vec2(uv.x * float(width) - 0.5, uv.y * float(height));
    ivec2 base = ivec2(floor(position));
    vec2 f = fract(position);
    float a = v_a.values[v_index(base, width, height)];
    float b = v_a.values[v_index(base + ivec2(1, 0), width, height)];
    float c = v_a.values[v_index(base + ivec2(0, 1), width, height)];
    float d = v_a.values[v_index(base + ivec2(1, 1), width, height)];
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

vec3 signed_scalar_color(float value, float scale) {
    float positive = clamp(value * scale, 0.0, 1.0);
    float negative = clamp(-value * scale, 0.0, 1.0);
    return vec3(0.075, 0.085, 0.105) + vec3(0.92, 0.22, 0.10) * positive +
           vec3(0.10, 0.42, 0.92) * negative;
}

void main() {
    vec2 uv = frag_position * 0.5 + 0.5;
    uint width = uint(params.grid_debug.x);
    uint height = uint(params.grid_debug.y);
    int debug_mode = int(params.grid_debug.z + 0.5);
    float phi = sample_cell_field(uv, width, height, 0);
    float solid_value = sample_cell_field(uv, width, height, 3);
    vec2 velocity = vec2(sample_u(uv, width, height), sample_v(uv, width, height));
    float speed = clamp(length(velocity) * 0.8, 0.0, 1.0);

    if (debug_mode == 1) {
        out_color = vec4(cubey_srgb_to_linear(signed_scalar_color(phi, 14.0)), 1.0);
        return;
    }
    if (debug_mode == 2) {
        vec2 direction = speed > 0.001 ? normalize(velocity) : vec2(0.0);
        out_color = vec4(cubey_srgb_to_linear(vec3(direction * 0.34 + 0.5, speed)), 1.0);
        return;
    }
    if (debug_mode == 3) {
        float value = sample_cell_field(uv, width, height, 2);
        out_color = vec4(cubey_srgb_to_linear(signed_scalar_color(value, 18.0)), 1.0);
        return;
    }
    if (debug_mode == 4) {
        float value = sample_cell_field(uv, width, height, 1);
        out_color = vec4(cubey_srgb_to_linear(signed_scalar_color(value, 3.5)), 1.0);
        return;
    }
    if (debug_mode == 5) {
        out_color = vec4(cubey_srgb_to_linear(mix(vec3(0.04, 0.045, 0.055),
                                                  vec3(0.82, 0.86, 0.90),
                                                  clamp(solid_value, 0.0, 1.0))),
                         1.0);
        return;
    }

    vec3 background = vec3(0.010, 0.016, 0.024) + vec3(0.018, 0.026, 0.032) * uv.y;
    if (solid_value > 0.45) {
        out_color = vec4(cubey_srgb_to_linear(vec3(0.030, 0.034, 0.040)), 1.0);
        return;
    }

    float h = 1.0 / float(min(width, height));
    float water = 1.0 - smoothstep(0.0, h * 1.8, phi);
    float surface = 1.0 - smoothstep(h * 0.2, h * 2.0, abs(phi));
    vec3 water_color = vec3(0.045, 0.30, 0.55) + vec3(0.10, 0.24, 0.30) * speed;
    vec3 surface_color = vec3(0.62, 0.90, 1.0);
    vec3 color = mix(background, water_color, water);
    color += surface_color * surface * 0.48;
    color += vec3(0.02, 0.05, 0.08) * smoothstep(0.0, 0.9, speed);
    out_color = vec4(cubey_srgb_to_linear(clamp(color, vec3(0.0), vec3(1.0))), 1.0);
}
