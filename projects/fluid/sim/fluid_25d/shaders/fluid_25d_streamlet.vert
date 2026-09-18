#version 450

layout(set = 0, binding = 0, std430) readonly buffer TerrainField {
    float values[];
} terrain;
layout(set = 0, binding = 1, std430) readonly buffer DepthField {
    float values[];
} depth;
layout(set = 0, binding = 2, std430) readonly buffer VelocityField {
    vec4 values[];
} velocity;
layout(set = 0, binding = 3, std430) readonly buffer StreamletField {
    vec4 values[];
} streamlets;

layout(push_constant) uniform CatchmentParams {
    mat4 view_projection;
    vec4 grid_cell;
    vec4 camera_wet;
    vec4 presentation;
} params;

layout(location = 0) out vec2 frag_local;
layout(location = 1) out float frag_visibility;

const float kMinimumStreamletSpeedMPerS = 0.02;
// A streamlet spans independently projected, locally draped ribbon segments.
// Keep a conservative D32 separation from the separately rasterized water
// triangles so its sparse directional silhouette does not dissolve into
// z-fighting along a rough imported bed.
const float kWaterClipDepthBias = 256.0 * 1.19209290e-7;
const uint kStreamletSegmentCount = 8u;

vec2 quad_corner(uint index) {
    const vec2 corners[6] = vec2[](vec2(-1.0, -1.0), vec2(1.0, -1.0), vec2(1.0, 1.0),
                                   vec2(-1.0, -1.0), vec2(1.0, 1.0), vec2(-1.0, 1.0));
    return corners[index % 6u];
}

bool finite_vec2(vec2 value) {
    return !any(isnan(value)) && !any(isinf(value));
}

uvec2 sample_coordinate(vec2 cell, uint width, uint height) {
    vec2 bounded = clamp(cell, vec2(0.0), vec2(float(width - 1u), float(height - 1u)));
    return uvec2(floor(bounded + vec2(0.5)));
}

float sample_surface_height(vec2 cell, uint width, uint height) {
    vec2 bounded = clamp(cell, vec2(0.0), vec2(float(width - 1u), float(height - 1u)));
    uvec2 lower = uvec2(floor(bounded));
    uvec2 upper = min(lower + uvec2(1u), uvec2(width - 1u, height - 1u));
    vec2 fraction = fract(bounded);
    float h00 = terrain.values[lower.y * width + lower.x] + depth.values[lower.y * width + lower.x];
    float h10 = terrain.values[lower.y * width + upper.x] + depth.values[lower.y * width + upper.x];
    float h01 = terrain.values[upper.y * width + lower.x] + depth.values[upper.y * width + lower.x];
    float h11 = terrain.values[upper.y * width + upper.x] + depth.values[upper.y * width + upper.x];
    // Match the two procedural water triangles for this grid quad exactly.
    return fraction.x >= fraction.y
               ? h00 * (1.0 - fraction.x) + h11 * fraction.y + h10 * (fraction.x - fraction.y)
               : h00 * (1.0 - fraction.y) + h01 * (fraction.y - fraction.x) + h11 * fraction.x;
}

void main() {
    uint width = uint(params.grid_cell.x);
    uint height = uint(params.grid_cell.y);
    vec4 state = streamlets.values[uint(gl_InstanceIndex)];
    if (state.z < 0.0 || !finite_vec2(state.xy)) {
        frag_local = vec2(0.0);
        frag_visibility = 0.0;
        gl_Position = vec4(2.0, 2.0, 0.0, 1.0);
        return;
    }

    uvec2 coordinate = sample_coordinate(state.xy, width, height);
    uint index = coordinate.y * width + coordinate.x;
    float water_depth = depth.values[index];
    vec2 flow = velocity.values[index].xy;
    float speed = length(flow);
    if (!finite_vec2(flow) || isnan(water_depth) || isinf(water_depth) ||
        water_depth <= params.camera_wet.w || speed < kMinimumStreamletSpeedMPerS) {
        frag_local = vec2(0.0);
        frag_visibility = 0.0;
        gl_Position = vec4(2.0, 2.0, 0.0, 1.0);
        return;
    }

    vec2 direction = flow / speed;
    vec2 side = vec2(-direction.y, direction.x);
    uint segment = uint(gl_VertexIndex) / 6u;
    vec2 corner = quad_corner(uint(gl_VertexIndex));
    float segment_start = -0.5 * float(kStreamletSegmentCount) + float(segment);
    float along = mix(segment_start, segment_start + 1.0, corner.x * 0.5 + 0.5);
    float headness = clamp((along + 0.5 * float(kStreamletSegmentCount)) /
                               float(kStreamletSegmentCount),
                           0.0, 1.0);
    // The narrow downstream end gives each sparse mark an unambiguous head;
    // fragment alpha/color reinforce that direction without a busy texture.
    // Collapse the pale downstream end to a point. This is a streamlet, not
    // a particle sprite: its asymmetric silhouette makes flow direction legible.
    float half_width = mix(0.50, 0.0, headness) * params.grid_cell.z;
    // Eight one-cell ribbon segments trace the same eight-cell mark. Sampling
    // every segment vertex against the procedural surface keeps the mark on
    // steep imported terrain instead of depth-hiding a long flat quad.
    vec2 cell_offset = direction * (along * params.grid_cell.z) +
                       side * (corner.y * half_width);
    vec2 streamlet_cell = state.xy + cell_offset / params.grid_cell.z;
    vec2 centered = (streamlet_cell - 0.5 * vec2(float(width - 1u), float(height - 1u))) *
                    params.grid_cell.z;
    float surface = sample_surface_height(streamlet_cell, width, height) * params.grid_cell.w;
    vec3 world_position = vec3(centered.x, surface + 0.005, centered.y);
    frag_local = vec2(headness * 2.0 - 1.0, corner.y);
    frag_visibility = smoothstep(kMinimumStreamletSpeedMPerS, 0.065, speed);
    gl_Position = params.view_projection * vec4(world_position, 1.0);
    gl_Position.z -= kWaterClipDepthBias * gl_Position.w;
}
