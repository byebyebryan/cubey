#version 450

struct QuiverState {
    vec4 anchor_xy_reserved;
    vec4 direction_xy_strength_opacity;
};

layout(set = 0, binding = 0, std430) readonly buffer TerrainField {
    float values[];
} terrain;
layout(set = 0, binding = 1, std430) readonly buffer DepthField {
    float values[];
} depth;
layout(set = 0, binding = 2, std430) readonly buffer VelocityField {
    vec4 values[];
} velocity;
layout(set = 0, binding = 3, std430) readonly buffer QuiverField {
    QuiverState values[];
} quiver;

layout(push_constant) uniform CatchmentParams {
    mat4 view_projection;
    vec4 grid_cell;
    vec4 camera_wet;
    vec4 presentation;
} params;

layout(location = 0) out vec2 frag_local;
layout(location = 1) flat out uint frag_part;
layout(location = 2) out float frag_speed_fraction;
layout(location = 3) out float frag_opacity;

const float kWaterClipDepthBias = 256.0 * 1.19209290e-7;
const float kDirectionEpsilon = 1.0e-5;

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
    return fraction.x >= fraction.y
               ? h00 * (1.0 - fraction.x) + h11 * fraction.y + h10 * (fraction.x - fraction.y)
               : h00 * (1.0 - fraction.y) + h01 * (fraction.y - fraction.x) + h11 * fraction.x;
}

vec2 shaft_vertex(uint vertex_index) {
    const vec2 vertices[6] = vec2[](vec2(-0.56, -0.10), vec2(0.08, -0.10),
                                    vec2(0.08, 0.10), vec2(-0.56, -0.10),
                                    vec2(0.08, 0.10), vec2(-0.56, 0.10));
    return vertices[vertex_index];
}

vec2 head_vertex(uint vertex_index) {
    const vec2 vertices[3] = vec2[](vec2(0.04, -0.27), vec2(0.70, 0.0),
                                    vec2(0.04, 0.27));
    return vertices[vertex_index];
}

void hide_arrow() {
    frag_local = vec2(0.0);
    frag_part = 0u;
    frag_speed_fraction = 0.0;
    frag_opacity = 0.0;
    gl_Position = vec4(2.0, 2.0, 0.0, 1.0);
}

void main() {
    uint width = uint(params.grid_cell.x);
    uint height = uint(params.grid_cell.y);
    QuiverState state = quiver.values[uint(gl_InstanceIndex)];
    vec2 anchor = state.anchor_xy_reserved.xy;
    vec2 stored_direction = state.direction_xy_strength_opacity.xy;
    float stored_strength = state.direction_xy_strength_opacity.z;
    float opacity = state.direction_xy_strength_opacity.w;
    float direction_length = length(stored_direction);
    if (!finite_vec2(anchor) || !finite_vec2(stored_direction) || opacity <= 0.0 ||
        stored_strength <= 0.0 || direction_length < kDirectionEpsilon) {
        hide_arrow();
        return;
    }

    uvec2 coordinate = sample_coordinate(anchor, width, height);
    uint field_index = coordinate.y * width + coordinate.x;
    float water_depth = depth.values[field_index];
    vec4 current_velocity = velocity.values[field_index];
    if (isnan(water_depth) || isinf(water_depth) || water_depth <= params.camera_wet.w ||
        !finite_vec2(current_velocity.xy) || isnan(current_velocity.z) ||
        isinf(current_velocity.z) || current_velocity.z < 0.5) {
        hide_arrow();
        return;
    }

    vec2 direction = stored_direction / direction_length;
    vec2 side = vec2(-direction.y, direction.x);
    float speed_fraction = clamp((stored_strength - 0.025) / 0.20, 0.0, 1.0);
    // The complete silhouette is 1.26 units long, so this 1.20--2.35-cell
    // scale caps it below 75% of the compact demo's ~4-cell lattice pitch.
    // That leaves a clear spatial gap while still carrying a speed difference.
    float length_cells = mix(1.20, 2.35, sqrt(speed_fraction));
    uint vertex_index = uint(gl_VertexIndex);
    bool is_head = vertex_index >= 6u;
    vec2 local = is_head ? head_vertex(vertex_index - 6u) : shaft_vertex(vertex_index);
    vec2 cell = anchor + (direction * local.x + side * local.y) * length_cells;
    vec2 centered = (cell - 0.5 * vec2(float(width - 1u), float(height - 1u))) * params.grid_cell.z;
    float surface = sample_surface_height(cell, width, height) * params.grid_cell.w;
    frag_local = local;
    frag_part = is_head ? 1u : 0u;
    frag_speed_fraction = speed_fraction;
    frag_opacity = clamp(opacity, 0.0, 1.0);
    gl_Position = params.view_projection * vec4(centered.x, surface + 0.005, centered.y, 1.0);
    gl_Position.z -= kWaterClipDepthBias * gl_Position.w;
}
