#version 450
#extension GL_GOOGLE_include_directive : require

#include "cloud_ref_2_common.glsl"

layout(set = 0, binding = 1) uniform sampler2D cloud_blend_from_texture;
layout(set = 0, binding = 2) uniform sampler2D cloud_blend_to_texture;

layout(location = 0) in vec2 frag_position;
layout(location = 0) out vec4 out_color;

vec4 cloud_ref_2_sample_blended_cloud(vec2 uv) {
    vec4 blend_from = texture(cloud_blend_from_texture, uv);
    vec4 blend_to = texture(cloud_blend_to_texture, uv);
    return mix(blend_from, blend_to, clamp(params.cache_status.x, 0.0, 1.0));
}

bool cloud_ref_2_inside_update_region(vec2 oct_uv) {
    vec2 pixel = oct_uv * params.cache_region.w;
    vec2 region_min = params.cache_region.xy;
    vec2 region_max = region_min + vec2(params.cache_region.z);
    return pixel.x >= region_min.x && pixel.x < region_max.x &&
           pixel.y >= region_min.y && pixel.y < region_max.y;
}

vec3 cloud_ref_2_final_post(vec3 color, vec3 direction, float cloud_alpha) {
    vec3 sun_dir = normalize(params.sun_direction_intensity.xyz);
    float sun_alignment = max(dot(direction, sun_dir), 0.0);
    float horizon = pow(max(1.0 - abs(direction.y), 0.0), 3.0);
    float halo = pow(sun_alignment, 38.0) * params.sun_direction_intensity.w;
    float tight_glare = pow(sun_alignment, 420.0) * params.sun_direction_intensity.w;

    color += vec3(1.0, 0.58, 0.22) * halo * (0.10 + 0.16 * cloud_alpha);
    color += vec3(1.0, 0.82, 0.50) * tight_glare * 1.25;
    color += vec3(0.10, 0.12, 0.13) * horizon * (1.0 - cloud_alpha) * 0.22;
    float luma = dot(color, vec3(0.2126, 0.7152, 0.0722));
    color = mix(vec3(luma), color, 1.08);
    color = max((color - vec3(0.018)) * 1.10, vec3(0.0));
    color = pow(max(color, vec3(0.0)), vec3(1.02));
    return color;
}

void main() {
    int debug_view = int(params.ref_options.x + 0.5);
    bool final_view = debug_view == CLOUD_REF_2_DEBUG_FINAL;
    bool raw_final_view = debug_view == CLOUD_REF_2_DEBUG_RAW_FINAL;
    vec3 direction = cloud_ref_2_view_direction(frag_position);
    vec2 oct_uv = cloud_ref_2_direction_to_oct_uv(direction);
    vec3 background = cloud_ref_2_background(direction);
    bool above_horizon = direction.y >= 0.0;
    vec4 blend_from = above_horizon ? texture(cloud_blend_from_texture, oct_uv)
                                    : vec4(0.0, 0.0, 0.0, 1.0);
    vec4 blend_to = above_horizon ? texture(cloud_blend_to_texture, oct_uv) : blend_from;
    vec4 cloud = above_horizon ? cloud_ref_2_sample_blended_cloud(oct_uv) : blend_from;
    vec3 color = background * clamp(cloud.a, 0.0, 1.0) + cloud.rgb;
    if (debug_view == CLOUD_REF_2_DEBUG_BACKGROUND) {
        color = background;
    } else if (debug_view == CLOUD_REF_2_DEBUG_RAW_CLOUD_PRODUCT) {
        color = max(cloud.rgb, vec3(0.0));
    } else if (debug_view == CLOUD_REF_2_DEBUG_BLEND_FROM) {
        color = max(blend_from.rgb, vec3(0.0));
    } else if (debug_view == CLOUD_REF_2_DEBUG_BLEND_TO) {
        color = max(blend_to.rgb, vec3(0.0));
    } else if (debug_view == CLOUD_REF_2_DEBUG_UPDATE_REGION) {
        color = vec3(oct_uv, 0.18);
        if (cloud_ref_2_inside_update_region(oct_uv)) {
            color = mix(color, vec3(1.0, 0.18, 0.05), 0.82);
        }
    } else if (debug_view == CLOUD_REF_2_DEBUG_OCT_UV) {
        color = vec3(oct_uv, 0.0);
    } else if (debug_view == CLOUD_REF_2_DEBUG_CACHE_ALPHA) {
        color = vec3(clamp(cloud.a, 0.0, 1.0), 1.0 - clamp(cloud.a, 0.0, 1.0), 0.0);
    } else if (debug_view == CLOUD_REF_2_DEBUG_CLOUD_ALPHA) {
        color = vec3(1.0 - clamp(cloud.a, 0.0, 1.0));
    } else if (raw_final_view) {
        color = max(color, vec3(0.0));
    } else if (!final_view) {
        color = cloud.rgb;
    } else {
        color = cloud_ref_2_final_post(color, direction, 1.0 - clamp(cloud.a, 0.0, 1.0));
    }
    out_color = vec4(cloud_ref_2_tonemap(color), 1.0);
}
