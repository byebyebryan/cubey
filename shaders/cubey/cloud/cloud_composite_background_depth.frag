#version 450
#extension GL_GOOGLE_include_directive : require

#include "cubey/cloud/cloud_common.glsl"

layout(set = 0, binding = 1) uniform sampler2D cloud_product_texture;
layout(set = 0, binding = 2) uniform sampler2D cloud_metadata_texture;
layout(set = 0, binding = 3) uniform sampler2D background_texture;
layout(set = 0, binding = 4) uniform sampler2D scene_depth_texture;

layout(location = 0) in vec2 frag_position;
layout(location = 0) out vec4 out_color;

#include "cubey/cloud/cloud_resolve_common.glsl"
#include "cubey/cloud/cloud_composite_post.glsl"

float cloud_linear_scene_depth(float raw_depth) {
    float near_plane = max(params.scene_depth_options.x, 0.0001);
    float far_plane = max(params.scene_depth_options.y, near_plane + 1.0);
    return (near_plane * far_plane) / max(far_plane - raw_depth * (far_plane - near_plane),
                                          0.0001);
}

float cloud_scene_depth_visibility(vec2 uv, float cloud_distance, float cloud_alpha) {
    if (params.scene_depth_options.z < 0.5 || cloud_alpha <= 0.0001 || cloud_distance <= 0.0) {
        return 1.0;
    }

    float raw_depth = texture(scene_depth_texture, uv).r;
    if (raw_depth >= 0.999999) {
        return 1.0;
    }

    float scene_distance = cloud_linear_scene_depth(raw_depth);
    float fade_distance = max(params.scene_depth_options.w, scene_distance * 0.0025);
    return 1.0 - smoothstep(scene_distance, scene_distance + fade_distance, cloud_distance);
}

void main() {
    vec2 uv = frag_position * 0.5 + 0.5;
    int debug_view = int(params.ref_options.x + 0.5);
    bool final_view = debug_view == CLOUD_DEBUG_FINAL;
    bool raw_final_view = debug_view == CLOUD_DEBUG_RAW_FINAL;
    vec3 direction = cloud_view_direction(frag_position);
    vec3 background = texture(background_texture, uv).rgb;
    vec4 raw_cloud = texture(cloud_product_texture, uv);
    vec4 raw_metadata = texture(cloud_metadata_texture, uv);
    vec4 resolved_cloud = cloud_resolve_cloud_product(uv, direction);
    float edge_mask = cloud_resolve_edge_mask(uv, raw_cloud, raw_metadata, direction);
    float resolve_strength = cloud_resolve_strength(uv, raw_cloud, raw_metadata, direction);
    vec4 cloud = final_view ? mix(raw_cloud, resolved_cloud, resolve_strength) : raw_cloud;
    float cloud_alpha = 1.0 - clamp(cloud.a, 0.0, 1.0);

    if (final_view || raw_final_view) {
        float scene_visibility = cloud_scene_depth_visibility(uv, raw_metadata.r, cloud_alpha);
        cloud.rgb *= scene_visibility;
        cloud.a = mix(1.0, cloud.a, scene_visibility);
        cloud_alpha *= scene_visibility;
    }

    vec3 color = background * clamp(cloud.a, 0.0, 1.0) + cloud.rgb;

    if (debug_view == CLOUD_DEBUG_SCENE_DEPTH_OCCLUSION) {
        float visibility = cloud_scene_depth_visibility(uv, raw_metadata.r,
                                                        cloud_metadata_alpha(raw_cloud,
                                                                             raw_metadata));
        color = vec3(1.0 - visibility);
    } else if (debug_view == CLOUD_DEBUG_METADATA_DISTANCE ||
        debug_view == CLOUD_DEBUG_METADATA_ALPHA ||
        debug_view == CLOUD_DEBUG_METADATA_CONFIDENCE ||
        debug_view == CLOUD_DEBUG_METADATA_DENSITY) {
        color = cloud_metadata_debug_color(uv, debug_view);
    } else if (debug_view == CLOUD_DEBUG_BACKGROUND) {
        color = background;
    } else if (debug_view == CLOUD_DEBUG_EDGE_MASK) {
        color = vec3(edge_mask);
    } else if (raw_final_view) {
        color = max(color, vec3(0.0));
    } else if (!final_view) {
        color = cloud.rgb;
    } else {
        vec3 posted = cloud_composite_external_final_post(color, direction, cloud_alpha,
                                                          edge_mask);
        color = mix(color, posted, clamp(cloud_alpha * 1.15, 0.0, 1.0));
    }
    out_color = vec4(max(color, vec3(0.0)), 1.0);
}
