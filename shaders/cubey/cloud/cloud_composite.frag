#version 450
#extension GL_GOOGLE_include_directive : require

#include "cubey/cloud/cloud_common.glsl"

layout(set = 0, binding = 1) uniform sampler2D cloud_product_texture;
layout(set = 0, binding = 2) uniform sampler2D cloud_metadata_texture;

layout(location = 0) in vec2 frag_position;
layout(location = 0) out vec4 out_color;

#include "cubey/cloud/cloud_resolve_common.glsl"

vec3 cloud_final_post(vec3 color, vec3 direction, float cloud_alpha) {
    vec3 sun_dir = normalize(params.sun_direction_intensity.xyz);
    vec3 up = cloud_planet_up();
    float sun_elevation = dot(sun_dir, up);
    float day = smoothstep(-0.05, 0.16, sun_elevation);
    float twilight =
        smoothstep(-0.24, 0.02, sun_elevation) *
        (1.0 - smoothstep(0.04, 0.30, sun_elevation));
    float lit_post = max(day, twilight * 0.42);
    float sun_alignment = max(dot(direction, sun_dir), 0.0);
    float horizon = pow(max(1.0 - abs(direction.y), 0.0), 3.0);
    float sun_post_intensity = params.sun_direction_intensity.w * lit_post;
    float halo = pow(sun_alignment, 38.0) * sun_post_intensity;
    float tight_glare = pow(sun_alignment, 420.0) * sun_post_intensity;
    float glare_strength = clamp(params.lighting_strengths.w, 0.0, 3.0);
    float horizon_strength = clamp(params.composite_options.w, 0.0, 3.0);
    float contrast = max(params.composite_options.y, 0.0);
    float saturation = max(params.composite_options.z, 0.0);
    float surface_view = cloud_surface_view_factor();
    float surface_haze = mix(1.0, cloud_weather_haze_factor(), surface_view);

    color += vec3(1.0, 0.58, 0.22) * halo * (0.10 + 0.16 * cloud_alpha) *
             glare_strength * mix(1.0, 0.42, surface_view);
    color += vec3(1.0, 0.82, 0.50) * tight_glare * 1.25 * glare_strength *
             mix(1.0, 0.34, surface_view);
    color += vec3(0.10, 0.12, 0.13) * horizon * (1.0 - cloud_alpha) * 0.22 *
             horizon_strength * mix(0.18, 1.0, lit_post) *
             mix(1.0, 0.54, surface_view) * surface_haze;
    color *= mix(1.0, mix(0.92, 0.74, surface_view), lit_post);
    float luma = dot(color, vec3(0.2126, 0.7152, 0.0722));
    float regime_saturation =
        saturation * mix(0.34, mix(0.72, 1.06, surface_view), lit_post);
    color = mix(vec3(luma), color, regime_saturation);
    float black_point = mix(0.0035, mix(0.018, 0.035, surface_view), lit_post);
    float regime_contrast = contrast * mix(0.58, mix(0.82, 1.06, surface_view), lit_post);
    color = max((color - vec3(black_point)) * regime_contrast,
                vec3(0.0));
    color = pow(max(color, vec3(0.0)), vec3(mix(0.92, 1.02, lit_post)));
    return color;
}

void main() {
    vec2 uv = frag_position * 0.5 + 0.5;
    int debug_view = int(params.ref_options.x + 0.5);
    bool final_view = debug_view == CLOUD_DEBUG_FINAL;
    bool raw_final_view = debug_view == CLOUD_DEBUG_RAW_FINAL;
    vec3 direction = cloud_view_direction(frag_position);
    vec3 background = cloud_background(direction);
    vec4 raw_cloud = texture(cloud_product_texture, uv);
    vec4 raw_metadata = texture(cloud_metadata_texture, uv);
    vec4 resolved_cloud = cloud_resolve_cloud_product(uv, direction);
    float edge_mask = cloud_resolve_edge_mask(uv, raw_cloud, raw_metadata, direction);
    float resolve_strength = cloud_resolve_strength(uv, raw_cloud, raw_metadata, direction);
    vec4 cloud = final_view ? mix(raw_cloud, resolved_cloud, resolve_strength)
                            : raw_cloud;
    vec3 color = background * clamp(cloud.a, 0.0, 1.0) + cloud.rgb;
    if (debug_view == CLOUD_DEBUG_SCENE_DEPTH_OCCLUSION) {
        color = vec3(0.0);
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
        color = cloud_final_post(color, direction, 1.0 - clamp(cloud.a, 0.0, 1.0));
    }
    out_color = vec4(cloud_tonemap(color), 1.0);
}
