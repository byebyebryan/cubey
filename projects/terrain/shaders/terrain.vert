#version 450
#extension GL_GOOGLE_include_directive : require

#include "terrain_source.glsl"

layout(set = 0, binding = 0, std140) uniform TerrainSourceUniforms {
    TerrainSourceGpuParameters source;
} terrain_uniforms;

layout(push_constant) uniform TerrainPushConstants {
    mat4 view_projection;
    vec4 camera_position_vertical_scale;
    vec4 render_options;
} pc;

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_color;
layout(location = 2) in vec3 in_normal;

layout(location = 0) out vec3 frag_world_position;
layout(location = 1) out float frag_base_height_m;
layout(location = 2) out float frag_height_m;
layout(location = 3) out float frag_weathering_delta_m;
layout(location = 4) flat out float frag_lod;
layout(location = 5) out vec3 frag_source_normal;
layout(location = 6) flat out float frag_child_half_extent_m;
layout(location = 7) flat out float frag_origin_snap_m;
layout(location = 8) flat out float frag_cell_size_m;
layout(location = 9) out float frag_lod_morph;
layout(location = 10) out float frag_footprint_m;

void main() {
    float cell_size_m = in_color.x;
    float morph = in_color.y;
    vec2 camera_xz = pc.camera_position_vertical_scale.xz;
    vec2 level_origin = floor(camera_xz / in_normal.z) * in_normal.z;
    vec2 world_xz = in_position.xz + level_origin;

    float coarse_cell_size_m = cell_size_m * 2.0;
    vec2 coarse_world_xz = floor(world_xz / coarse_cell_size_m + 0.5) * coarse_cell_size_m;
    vec2 sample_xz = mix(world_xz, coarse_world_xz, morph);
    float footprint_m = mix(cell_size_m, coarse_cell_size_m, morph);

    float base_height_m = terrain_source_base_height(
        terrain_uniforms.source, sample_xz, footprint_m);
    float weathering_delta_m = terrain_source_weathering_delta(
        terrain_uniforms.source, sample_xz, footprint_m, base_height_m);
    float height_m = base_height_m + weathering_delta_m;
    float normal_step_m = max(4.0, footprint_m);
    float height_x = terrain_source_base_height(
        terrain_uniforms.source, sample_xz + vec2(normal_step_m, 0.0), footprint_m);
    float height_z = terrain_source_base_height(
        terrain_uniforms.source, sample_xz + vec2(0.0, normal_step_m), footprint_m);
    vec2 gradient = vec2(height_x - base_height_m, height_z - base_height_m) / normal_step_m;
    vec3 world_position = vec3(world_xz.x,
        (height_m - in_normal.y) * pc.camera_position_vertical_scale.w, world_xz.y);

    gl_Position = pc.view_projection * vec4(world_position, 1.0);
    frag_world_position = world_position;
    frag_base_height_m = base_height_m;
    frag_height_m = height_m;
    frag_weathering_delta_m = weathering_delta_m;
    frag_lod = in_color.z;
    frag_source_normal = normalize(vec3(-gradient.x * pc.camera_position_vertical_scale.w,
        1.0, -gradient.y * pc.camera_position_vertical_scale.w));
    frag_child_half_extent_m = in_normal.x;
    frag_origin_snap_m = in_normal.z;
    frag_cell_size_m = cell_size_m;
    frag_lod_morph = morph;
    frag_footprint_m = footprint_m;
}
