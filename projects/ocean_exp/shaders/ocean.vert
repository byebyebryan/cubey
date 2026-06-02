#version 450

layout(set = 0, binding = 0) uniform sampler2D displacement_cascade0_texture;
layout(set = 0, binding = 1) uniform sampler2D displacement_cascade1_texture;
layout(set = 0, binding = 2) uniform sampler2D displacement_cascade2_texture;
layout(set = 0, binding = 3) uniform sampler2D displacement_cascade3_texture;
layout(set = 0, binding = 4) uniform sampler2D displacement_cascade4_texture;

layout(push_constant) uniform OceanParams {
    mat4 view_projection;
    vec4 camera_time;
    vec4 mesh_options;
    vec4 patch_bounds;
    vec4 sun_direction;
    vec4 debug_options;
    vec4 inspection_options;
    vec4 tile_lengths;
    vec4 displacement_scales;
    vec4 normal_scales;
    vec4 cascade4_options;
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
const float OCEAN_MACRO_ANTI_REPEAT_WEIGHT = 0.32;

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

vec2 rotate2(vec2 value, float angle) {
    float s = sin(angle);
    float c = cos(angle);
    return vec2(c * value.x - s * value.y, s * value.x + c * value.y);
}

float macro_anti_repeat_angle(uint cascade) {
    return cascade == 0u ? 0.47 : -0.61;
}

vec2 macro_anti_repeat_offset(uint cascade) {
    return cascade == 0u ? vec2(347.0, -911.0) : vec2(-193.0, 467.0);
}

vec3 rotate_displacement_xz(vec3 displacement, float angle) {
    vec2 xz = rotate2(displacement.xz, angle);
    return vec3(xz.x, displacement.y, xz.y);
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
    if (cascade == 2u) {
        return ocean.tile_lengths.z;
    }
    if (cascade == 3u) {
        return ocean.tile_lengths.w;
    }
    return ocean.cascade4_options.x;
}

float cascade_displacement_scale(uint cascade) {
    if (cascade == 0u) {
        return ocean.displacement_scales.x;
    }
    if (cascade == 1u) {
        return ocean.displacement_scales.y;
    }
    if (cascade == 2u) {
        return ocean.displacement_scales.z;
    }
    if (cascade == 3u) {
        return ocean.displacement_scales.w;
    }
    return ocean.cascade4_options.y;
}

vec4 sample_displacement(uint cascade, vec2 uv) {
    if (cascade == 0u) {
        return texture(displacement_cascade0_texture, uv);
    }
    if (cascade == 1u) {
        return texture(displacement_cascade1_texture, uv);
    }
    if (cascade == 2u) {
        return texture(displacement_cascade2_texture, uv);
    }
    if (cascade == 3u) {
        return texture(displacement_cascade3_texture, uv);
    }
    return texture(displacement_cascade4_texture, uv);
}

bool ocean_cascade_enabled(uint cascade) {
    float selected = ocean.inspection_options.x;
    return selected < -0.5 || abs(selected - float(cascade)) < 0.5;
}

bool ocean_macro_anti_repeat_enabled(uint cascade) {
    return cascade < 2u && ocean.inspection_options.y > 0.0;
}

float cascade_displacement_lod_weight(uint cascade, float camera_distance) {
    if (cascade == 0u) {
        return 1.0;
    }
    if (cascade == 1u) {
        return 1.0 - smoothstep(2400.0, 5200.0, camera_distance);
    }
    if (cascade == 2u) {
        return 1.0 - smoothstep(900.0, 2400.0, camera_distance);
    }
    if (cascade == 3u) {
        return 1.0 - smoothstep(520.0, 1600.0, camera_distance);
    }
    return 1.0 - smoothstep(220.0, 800.0, camera_distance);
}

float horizon_displacement_weight(float camera_distance) {
    return min(exp(-(camera_distance - 150.0) * 0.007), 1.0);
}

vec3 sample_ocean_displacement(uint cascade, vec2 position, float tile_length) {
    vec3 primary = sample_displacement(cascade, position / tile_length).xyz;
    if (!ocean_macro_anti_repeat_enabled(cascade)) {
        return primary;
    }

    float angle = macro_anti_repeat_angle(cascade);
    vec2 secondary_position = rotate2(position, angle) + macro_anti_repeat_offset(cascade);
    vec3 secondary = sample_displacement(cascade, secondary_position / tile_length).xyz;
    secondary = rotate_displacement_xz(secondary, -angle);

    float weight = OCEAN_MACRO_ANTI_REPEAT_WEIGHT * clamp(ocean.inspection_options.y, 0.0, 1.0);
    return (primary + secondary * weight) / (1.0 + weight);
}

void add_displacement(inout vec3 displacement, uint cascade, vec2 position,
                      float camera_distance) {
    if (!ocean_cascade_enabled(cascade)) {
        return;
    }
    float tile_length = max(cascade_tile_length(cascade), 0.001);
    float lod_weight = cascade_displacement_lod_weight(cascade, camera_distance);
    displacement += sample_ocean_displacement(cascade, position, tile_length) *
                    cascade_displacement_scale(cascade) * lod_weight;
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

    vec3 displacement = vec3(0.0);
    for (uint cascade = 0u; cascade < 5u; ++cascade) {
        add_displacement(displacement, cascade, base_position, camera_distance);
    }
    displacement *= horizon_displacement_weight(camera_distance);

    vec3 world_position = vec3(base_position.x, 0.0, base_position.y) + displacement;
    frag_world_position = world_position;
    frag_displacement = displacement;
    frag_sample_position = base_position;
    frag_wave = vec4(displacement.y, 0.0, camera_distance, ocean.foam_color.w);
    frag_patch_alpha = clipmap_patch_alpha(patch_position, patch_cell_size);
    frag_barycentric = triangle_barycentric(vertex_in_cell);
    gl_Position = ocean.view_projection * vec4(world_position, 1.0);
}
