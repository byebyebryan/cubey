#version 450
#extension GL_GOOGLE_include_directive : require

#include "terrain_environment.glsl"
#include "terrain_source.glsl"
#include "terrain_shadow.glsl"

layout(quads, equal_spacing, ccw) in;

layout(set = 0, binding = 0, std140) uniform TerrainSourceUniforms {
    TerrainSourceGpuParameters source;
} terrain_uniforms;

layout(push_constant) uniform TerrainPushConstants {
    mat4 view_projection;
    vec4 camera_position_vertical_scale;
    vec4 render_options;
    vec4 quality_options;
    vec4 stage_options;
} pc;

layout(location = 0) in vec3 patch_position[];
layout(location = 1) in vec3 patch_metadata[];
layout(location = 2) in vec3 patch_ownership[];

layout(location = 3) patch in float patch_tess_factor;
layout(location = 4) patch in float patch_projected_edge_px;

layout(location = 0) out vec3 frag_world_position;
layout(location = 1) out float frag_base_height_m;
layout(location = 2) out float frag_height_m;
layout(location = 3) out float frag_weathering_delta_m;
layout(location = 4) flat out float frag_lod;
layout(location = 5) out vec2 frag_base_gradient_xz;
layout(location = 6) flat out float frag_child_half_extent_m;
layout(location = 7) flat out float frag_origin_snap_m;
layout(location = 8) flat out float frag_cell_size_m;
layout(location = 9) out float frag_lod_morph;
layout(location = 10) out float frag_footprint_m;
layout(location = 11) out float frag_direct_visibility;
layout(location = 12) out float frag_landform_concavity_m;
layout(location = 13) flat out float frag_tess_factor;
layout(location = 14) flat out float frag_projected_edge_px;

vec2 terrain_quality_local_xz() {
    vec2 bottom = mix(patch_position[0].xz, patch_position[1].xz, gl_TessCoord.x);
    vec2 top = mix(patch_position[3].xz, patch_position[2].xz, gl_TessCoord.x);
    return mix(bottom, top, gl_TessCoord.y);
}

float terrain_quality_morph(vec2 local_xz, float cell_size_m) {
    float outer = cell_size_m * 64.0;
    float transition = min(cell_size_m * 2.0 * 16.0, outer * (11.0 / 32.0));
    float distance_to_outer = outer - max(abs(local_xz.x), abs(local_xz.y));
    if (distance_to_outer <= cell_size_m) {
        return 1.0;
    }
    return 1.0 - smoothstep(0.0, transition, distance_to_outer);
}

void main() {
    float cell_size_m = patch_metadata[0].x;
    float origin_snap_m = patch_ownership[0].y;
    vec2 level_origin = floor(pc.camera_position_vertical_scale.xz / origin_snap_m) * origin_snap_m;
    vec2 local_xz = terrain_quality_local_xz();
    vec2 world_xz = local_xz + level_origin;
    float morph = terrain_quality_morph(local_xz, cell_size_m);
    float coarse_cell_size_m = cell_size_m * 2.0;
    vec2 coarse_world_xz = floor(world_xz / coarse_cell_size_m + 0.5) * coarse_cell_size_m;
    vec2 sample_xz = mix(world_xz, coarse_world_xz, morph);
    vec2 source_xz = sample_xz + pc.stage_options.xy;
    vec3 footprint_position = vec3(sample_xz.x, terrain_uniforms.source.elevation.x, sample_xz.y);
    float generated_spacing_m = max(
        0.25, length(pc.camera_position_vertical_scale.xyz - footprint_position) *
                  pc.render_options.z * max(pc.quality_options.x, 1.0));
    float footprint_m = max(0.25, mix(generated_spacing_m,
        max(generated_spacing_m, coarse_cell_size_m), morph));

    float base_height_m = terrain_source_base_height(
        terrain_uniforms.source, source_xz, footprint_m);
    float weathering_delta_m = terrain_source_weathering_delta(
        terrain_uniforms.source, source_xz, footprint_m, base_height_m);
    float height_m = base_height_m + weathering_delta_m;
    float normal_step_m = max(1.0, footprint_m);
    float height_x = terrain_source_base_height(
        terrain_uniforms.source, source_xz + vec2(normal_step_m, 0.0), footprint_m);
    float height_z = terrain_source_base_height(
        terrain_uniforms.source, source_xz + vec2(0.0, normal_step_m), footprint_m);
    vec3 world_position = vec3(sample_xz.x,
        height_m * pc.camera_position_vertical_scale.w + pc.quality_options.w, sample_xz.y);

    gl_Position = pc.view_projection * vec4(world_position, 1.0);
    frag_world_position = world_position;
    frag_base_height_m = base_height_m;
    frag_height_m = height_m;
    frag_weathering_delta_m = weathering_delta_m;
    frag_lod = patch_metadata[0].y;
    frag_base_gradient_xz = vec2(height_x - base_height_m, height_z - base_height_m) /
        normal_step_m;
    frag_child_half_extent_m = patch_ownership[0].x;
    frag_origin_snap_m = origin_snap_m;
    frag_cell_size_m = cell_size_m;
    frag_lod_morph = morph;
    frag_footprint_m = footprint_m;
#if CUBEY_TERRAIN_SOURCE_VARIANT == 1
    frag_direct_visibility = terrain_heightfield_shadow_v3(
        terrain_uniforms.source, source_xz, height_m,
        pc.camera_position_vertical_scale.w, footprint_m);
    frag_landform_concavity_m = 0.0;
#else
    frag_direct_visibility = terrain_heightfield_shadow(
        terrain_uniforms.source, source_xz, height_m,
        pc.camera_position_vertical_scale.w, footprint_m);
    const float landform_radius_m = 96.0;
    float landform_footprint_m = max(footprint_m, landform_radius_m);
    float landform_neighbor_height = 0.25 * (
        terrain_source_base_height(terrain_uniforms.source,
            source_xz + vec2(landform_radius_m, 0.0), landform_footprint_m) +
        terrain_source_base_height(terrain_uniforms.source,
            source_xz - vec2(landform_radius_m, 0.0), landform_footprint_m) +
        terrain_source_base_height(terrain_uniforms.source,
            source_xz + vec2(0.0, landform_radius_m), landform_footprint_m) +
        terrain_source_base_height(terrain_uniforms.source,
            source_xz - vec2(0.0, landform_radius_m), landform_footprint_m));
    float landform_center_height = terrain_source_base_height(
        terrain_uniforms.source, source_xz, landform_footprint_m);
    frag_landform_concavity_m = landform_neighbor_height - landform_center_height;
#endif
    frag_tess_factor = patch_tess_factor;
    frag_projected_edge_px = patch_projected_edge_px;
}
