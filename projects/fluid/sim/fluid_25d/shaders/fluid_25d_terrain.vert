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

layout(push_constant) uniform CatchmentParams {
    mat4 view_projection;
    vec4 grid_cell;
    vec4 camera_wet;
} params;

layout(location = 0) out vec3 world_position;
layout(location = 1) out vec3 world_normal;
layout(location = 2) out vec2 world_xz;

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

float terrain_height(uvec2 coordinate, uint width, uint height) {
    uvec2 clamped = min(coordinate, uvec2(width - 1u, height - 1u));
    return terrain.values[cell_index(clamped, width)] * params.grid_cell.w;
}

vec3 terrain_normal(uvec2 coordinate, uint width, uint height, float cell_size) {
    uvec2 left = uvec2(coordinate.x > 0u ? coordinate.x - 1u : 0u, coordinate.y);
    uvec2 right = uvec2(min(coordinate.x + 1u, width - 1u), coordinate.y);
    uvec2 down = uvec2(coordinate.x, coordinate.y > 0u ? coordinate.y - 1u : 0u);
    uvec2 up = uvec2(coordinate.x, min(coordinate.y + 1u, height - 1u));
    return normalize(vec3(terrain_height(left, width, height) - terrain_height(right, width, height),
                          2.0 * cell_size,
                          terrain_height(down, width, height) - terrain_height(up, width, height)));
}

void main() {
    uint width = uint(params.grid_cell.x);
    uint height = uint(params.grid_cell.y);
    float cell_size = params.grid_cell.z;
    uvec2 coordinate = vertex_coordinate(uint(gl_VertexIndex), width);
    vec2 centered = vec2(float(coordinate.x) - 0.5 * float(width - 1u),
                         float(coordinate.y) - 0.5 * float(height - 1u)) * cell_size;
    float elevation = terrain_height(coordinate, width, height);
    world_position = vec3(centered.x, elevation, centered.y);
    world_normal = terrain_normal(coordinate, width, height, cell_size);
    world_xz = centered;
    gl_Position = params.view_projection * vec4(world_position, 1.0);
}
