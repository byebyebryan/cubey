#version 450
#extension GL_GOOGLE_include_directive : require

#include "cubey/color_space.glsl"
#include "water_3d_contract.glsl"

WATER3D_RENDER_PARAMS;

layout(set = 0, binding = WATER3D_BINDING_U_FIELD, std430) readonly buffer UField { float values[]; } u_field;
layout(set = 0, binding = WATER3D_BINDING_V_FIELD, std430) readonly buffer VField { float values[]; } v_field;
layout(set = 0, binding = WATER3D_BINDING_W_FIELD, std430) readonly buffer WField { float values[]; } w_field;
layout(set = 0, binding = WATER3D_BINDING_PRESSURE_A, std430) readonly buffer PressureAField { float values[]; } pressure_a;
layout(set = 0, binding = WATER3D_BINDING_PRESSURE_B, std430) readonly buffer PressureBField { float values[]; } pressure_b;
layout(set = 0, binding = WATER3D_BINDING_SOLID, std430) readonly buffer SolidField { float values[]; } solid;
layout(set = 0, binding = WATER3D_BINDING_CELL_COUNTS, std430) readonly buffer CellCounts { uint values[]; } cell_counts;

layout(location = 0) in vec2 frag_uv;
layout(location = 1) in vec2 frag_local;
layout(location = 2) in float frag_particle;
layout(location = 0) out vec4 out_color;

uint cell_index(uint x, uint y, uint z, uint width, uint height) {
    return ((z * height) + y) * width + x;
}
uint u_index(uint x, uint y, uint z, uint width, uint height) {
    return ((z * height) + y) * (width + 1u) + x;
}
uint v_index(uint x, uint y, uint z, uint width, uint height) {
    return ((z * (height + 1u)) + y) * width + x;
}
uint w_index(uint x, uint y, uint z, uint width, uint height) {
    return ((z * height) + y) * width + x;
}

uvec3 slice_coord(vec2 uv, uint width, uint height, uint depth) {
    vec3 normalized = vec3(clamp(uv.x, 0.0, 0.999999), clamp(1.0 - uv.y, 0.0, 0.999999),
                           clamp(params.grid_slice.w, 0.0, 0.999999));
    return uvec3(clamp(ivec3(floor(normalized * vec3(width, height, depth))), ivec3(0),
                       ivec3(int(width) - 1, int(height) - 1, int(depth) - 1)));
}

vec3 signed_scalar_color(float value, float scale) {
    float positive = clamp(value * scale, 0.0, 1.0);
    float negative = clamp(-value * scale, 0.0, 1.0);
    return vec3(0.050, 0.058, 0.070) + vec3(0.96, 0.24, 0.10) * positive +
           vec3(0.12, 0.48, 0.98) * negative;
}

void main() {
    uint debug_view = uint(params.camera_up_debug.w + 0.5);
    if (frag_particle > 0.5) {
        float radius = length(frag_local);
        if (radius > 1.0) {
            discard;
        }
        float alpha = smoothstep(1.0, 0.15, radius) * 0.30;
        vec3 edge = vec3(0.16, 0.45, 0.78);
        vec3 core = vec3(0.54, 0.86, 1.0);
        vec3 linear_color = cubey_srgb_to_linear(mix(edge, core, smoothstep(0.95, 0.0, radius)));
        out_color = vec4(linear_color * alpha, alpha);
        return;
    }

    uint width = uint(params.grid_slice.x + 0.5);
    uint height = uint(params.grid_slice.y + 0.5);
    uint depth = uint(params.grid_slice.z + 0.5);
    uvec3 coord = slice_coord(frag_uv, width, height, depth);
    uint index = cell_index(coord.x, coord.y, coord.z, width, height);
    float solid_value = solid.values[index];
    vec3 color = vec3(0.012, 0.016, 0.024);

    if (debug_view == 1u) {
        float occupancy = clamp(float(cell_counts.values[index]) / max(1.0, params.color_options.z), 0.0, 1.0);
        color = mix(vec3(0.024, 0.032, 0.044), vec3(0.12, 0.54, 0.86), occupancy);
        if (solid_value > 0.5) {
            color = vec3(0.72, 0.76, 0.82);
        }
    } else if (debug_view == 2u) {
        uint ux = min(coord.x, width);
        uint uy = min(coord.y, height - 1u);
        uint uz = min(coord.z, depth - 1u);
        uint vx = min(coord.x, width - 1u);
        uint vy = min(coord.y, height);
        uint vz = min(coord.z, depth - 1u);
        uint wx = min(coord.x, width - 1u);
        uint wy = min(coord.y, height - 1u);
        uint wz = min(coord.z, depth);
        vec3 velocity = vec3(u_field.values[u_index(ux, uy, uz, width, height)],
                             v_field.values[v_index(vx, vy, vz, width, height)],
                             w_field.values[w_index(wx, wy, wz, width, height)]);
        float speed = clamp(length(velocity) * 0.8, 0.0, 1.0);
        vec3 direction_color = normalize(abs(velocity) + vec3(0.001));
        color = mix(vec3(0.030, 0.038, 0.052), direction_color, speed);
    } else if (debug_view == 3u) {
        bool read_b = params.color_options.x > 0.5;
        float pressure = read_b ? pressure_b.values[index] : pressure_a.values[index];
        color = signed_scalar_color(pressure, 6.0);
    } else if (debug_view == 4u) {
        color = mix(vec3(0.035, 0.042, 0.054), vec3(0.82, 0.86, 0.92),
                    clamp(solid_value, 0.0, 1.0));
    }

    vec2 grid = fract(frag_uv * vec2(width, height));
    float line = max(step(grid.x, 0.018), step(grid.y, 0.018)) * 0.12;
    color += vec3(line);
    out_color = vec4(cubey_srgb_to_linear(color), 1.0);
}
