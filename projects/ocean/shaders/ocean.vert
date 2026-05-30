#version 450

layout(set = 0, binding = 0) uniform sampler2D displacement_cascade0_texture;
layout(set = 0, binding = 1) uniform sampler2D displacement_cascade1_texture;
layout(set = 0, binding = 2) uniform sampler2D displacement_cascade2_texture;

layout(push_constant) uniform OceanParams {
    mat4 view_projection;
    vec4 camera_time;
    vec4 mesh_options;
    vec4 patch_bounds;
    vec4 display_transform;
    vec4 debug_options;
    vec4 inspection_options;
    vec4 tile_lengths;
    vec4 displacement_scales;
    vec4 normal_scales;
    vec4 water_color;
    vec4 foam_color;
} ocean;

layout(location = 0) out vec3 frag_world_position;
layout(location = 1) out vec3 frag_displacement;
layout(location = 2) out vec2 frag_sample_position;
layout(location = 3) out vec4 frag_wave;
layout(location = 4) out float frag_patch_alpha;
layout(location = 5) noperspective out vec3 frag_barycentric;

const float OCEAN_MESH_TRANSITION_CELLS = 16.0;
const float OCEAN_MESH_MAX_TRANSITION_RATIO = 0.35;

vec2 triangle_corner(uint vertex_in_cell) {
    if (vertex_in_cell == 0u) {
        return vec2(0.0, 0.0);
    }
    if (vertex_in_cell == 1u) {
        return vec2(1.0, 0.0);
    }
    if (vertex_in_cell == 2u) {
        return vec2(0.0, 1.0);
    }
    if (vertex_in_cell == 3u) {
        return vec2(0.0, 1.0);
    }
    if (vertex_in_cell == 4u) {
        return vec2(1.0, 0.0);
    }
    return vec2(1.0, 1.0);
}

vec3 triangle_barycentric(uint vertex_in_cell) {
    if (vertex_in_cell == 0u || vertex_in_cell == 3u) {
        return vec3(1.0, 0.0, 0.0);
    }
    if (vertex_in_cell == 1u || vertex_in_cell == 4u) {
        return vec3(0.0, 1.0, 0.0);
    }
    return vec3(0.0, 0.0, 1.0);
}

vec2 clipmap_patch_position(vec2 uv) {
    return vec2(mix(ocean.patch_bounds.x, ocean.patch_bounds.y, uv.x),
                mix(ocean.patch_bounds.z, ocean.patch_bounds.w, uv.y));
}

float clipmap_transition_width(float coarse_cell_size, float boundary_extent) {
    const float preferred = coarse_cell_size * OCEAN_MESH_TRANSITION_CELLS;
    const float maximum = boundary_extent * OCEAN_MESH_MAX_TRANSITION_RATIO;
    return max(0.001, min(preferred, maximum));
}

float clipmap_patch_alpha(vec2 patch_position, float patch_cell_size) {
    if (ocean.debug_options.y >= ocean.debug_options.z - 0.5) {
        return 1.0;
    }
    float outer_extent = max(max(abs(ocean.patch_bounds.x), abs(ocean.patch_bounds.y)),
                             max(abs(ocean.patch_bounds.z), abs(ocean.patch_bounds.w)));
    float transition_width = clipmap_transition_width(patch_cell_size * 2.0, outer_extent);
    float radius = max(abs(patch_position.x), abs(patch_position.y));
    return 1.0 - smoothstep(outer_extent - transition_width, outer_extent, radius);
}

float cascade_tile_length(uint cascade) {
    if (cascade == 0u) {
        return ocean.tile_lengths.x;
    }
    if (cascade == 1u) {
        return ocean.tile_lengths.y;
    }
    return ocean.tile_lengths.z;
}

float cascade_displacement_scale(uint cascade) {
    if (cascade == 0u) {
        return ocean.displacement_scales.x;
    }
    if (cascade == 1u) {
        return ocean.displacement_scales.y;
    }
    return ocean.displacement_scales.z;
}

vec4 sample_displacement(uint cascade, vec2 uv) {
    if (cascade == 0u) {
        return texture(displacement_cascade0_texture, uv);
    }
    if (cascade == 1u) {
        return texture(displacement_cascade1_texture, uv);
    }
    return texture(displacement_cascade2_texture, uv);
}

bool ocean_cascade_enabled(uint cascade) {
    float selected = ocean.inspection_options.x;
    return selected < -0.5 || abs(selected - float(cascade)) < 0.5;
}

void add_displacement(inout vec3 displacement, uint cascade, vec2 position) {
    if (!ocean_cascade_enabled(cascade)) {
        return;
    }
    float tile_length = max(cascade_tile_length(cascade), 0.001);
    vec2 uv = position / tile_length;
    displacement += sample_displacement(cascade, uv).xyz * cascade_displacement_scale(cascade);
}

void main() {
    uint cells_x = max(uint(ocean.mesh_options.x + 0.5), 1u);
    uint cells_z = max(uint(ocean.mesh_options.y + 0.5), 1u);
    uint vertex_in_cell = uint(gl_VertexIndex) % 6u;
    uint cell_index = uint(gl_VertexIndex) / 6u;
    uint cell_x = cell_index % cells_x;
    uint cell_z = cell_index / cells_x;

    vec2 uv = (vec2(cell_x, cell_z) + triangle_corner(vertex_in_cell)) /
              vec2(float(cells_x), float(cells_z));
    vec2 camera_xz = ocean.camera_time.xz;
    vec2 patch_size =
        vec2(ocean.patch_bounds.y - ocean.patch_bounds.x,
             ocean.patch_bounds.w - ocean.patch_bounds.z);
    float patch_cell_size =
        max(patch_size.x / max(float(cells_x), 1.0), patch_size.y / max(float(cells_z), 1.0));
    float snap = max(patch_cell_size / exp2(ocean.debug_options.y), 0.001);
    vec2 snapped_center = floor(camera_xz / snap) * snap;
    vec2 patch_position = clipmap_patch_position(uv);
    vec2 base_position = snapped_center + patch_position;
    float camera_distance = length(base_position - camera_xz);
    float distance_factor = min(exp(-(camera_distance - 150.0) * 0.007), 1.0);

    vec3 displacement = vec3(0.0);
    add_displacement(displacement, 0u, base_position);
    add_displacement(displacement, 1u, base_position);
    add_displacement(displacement, 2u, base_position);
    displacement *= distance_factor;

    vec3 world_position = vec3(base_position.x, 0.0, base_position.y) + displacement;
    frag_world_position = world_position;
    frag_displacement = displacement;
    frag_sample_position = base_position;
    frag_wave = vec4(displacement.y, 0.0, camera_distance, ocean.normal_scales.w);
    frag_patch_alpha = clipmap_patch_alpha(patch_position, patch_cell_size);
    frag_barycentric = triangle_barycentric(vertex_in_cell);
    gl_Position = ocean.view_projection * vec4(world_position, 1.0);
}
