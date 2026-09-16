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
layout(set = 0, binding = 3, std430) readonly buffer PresentationCueAField {
    float values[];
} cue_a;
layout(set = 0, binding = 4, std430) readonly buffer PresentationCueBField {
    float values[];
} cue_b;

layout(push_constant) uniform CatchmentParams {
    mat4 view_projection;
    vec4 grid_cell;
    vec4 camera_wet;
    vec4 presentation;
} params;

layout(location = 0) out vec3 world_position;
layout(location = 1) out vec3 world_normal;
layout(location = 2) out float water_depth;
layout(location = 3) out vec3 water_flow;
layout(location = 4) out float presentation_cue;

// The catchment keeps metre-scale X/Z but compresses elevation for a readable
// overview. Even with its scale-derived near plane, a 5 cm physical sheet can
// land within the same forward-Z representable interval as its terrain bed at
// the far side. Move only the raster depth by a few D32 intervals; world-space
// shading and the physical surface height stay unchanged. Together with the
// project-local camera near plane this remains below a decimetre at the
// farthest default catchment samples, so genuinely foreground terrain retains
// its occlusion. Eight float depth intervals are enough once the near plane no
// longer spans metres to fractions of a millimetre.
const float kWaterClipDepthBias = 8.0 * 1.19209290e-7;

uint cell_index(uvec2 coordinate, uint width) {
    return coordinate.y * width + coordinate.x;
}

uvec2 vertex_coordinate(uint vertex_index, uint width) {
    uint cells_x = width - 1u;
    uint quad_index = vertex_index / 6u;
    uint vertex_in_quad = vertex_index % 6u;
    uvec2 base = uvec2(quad_index % cells_x, quad_index / cells_x);
    const uvec2 corners[6] = uvec2[](uvec2(0u, 0u), uvec2(1u, 1u), uvec2(1u, 0u),
                                      uvec2(0u, 0u), uvec2(0u, 1u), uvec2(1u, 1u));
    return base + corners[vertex_in_quad];
}

float surface_height(uvec2 coordinate, uint width, uint height) {
    uvec2 clamped = min(coordinate, uvec2(width - 1u, height - 1u));
    uint index = cell_index(clamped, width);
    return (terrain.values[index] + depth.values[index]) * params.grid_cell.w;
}

vec3 surface_normal(uvec2 coordinate, uint width, uint height, float cell_size) {
    uvec2 left = uvec2(coordinate.x > 0u ? coordinate.x - 1u : 0u, coordinate.y);
    uvec2 right = uvec2(min(coordinate.x + 1u, width - 1u), coordinate.y);
    uvec2 down = uvec2(coordinate.x, coordinate.y > 0u ? coordinate.y - 1u : 0u);
    uvec2 up = uvec2(coordinate.x, min(coordinate.y + 1u, height - 1u));
    return normalize(vec3(surface_height(left, width, height) - surface_height(right, width, height),
                          2.0 * cell_size,
                          surface_height(down, width, height) - surface_height(up, width, height)));
}

void main() {
    uint width = uint(params.grid_cell.x);
    uint height = uint(params.grid_cell.y);
    float cell_size = params.grid_cell.z;
    uvec2 coordinate = vertex_coordinate(uint(gl_VertexIndex), width);
    uint index = cell_index(coordinate, width);
    vec2 centered = vec2(float(coordinate.x) - 0.5 * float(width - 1u),
                         float(coordinate.y) - 0.5 * float(height - 1u)) * cell_size;
    water_depth = depth.values[index];
    world_position = vec3(centered.x, surface_height(coordinate, width, height), centered.y);
    world_normal = surface_normal(coordinate, width, height, cell_size);
    water_flow = velocity.values[index].xyz;
    presentation_cue =
        params.presentation.x > 0.5 ? cue_a.values[index] : cue_b.values[index];
    gl_Position = params.view_projection * vec4(world_position, 1.0);
    gl_Position.z -= kWaterClipDepthBias * gl_Position.w;
}
