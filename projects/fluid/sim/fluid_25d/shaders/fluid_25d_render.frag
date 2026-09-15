#version 450
#extension GL_GOOGLE_include_directive : require

#include "cubey/color_space.glsl"

layout(push_constant) uniform RenderParams {
    vec4 grid_debug;
} params;

layout(set = 0, binding = 0, std430) readonly buffer TerrainField {
    float values[];
} terrain;
layout(set = 0, binding = 1, std430) readonly buffer DepthField {
    float values[];
} depth;
layout(set = 0, binding = 2, std430) readonly buffer VelocityField {
    vec4 values[];
} velocity;

layout(location = 0) in vec2 frag_position;
layout(location = 0) out vec4 out_color;

uint cell_index(vec2 uv, uint width, uint height) {
    uvec2 coord = min(uvec2(clamp(uv, vec2(0.0), vec2(0.999999)) * vec2(width, height)),
                      uvec2(width - 1u, height - 1u));
    return coord.y * width + coord.x;
}

vec3 terrain_color(float value) {
    float height = clamp((value + 0.2) * 0.24, 0.0, 1.0);
    return mix(vec3(0.05, 0.12, 0.07), vec3(0.64, 0.47, 0.25), height);
}

void main() {
    uint width = uint(params.grid_debug.x);
    uint height = uint(params.grid_debug.y);
    uint index = cell_index(frag_position * 0.5 + 0.5, width, height);
    float terrain_height = terrain.values[index];
    float water_depth = depth.values[index];
    vec4 flow = velocity.values[index];
    int debug_view = int(params.grid_debug.z + 0.5);

    vec3 color = terrain_color(terrain_height);
    if (debug_view == 1) {
        color = mix(vec3(0.01, 0.035, 0.08), vec3(0.10, 0.82, 1.0),
                    clamp(water_depth * 12.0, 0.0, 1.0));
    } else if (debug_view == 2) {
        color = terrain_color(terrain_height + water_depth);
        color = mix(color, vec3(0.35, 0.86, 1.0), clamp(water_depth * 7.0, 0.0, 0.75));
    } else if (debug_view == 3) {
        color = mix(vec3(0.015, 0.02, 0.03), vec3(1.0, 0.62, 0.08),
                    clamp(length(flow.xy) * 0.14, 0.0, 1.0));
    } else if (debug_view == 4) {
        vec2 direction = length(flow.xy) > 0.00001 ? normalize(flow.xy) : vec2(0.0);
        color = vec3(direction * 0.42 + 0.5, clamp(length(flow.xy) * 0.12, 0.0, 1.0));
    } else if (debug_view == 5) {
        color = flow.z > 0.5 ? vec3(0.05, 0.60, 0.93) : vec3(0.18, 0.12, 0.075);
    } else if (water_depth > params.grid_debug.w) {
        float water = clamp(water_depth * 10.0, 0.0, 1.0);
        color = mix(color, vec3(0.03, 0.30, 0.70), 0.42 + 0.44 * water);
    }
    out_color = vec4(cubey_srgb_to_linear(color), 1.0);
}
