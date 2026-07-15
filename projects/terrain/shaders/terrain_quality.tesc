#version 450
#extension GL_GOOGLE_include_directive : require

#include "terrain_source.glsl"

layout(vertices = 4) out;

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

layout(location = 0) in vec3 control_position[];
layout(location = 1) in vec3 control_metadata[];
layout(location = 2) in vec3 control_ownership[];

layout(location = 0) out vec3 patch_position[];
layout(location = 1) out vec3 patch_metadata[];
layout(location = 2) out vec3 patch_ownership[];

layout(location = 3) patch out float patch_tess_factor;
layout(location = 4) patch out float patch_projected_edge_px;

vec2 terrain_quality_origin() {
    float origin_snap_m = control_ownership[0].y;
    return floor(pc.camera_position_vertical_scale.xz / origin_snap_m) * origin_snap_m;
}

vec4 terrain_quality_clip(vec2 local_xz) {
    vec2 world_xz = local_xz + terrain_quality_origin();
    float footprint_m = max(control_metadata[0].x, 0.25);
    float height_m = terrain_source_height(
        terrain_uniforms.source, world_xz + pc.stage_options.xy, footprint_m);
    vec3 world_position = vec3(world_xz.x,
        height_m * pc.camera_position_vertical_scale.w, world_xz.y);
    return pc.view_projection * vec4(world_position, 1.0);
}

float terrain_quality_projected_edge(vec4 first, vec4 second) {
    if (first.w <= 0.0001 || second.w <= 0.0001) {
        return max(pc.quality_options.y, pc.quality_options.z);
    }
    vec2 first_pixel = (first.xy / first.w * 0.5 + 0.5) * pc.quality_options.yz;
    vec2 second_pixel = (second.xy / second.w * 0.5 + 0.5) * pc.quality_options.yz;
    return length(second_pixel - first_pixel);
}

float terrain_quality_factor(float projected_edge_px) {
    float requested = max(projected_edge_px / max(pc.quality_options.x, 1.0), 1.0);
    return clamp(exp2(ceil(log2(requested))), 1.0, 64.0);
}

bool terrain_quality_outside_frustum(vec2 origin) {
    vec4 bounds[8];
    float minimum_height = (terrain_uniforms.source.elevation.x - 128.0) *
        pc.camera_position_vertical_scale.w;
    float maximum_height = (terrain_uniforms.source.elevation.x +
        terrain_uniforms.source.elevation.y + 128.0) * pc.camera_position_vertical_scale.w;
    for (int corner = 0; corner < 4; ++corner) {
        vec2 world_xz = control_position[corner].xz + origin;
        bounds[corner] = pc.view_projection * vec4(world_xz.x, minimum_height, world_xz.y, 1.0);
        bounds[corner + 4] = pc.view_projection *
            vec4(world_xz.x, maximum_height, world_xz.y, 1.0);
    }
    bool left = true;
    bool right = true;
    bool bottom = true;
    bool top = true;
    bool near_plane = true;
    bool far_plane = true;
    for (int index = 0; index < 8; ++index) {
        left = left && bounds[index].x < -bounds[index].w;
        right = right && bounds[index].x > bounds[index].w;
        bottom = bottom && bounds[index].y < -bounds[index].w;
        top = top && bounds[index].y > bounds[index].w;
        near_plane = near_plane && bounds[index].z < 0.0;
        far_plane = far_plane && bounds[index].z > bounds[index].w;
    }
    return left || right || bottom || top || near_plane || far_plane;
}

void main() {
    patch_position[gl_InvocationID] = control_position[gl_InvocationID];
    patch_metadata[gl_InvocationID] = control_metadata[gl_InvocationID];
    patch_ownership[gl_InvocationID] = control_ownership[gl_InvocationID];
    gl_out[gl_InvocationID].gl_Position = gl_in[gl_InvocationID].gl_Position;

    if (gl_InvocationID != 0) {
        return;
    }

    vec4 clips[4];
    for (int corner = 0; corner < 4; ++corner) {
        clips[corner] = terrain_quality_clip(control_position[corner].xz);
    }
    float projected_edges[4] = float[4](
        terrain_quality_projected_edge(clips[0], clips[3]),
        terrain_quality_projected_edge(clips[0], clips[1]),
        terrain_quality_projected_edge(clips[1], clips[2]),
        terrain_quality_projected_edge(clips[3], clips[2]));
    float factors[4] = float[4](
        terrain_quality_factor(projected_edges[0]),
        terrain_quality_factor(projected_edges[1]),
        terrain_quality_factor(projected_edges[2]),
        terrain_quality_factor(projected_edges[3]));

    if (terrain_quality_outside_frustum(terrain_quality_origin())) {
        factors = float[4](0.0, 0.0, 0.0, 0.0);
    }
    gl_TessLevelOuter[0] = factors[0];
    gl_TessLevelOuter[1] = factors[1];
    gl_TessLevelOuter[2] = factors[2];
    gl_TessLevelOuter[3] = factors[3];
    gl_TessLevelInner[0] = max(factors[1], factors[3]);
    gl_TessLevelInner[1] = max(factors[0], factors[2]);
    patch_tess_factor = max(max(factors[0], factors[1]), max(factors[2], factors[3]));
    patch_projected_edge_px = patch_tess_factor > 0.0
        ? max(max(projected_edges[0] / max(factors[0], 1.0),
                  projected_edges[1] / max(factors[1], 1.0)),
              max(projected_edges[2] / max(factors[2], 1.0),
                  projected_edges[3] / max(factors[3], 1.0)))
        : 0.0;
}
