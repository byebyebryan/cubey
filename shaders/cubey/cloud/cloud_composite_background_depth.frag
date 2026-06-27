#version 450
#extension GL_GOOGLE_include_directive : require

#include "cubey/cloud/cloud_common.glsl"

layout(set = 0, binding = 1) uniform sampler2D cloud_product_texture;
layout(set = 0, binding = 2) uniform sampler2D cloud_metadata_texture;
layout(set = 0, binding = 3) uniform sampler2D background_texture;
layout(set = 0, binding = 4) uniform sampler2D scene_depth_texture;

layout(location = 0) in vec2 frag_position;
layout(location = 0) out vec4 out_color;

float cloud_product_alpha(vec4 cloud) {
    return 1.0 - clamp(cloud.a, 0.0, 1.0);
}

float cloud_metadata_alpha(vec4 cloud, vec4 metadata) {
    return max(cloud_product_alpha(cloud), clamp(metadata.g, 0.0, 1.0));
}

float cloud_resolve_neighbor_weight(vec4 center_cloud, vec4 center_metadata,
                                    vec4 sample_cloud, vec4 sample_metadata,
                                    ivec2 offset) {
    float center_alpha = cloud_metadata_alpha(center_cloud, center_metadata);
    float sample_alpha = cloud_metadata_alpha(sample_cloud, sample_metadata);
    float kernel_weight = (abs(offset.x) + abs(offset.y) == 1) ? 0.52 : 0.32;
    if (max(center_alpha, sample_alpha) < 0.002) {
        return kernel_weight;
    }

    float alpha_weight = exp(-abs(sample_alpha - center_alpha) * 6.0);
    float center_confidence = clamp(center_metadata.b, 0.0, 1.0);
    float sample_confidence = clamp(sample_metadata.b, 0.0, 1.0);
    float confidence = min(center_confidence, sample_confidence);
    float confidence_weight = mix(0.32, 1.0, confidence);

    float distance_weight = 1.0;
    float center_distance = max(center_metadata.r, 0.0);
    float sample_distance = max(sample_metadata.r, 0.0);
    if (confidence > 0.01 && max(center_alpha, sample_alpha) > 0.02) {
        float distance_scale = max(min(center_distance, sample_distance) * 0.035, 1200.0);
        distance_weight = exp(-abs(sample_distance - center_distance) / distance_scale);
    }
    return kernel_weight * alpha_weight * distance_weight * confidence_weight;
}

vec4 cloud_resolve_cloud_product(vec2 uv) {
    ivec2 size = textureSize(cloud_product_texture, 0);
    vec2 texel = 1.0 / vec2(max(size, ivec2(1)));
    vec4 center = texture(cloud_product_texture, uv);
    vec4 center_metadata = texture(cloud_metadata_texture, uv);
    vec4 total = center * 1.6;
    float total_weight = 1.6;

    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            if (x == 0 && y == 0) {
                continue;
            }
            vec2 sample_uv = uv + vec2(float(x), float(y)) * texel;
            vec4 sample_value = texture(cloud_product_texture, sample_uv);
            vec4 sample_metadata = texture(cloud_metadata_texture, sample_uv);
            float weight = cloud_resolve_neighbor_weight(center, center_metadata, sample_value,
                                                         sample_metadata, ivec2(x, y));
            total += sample_value * weight;
            total_weight += weight;
        }
    }
    return total / max(total_weight, 0.0001);
}

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

vec3 cloud_external_final_post(vec3 color, vec3 direction, float cloud_alpha) {
    vec3 sun_dir = normalize(params.sun_direction_intensity.xyz);
    float sun_alignment = max(dot(direction, sun_dir), 0.0);
    float horizon = pow(max(1.0 - abs(direction.y), 0.0), 3.0);
    float halo = pow(sun_alignment, 38.0) * params.sun_direction_intensity.w;
    float tight_glare = pow(sun_alignment, 420.0) * params.sun_direction_intensity.w;
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
             horizon_strength * mix(1.0, 0.54, surface_view) * surface_haze;
    color *= mix(1.0, 0.82, surface_view);

    float luma = dot(color, vec3(0.2126, 0.7152, 0.0722));
    color = mix(vec3(luma), color, saturation * mix(1.0, 1.06, surface_view));
    color = max((color - vec3(mix(0.018, 0.035, surface_view))) *
                    contrast * mix(1.0, 1.06, surface_view),
                vec3(0.0));
    color = pow(max(color, vec3(0.0)), vec3(1.02));
    return color;
}

vec3 cloud_metadata_debug_color(vec2 uv, int debug_view) {
    vec4 metadata = texture(cloud_metadata_texture, uv);
    if (debug_view == CLOUD_DEBUG_METADATA_DISTANCE) {
        return vec3(clamp(metadata.r / 50000.0, 0.0, 1.0));
    }
    if (debug_view == CLOUD_DEBUG_METADATA_ALPHA) {
        return vec3(clamp(metadata.g, 0.0, 1.0));
    }
    if (debug_view == CLOUD_DEBUG_METADATA_CONFIDENCE) {
        return vec3(clamp(metadata.b, 0.0, 1.0));
    }
    if (debug_view == CLOUD_DEBUG_METADATA_DENSITY) {
        return vec3(clamp(metadata.a * 16.0, 0.0, 1.0));
    }
    return vec3(0.0);
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
    vec4 resolved_cloud = cloud_resolve_cloud_product(uv);
    float resolve_strength = clamp(params.composite_options.x, 0.0, 1.0);
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
    } else if (raw_final_view) {
        color = max(color, vec3(0.0));
    } else if (!final_view) {
        color = cloud.rgb;
    } else {
        vec3 posted = cloud_external_final_post(color, direction, cloud_alpha);
        color = mix(color, posted, clamp(cloud_alpha * 1.15, 0.0, 1.0));
    }
    out_color = vec4(max(color, vec3(0.0)), 1.0);
}
